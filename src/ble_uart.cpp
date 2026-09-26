// ============================================================================
//  ble_uart.cpp - BLE 串口（Nordic UART Service，NUS）
//
//  为什么用 BLE 而不是经典蓝牙 SPP：
//      ESP32-S3 只有 BLE 4.2/5.0 控制器，**没有**经典蓝牙(BR/EDR)，
//      所以 BluetoolthSerial / SPP 在这颗芯片上根本不存在，只能走 GATT。
//      为了让手机端通用，这里用最常见的 NUS UUID：
//          服务  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
//          RX    6E400002-...  (手机 write  → 设备收)
//          TX    6E400003-...  (设备 notify → 手机收)
//      nRF Connect / 各类"BLE 串口助手"都能直接认出这个服务。
//
//  线程模型：
//      - GATT 回调跑在 Bluedroid 自己的线程里 → 只允许它碰环形缓冲和
//        几个 volatile 标志，绝不碰 LVGL 对象。
//      - LVGL 对象只在 LVGL 任务（core0）里改，靠 bleuart_pump() 每帧拉取。
// ============================================================================

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "app_shared.h"
#include "ui_test/screens/ui_ble.h"

// ---------------------------------------------------------------------------
// UUID（Nordic UART Service）
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME   "ESP32S3-BLE-UART"
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_CHAR_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // 手机 → 设备
#define NUS_TX_CHAR_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // 设备 → 手机

// ---------------------------------------------------------------------------
// 全局状态（在 app_shared.h 里 extern 声明）
// 注意：这一组变量只能在这里定义一次。main.cpp 里只保留一把锁的静态初始化，
//       （曾经两边都写定义，链接时报 multiple definition）
// ---------------------------------------------------------------------------
volatile uint8_t  g_bleRxRing[BLE_RX_RING_SIZE];
volatile uint16_t g_bleRxHead = 0;
volatile uint16_t g_bleRxTail = 0;
portMUX_TYPE      g_bleRxMux = portMUX_INITIALIZER_UNLOCKED;   // 见 app_shared.h 的说明
volatile bool     g_bleConnected = false;
volatile uint32_t g_bleRxTotal = 0;
volatile bool     g_bleRxPending = false;   // core1 置位，core0 消费
char              g_blePeerAddr[20] = {0};

// ---------------------------------------------------------------------------
// 本文件私有
// ---------------------------------------------------------------------------
static BLEServer*         ble_server = NULL;
static BLECharacteristic* ble_tx_char = NULL;    // notify 用
static BLEAdvertising*    ble_adv    = NULL;
static volatile bool      ble_ready  = false;    // 协议栈已起来（可以收/发）

// 接收显示缓冲（LVGL 任务和 BLE 任务都会写，必须加锁）
static char     rx_disp[512];
static uint16_t rx_disp_len = 0;
static volatile bool rx_dirty = false;
static uint32_t last_pump_ms = 0;

// 【关键】rx_disp / rx_disp_len 有两个写者：
//   core0 LVGL 任务：bleuart_send() 里的回显
//   core1 BLE 任务 ：bleuart_pump() 把收到的数据搬进来
// 不加锁的话，一次发送要持续上百毫秒（分片+delay），core1 必然并发进来，
// 两边互相覆盖 rx_disp_len，接收区就变成交错的乱码 —— 也就是"发送时花屏"。
// 同样用临界区而非互斥量：写入最长 512B 的 memcpy，微秒级，且绝不能在
// 高优先级协议栈线程里冒 priority-inheritance 断言的风险。
static portMUX_TYPE rx_disp_mux = portMUX_INITIALIZER_UNLOCKED;

// 数据统计
static uint32_t tx_total = 0;

#define BLE_PUMP_INTERVAL_MS 150     // 每 150ms 刷新一次接收区，避免频繁重绘

// ---------------------------------------------------------------------------
// 环形缓冲：只由 BLE 回调线程写，LVGL 任务读
// ---------------------------------------------------------------------------
static void ble_ring_push(const uint8_t* data, size_t len)
{
    if (data == NULL || len == 0) return;
    portENTER_CRITICAL(&g_bleRxMux);
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((g_bleRxHead + 1) & (BLE_RX_RING_SIZE - 1));
        if (next == g_bleRxTail) break;          // 满了就丢新数据，不覆盖旧的
        g_bleRxRing[g_bleRxHead] = data[i];
        g_bleRxHead = next;
    }
    portEXIT_CRITICAL(&g_bleRxMux);
}

static size_t ble_ring_pop(uint8_t* out, size_t max_len)
{
    if (out == NULL || max_len == 0) return 0;
    size_t n = 0;
    portENTER_CRITICAL(&g_bleRxMux);
    while (n < max_len && g_bleRxTail != g_bleRxHead) {
        out[n++] = g_bleRxRing[g_bleRxTail];
        g_bleRxTail = (uint16_t)((g_bleRxTail + 1) & (BLE_RX_RING_SIZE - 1));
    }
    portEXIT_CRITICAL(&g_bleRxMux);
    return n;
}

// ---------------------------------------------------------------------------
// 接收显示缓冲：满了就丢掉最老的一半（保留结尾，和串口助手行为一致）
// 所有写入都必须在 rx_disp_mux 临界区里进行（两个核都会写）
// ---------------------------------------------------------------------------
static void rx_disp_append_unlocked(const char* s, size_t n)
{
    if (n == 0) return;
    if (n >= sizeof(rx_disp)) {
        s += (n - (sizeof(rx_disp) - 1));
        n  = sizeof(rx_disp) - 1;
        rx_disp_len = 0;
    }
    if (rx_disp_len + n > sizeof(rx_disp) - 1) {
        uint16_t keep = (uint16_t)((sizeof(rx_disp) - 1) / 2);
        memmove(rx_disp, rx_disp + (rx_disp_len - keep), keep);
        rx_disp_len = keep;
    }
    memcpy(rx_disp + rx_disp_len, s, n);
    rx_disp_len = (uint16_t)(rx_disp_len + n);
    rx_disp[rx_disp_len] = '\0';
    rx_dirty = true;
}

static void rx_disp_append(const char* s, size_t n)
{
    if (s == NULL || n == 0) return;
    portENTER_CRITICAL(&rx_disp_mux);
    rx_disp_append_unlocked(s, n);
    portEXIT_CRITICAL(&rx_disp_mux);
}

// 把不可见字符转成 '.'，避免 \0 / 控制字符把 LVGL 的文本渲染搞乱
static void rx_disp_append_sanitized(const uint8_t* data, size_t len)
{
    char tmp[64];
    size_t k = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\r' || c == '\n' || c == '\t') tmp[k++] = (char)c;
        else if (c < 0x20 || c == 0x7F)          tmp[k++] = '.';
        else                                     tmp[k++] = (char)c;
        if (k == sizeof(tmp) - 1) {
            tmp[k] = '\0';
            rx_disp_append(tmp, k);
            k = 0;
        }
    }
    if (k > 0) {
        tmp[k] = '\0';
        rx_disp_append(tmp, k);
    }
}

// ---------------------------------------------------------------------------
// UI 刷新（只在 LVGL 任务里调用）
// ---------------------------------------------------------------------------
void ui_ble_set_status(void)
{
    if (ui_ble_status_label == NULL) return;
    char buf[64];
    if (!ble_ready) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_BULLET " BLE off");
        lv_label_set_text(ui_ble_status_label, buf);
        lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    if (g_bleConnected) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_BULLET " Connected  %s  RX %u / TX %u",
                 g_blePeerAddr, (unsigned)g_bleRxTotal, (unsigned)tx_total);
        lv_label_set_text(ui_ble_status_label, buf);
        lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_BULLET " Advertising as %s", BLE_DEVICE_NAME);
        lv_label_set_text(ui_ble_status_label, buf);
        lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0xFFFF88), LV_STATE_DEFAULT);
    }
}

// 刷新接收区：先快照（临界区，几微秒），再解锁去动 LVGL。
// 不能在临界区里调 lv_textarea_set_text —— 那是耗时操作，
// 会把蓝牙协议栈的中断响应拖长（发送时尤其明显）。
static void ui_ble_refresh_rx(void)
{
    if (ui_ble_rx_ta == NULL || !rx_dirty) return;

    static char snapshot[512];
    portENTER_CRITICAL(&rx_disp_mux);
    rx_dirty = false;
    memcpy(snapshot, rx_disp, (size_t)rx_disp_len + 1);
    portEXIT_CRITICAL(&rx_disp_mux);

    lv_textarea_set_text(ui_ble_rx_ta, snapshot);
    lv_textarea_set_cursor_pos(ui_ble_rx_ta, LV_TEXTAREA_CURSOR_LAST);
}

// ---------------------------------------------------------------------------
// BLE 回调（跑在协议栈线程，禁止碰 LVGL）
// ---------------------------------------------------------------------------
class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* srv) override
    {
        (void)srv;
        g_bleConnected = true;
        String addr = BLEDevice::getAddress().toString().c_str();
        strncpy(g_blePeerAddr, addr.c_str(), sizeof(g_blePeerAddr) - 1);
        g_blePeerAddr[sizeof(g_blePeerAddr) - 1] = '\0';
        Serial.printf("[BLE] connected: %s\n", g_blePeerAddr);
    }
    void onDisconnect(BLEServer* srv) override
    {
        g_bleConnected = false;
        g_blePeerAddr[0] = '\0';
        Serial.println("[BLE] disconnected, restart advertising");
        // 断连后必须重新开始广播，否则手机再也搜不到
        if (ble_adv) {
            BLEDevice::startAdvertising();
        }
        (void)srv;
    }
};

class BleRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* ch) override
    {
        // 新版核心会走带 param 的重载，这里保留是为了兼容旧签名
        if (ch == NULL) return;
        uint8_t* data = ch->getData();
        size_t   len  = ch->getLength();
        if (data == NULL || len == 0) return;
        ble_ring_push(data, len);
        g_bleRxTotal += (uint32_t)len;
    }
    void onWrite(BLECharacteristic* ch, esp_ble_gatts_cb_param_t* param) override
    {
        if (ch == NULL || param == NULL) return;
        uint8_t* data = ch->getData();
        size_t   len  = param->write.len;
        if (data == NULL || len == 0) return;
        if (len > ch->getLength()) len = ch->getLength();   // 防御：不越界读
        ble_ring_push(data, len);
        g_bleRxTotal += (uint32_t)len;
    }
};

// ---------------------------------------------------------------------------
// 初始化（由 BLE 任务调用，放在 setup() 里会拖慢启动）
// ---------------------------------------------------------------------------
void bleuart_init(void)
{
    if (ble_ready) return;

    Serial.printf("[BLE] init as \"%s\" ...\n", BLE_DEVICE_NAME);
    BLEDevice::init(BLE_DEVICE_NAME);
    BLEDevice::setMTU(247);                  // 单包最多 ~244 字节有效载荷

    ble_server = BLEDevice::createServer();
    if (ble_server == NULL) {
        Serial.println("[BLE] createServer failed");
        return;
    }
    ble_server->setCallbacks(new BleServerCallbacks());

    BLEService* svc = ble_server->createService(NUS_SERVICE_UUID);
    if (svc == NULL) {
        Serial.println("[BLE] createService failed");
        return;
    }

    // TX：设备 → 手机（notify）
    ble_tx_char = svc->createCharacteristic(
        NUS_TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    ble_tx_char->addDescriptor(new BLE2902());

    // RX：手机 → 设备（write / write without response）
    BLECharacteristic* rx_char = svc->createCharacteristic(
        NUS_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx_char->setCallbacks(new BleRxCallbacks());

    svc->start();

    ble_adv = BLEDevice::getAdvertising();
    if (ble_adv) {
        ble_adv->addServiceUUID(NUS_SERVICE_UUID);
        ble_adv->setScanResponse(true);
        ble_adv->setMinInterval(0x20);       // 20ms
        ble_adv->setMaxInterval(0x40);       // 40ms
        BLEDevice::startAdvertising();
    }

    ble_ready = true;
    Serial.println("[BLE] advertising started (NUS service)");
}

// ---------------------------------------------------------------------------
// 每帧任务（core1）：把环形缓冲搬进显示缓冲
// ---------------------------------------------------------------------------
void bleuart_pump(void)
{
    if (!ble_ready) return;

    uint8_t tmp[64];
    size_t n;
    while ((n = ble_ring_pop(tmp, sizeof(tmp))) > 0) {
        rx_disp_append_sanitized(tmp, n);
    }

    uint32_t now = millis();
    if (rx_dirty && (now - last_pump_ms) >= BLE_PUMP_INTERVAL_MS) {
        last_pump_ms = now;
        rx_dirty = false;
        // 不能在 core1 碰 LVGL：置个标志让 LVGL 任务去刷
        g_bleRxPending = true;
    }
}

bool bleuart_is_connected(void) { return g_bleConnected; }
bool bleuart_is_ready(void)     { return ble_ready; }

// ---------------------------------------------------------------------------
// 发送
// ---------------------------------------------------------------------------
size_t bleuart_send(const char* data, size_t len)
{
    if (data == NULL || len == 0) return 0;
    if (!ble_ready || !g_bleConnected || ble_tx_char == NULL) return 0;

    // MTU 247 → 有效载荷 244，留点余量按 180 分片，兼容没协商 MTU 的手机
    const size_t chunk_max = 180;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > chunk_max) chunk = chunk_max;
        ble_tx_char->setValue((uint8_t*)(data + sent), chunk);
        ble_tx_char->notify();
        sent += chunk;
        vTaskDelay(pdMS_TO_TICKS(8));        // 给协议栈一点时间把包发出去
    }
    tx_total += (uint32_t)sent;
    // 回显到接收区，方便确认"到底发出去了什么"
    rx_disp_append("> ", 2);
    rx_disp_append(data, len < 120 ? len : 120);
    if (len >= 120) rx_disp_append("...", 3);
    rx_disp_append("\n", 1);
    return sent;
}

// ---------------------------------------------------------------------------
// 1) 清空
// ---------------------------------------------------------------------------
extern "C" void ui_ble_on_clear_requested(void)
{
    portENTER_CRITICAL(&rx_disp_mux);
    rx_disp_len = 0;
    rx_disp[0] = '\0';
    rx_dirty = false;
    portEXIT_CRITICAL(&rx_disp_mux);
    g_bleRxPending = true;
    g_bleRxTotal = 0;
    Serial.println("[BLE] rx buffer cleared");
}

// ---------------------------------------------------------------------------
// 2) 发送框/键盘的 √ → 发送
// ---------------------------------------------------------------------------
extern "C" void ui_ble_on_send_requested(void)
{
    if (ui_ble_send_ta == NULL) return;
    const char* text = lv_textarea_get_text(ui_ble_send_ta);
    if (text == NULL || *text == '\0') return;

    if (!bleuart_is_ready()) {
        lv_label_set_text(ui_ble_status_label, "BLE not initialized");
        lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    if (!bleuart_is_connected()) {
        lv_label_set_text(ui_ble_status_label, "Not connected - pair first");
        lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }

    // 末尾补一个换行，跟串口助手的习惯一致（可选）
    String payload = String(text);
    payload += "\r\n";
    size_t sent = bleuart_send(payload.c_str(), payload.length());
    Serial.printf("[BLE] sent %u bytes\n", (unsigned)sent);

    lv_textarea_set_text(ui_ble_send_ta, "");
    g_bleRxPending = true;
}

// ---------------------------------------------------------------------------
// 3) 每帧任务（core0，LVGL 任务）
// ---------------------------------------------------------------------------
void bleuart_ui_service(void)
{
    static bool     last_connected = false;
    static uint32_t last_status_ms = 0;
    static bool     last_ready     = false;

    if (g_bleRxPending) {
        g_bleRxPending = false;
        ui_ble_refresh_rx();
    }

    uint32_t now = millis();
    if (g_bleConnected != last_connected || ble_ready != last_ready ||
        (now - last_status_ms) > 1000) {
        last_connected = g_bleConnected;
        last_ready     = ble_ready;
        last_status_ms = now;
        ui_ble_set_status();
    }
}
