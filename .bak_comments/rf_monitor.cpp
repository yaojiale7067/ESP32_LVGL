// ============================================================================
//  rf_monitor.cpp - 802.11 管理帧被动监听（只收不发）
//
//  工作方式：
//    · esp_wifi_set_promiscuous(true) + MGMT 过滤，回调里解析
//      beacon / probe-resp / probe-req / deauth / disassoc 五种子类型；
//    · 一个 core1 上的跳频任务按 1s/信道 轮转 1..13；
//    · 所有统计进静态表，读侧用 portMUX 临界区拿整块快照。
//
//  ★ 全文件没有任何发射路径（无 esp_wifi_80211_tx、无帧构造）。
//    这是被动频谱诊断：判断"网络老掉线是不是有人在打 deauth"、
//    "周围到底有多少 AP 在占信道"，仅此而已。
//
//  ★ 监听期间必须断开 STA：跳频会不断 esp_wifi_set_channel，
//    与关联态的信道锁定互斥。start 时先关 auto-reconnect 再 disconnect，
//    否则 Arduino WiFi 的内部重连会和跳频抢信道。stop 后置
//    g_wifiForceReconnect，由 WiFi_Task 立刻恢复连接。
//
//  ★ 回调标 IRAM_ATTR 且不调用任何 flash 常驻函数（memcpy/snprintf）：
//    回调跑在 wifi 任务上下文，若恰逢 NVS 写入（cache 短暂关闭），
//    取指/取常量落在 flash 上会直接崩。所以帧内拷贝全部手写循环。
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "rf_monitor.h"
#include "app_shared.h"

// 跨任务标志：rf_monitor 写，WiFi_Task / UI 读（定义在这里，声明在 app_shared.h）
volatile bool g_rfmonActive       = false;
volatile bool g_wifiForceReconnect = false;

static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static rfmon_stats_t s_stats;
static TaskHandle_t  s_task      = NULL;
static volatile bool s_run       = false;
static volatile bool s_task_done = true;

#define RFMON_DWELL_SLICES  20      // 每信道停留 20 x 50ms = 1s

// ---------------------------------------------------------------------------
//  OUI -> 厂商短名。只收录常见前缀，未命中的返回 "?"（UI 会退化成显示裸 MAC）。
//  这是"尽力而为"的速查表，不是权威数据库 —— OUI 注册表有几万条，
//  嵌入式设备放不全，查不到就如实显示 MAC，不猜。
// ---------------------------------------------------------------------------
static const struct { uint8_t oui[3]; const char * name; } kOuis[] = {
    { { 0x3C, 0x22, 0xFB }, "Apple"     },
    { { 0xF0, 0x18, 0x98 }, "Apple"     },
    { { 0xA4, 0x83, 0xE7 }, "Apple"     },
    { { 0x24, 0xA2, 0xE1 }, "Apple"     },
    { { 0x00, 0x1A, 0x11 }, "Google"    },
    { { 0x5C, 0xF3, 0x70 }, "Espressif" },
    { { 0x30, 0xAE, 0xA4 }, "Espressif" },
    { { 0x84, 0x0D, 0x8E }, "Espressif" },
    { { 0x50, 0xC7, 0xBF }, "TP-Link"   },
    { { 0xB0, 0x4E, 0x26 }, "TP-Link"   },
    { { 0xEC, 0x08, 0x6B }, "TP-Link"   },
    { { 0x28, 0x6C, 0x07 }, "Xiaomi"    },
    { { 0x64, 0x09, 0x80 }, "Xiaomi"    },
    { { 0x00, 0xE0, 0xFC }, "Huawei"    },
    { { 0x8C, 0x77, 0x12 }, "Samsung"   },
    { { 0xE8, 0x50, 0x8B }, "Samsung"   },
};

const char * rfmon_oui_vendor(const uint8_t * mac)
{
    if (mac == NULL) return "?";
    for (unsigned i = 0; i < sizeof(kOuis) / sizeof(kOuis[0]); i++) {
        if (kOuis[i].oui[0] == mac[0] && kOuis[i].oui[1] == mac[1] &&
            kOuis[i].oui[2] == mac[2])
            return kOuis[i].name;
    }
    return "?";
}

void rfmon_mac_str(const uint8_t * mac, char * out)
{
    if (mac == NULL || out == NULL) return;
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------------------------------------------------------------------------
//  表内查找（都必须在 s_mux 临界区内调用）。返回下标，未命中返回 -1。
//  "空槽"的判据是计数字段为 0 —— 插入时计数至少置 1，清零只发生在 rfmon_clear。
//  ★ IRAM_ATTR：这些只被混杂模式回调调用；驱动文档要求回调按中断上下文对待，
//    回调本体在 IRAM，它调用的函数也必须在 IRAM，否则白标。
//    它们只读写 .bss 里的静态表，天然 IRAM 安全（不碰 flash 常量）。
// ---------------------------------------------------------------------------
static int IRAM_ATTR find_ap(const uint8_t * bssid)
{
    for (int i = 0; i < RFMON_MAX_AP; i++) {
        if (s_stats.aps[i].beacons == 0) continue;
        bool m = true;
        for (int j = 0; j < 6; j++)
            if (s_stats.aps[i].bssid[j] != bssid[j]) { m = false; break; }
        if (m) return i;
    }
    return -1;
}

static int IRAM_ATTR find_free_ap(void)
{
    for (int i = 0; i < RFMON_MAX_AP; i++)
        if (s_stats.aps[i].beacons == 0) return i;
    return -1;
}

static int IRAM_ATTR find_deauth_src(const uint8_t * bssid)
{
    for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++) {
        if (s_stats.deauth_srcs[i].frames == 0) continue;
        bool m = true;
        for (int j = 0; j < 6; j++)
            if (s_stats.deauth_srcs[i].bssid[j] != bssid[j]) { m = false; break; }
        if (m) return i;
    }
    return -1;
}

static int IRAM_ATTR find_free_deauth_src(void)
{
    for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++)
        if (s_stats.deauth_srcs[i].frames == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
//  混杂模式回调：跑在 wifi 任务上下文（高优先级），必须短、必须 IRAM 安全。
//  只做计数与表更新，不打印、不分配、不调 flash 函数。
// ---------------------------------------------------------------------------
static void IRAM_ATTR rfmon_rx_cb(void * buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT || buf == NULL) return;

    const wifi_promiscuous_pkt_t * pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t * f = pkt->payload;
    if (f == NULL) return;

    // sig_len 含 4 字节 FCS（esp_wifi_types.h:430 的注释），
    // 不扣掉的话 tag 解析会把 FCS 当成 tag 读，边界检查也就形同虚设。
    uint32_t raw = pkt->rx_ctrl.sig_len;
    if (raw <= 4) return;
    uint32_t len = raw - 4;                     // 真正的 802.11 帧长
    if (len < 24) return;                       // 连 MGMT 头都不完整

    uint16_t fc = (uint16_t)(f[0] | ((uint16_t)f[1] << 8));
    if (((fc >> 2) & 0x3) != 0) return;         // type != MGMT（过滤器兜底）
    uint8_t st = (uint8_t)((fc >> 4) & 0xF);

    uint8_t ch   = pkt->rx_ctrl.channel;
    int8_t  rssi = (int8_t)pkt->rx_ctrl.rssi;
    const uint8_t * src = f + 10;               // addr2 = 发送方
    const uint8_t * bss = f + 16;               // addr3 = BSSID

    // ---- deauth(12) / disassoc(10)：记源、计数 ----
    if (st == 12 || st == 10) {
        portENTER_CRITICAL(&s_mux);
        if (st == 12) { if (s_stats.deauths  < 0xFFFFFFFFu) s_stats.deauths++;  }
        else          { if (s_stats.disassoc < 0xFFFFFFFFu) s_stats.disassoc++; }
        int idx = find_deauth_src(src);
        if (idx < 0) {
            idx = find_free_deauth_src();
            if (idx >= 0) {
                for (int j = 0; j < 6; j++) s_stats.deauth_srcs[idx].bssid[j] = src[j];
                s_stats.deauth_srcs[idx].frames = 0;
                s_stats.deauth_src_count = (uint8_t)(idx + 1);
                s_stats.version++;
            }
        }
        if (idx >= 0 && s_stats.deauth_srcs[idx].frames < 0xFFFF)
            s_stats.deauth_srcs[idx].frames++;
        portEXIT_CRITICAL(&s_mux);
        return;
    }

    // ---- probe request(4)：只计数（广播探测没有 SSID 可归并）----
    if (st == 4) {
        portENTER_CRITICAL(&s_mux);
        if (s_stats.probes < 0xFFFFFFFFu) s_stats.probes++;
        portEXIT_CRITICAL(&s_mux);
        return;
    }

    // ---- beacon(8) / probe response(5)：进 AP 表 ----
    if (st == 8 || st == 5) {
        // 提取 SSID（tag 0）。beacon/probe-resp 的固定参数 12B，tags 从 36 起。
        char ssid[33];
        int  ssid_len = 0;
        uint32_t off = 36;
        while (off + 2 <= len) {
            uint8_t id = f[off], tl = f[off + 1];
            if (off + 2u + tl > len) break;     // tag 越界，帧被截断
            if (id == 0) {
                ssid_len = (tl > 32) ? 32 : (int)tl;
                for (int j = 0; j < ssid_len; j++) ssid[j] = (char)f[off + 2 + j];
                break;
            }
            off += 2u + tl;
        }
        ssid[ssid_len] = '\0';

        portENTER_CRITICAL(&s_mux);
        if (s_stats.beacons < 0xFFFFFFFFu) s_stats.beacons++;
        int idx = find_ap(bss);
        if (idx >= 0) {
            if (s_stats.aps[idx].beacons < 0xFFFF) s_stats.aps[idx].beacons++;
            s_stats.aps[idx].rssi = rssi;
            s_stats.aps[idx].ch   = ch;
            // 之前只抓到过隐藏信标、这次带出 SSID 了（probe resp 常见）→ 补上
            if (s_stats.aps[idx].ssid[0] == '\0' && ssid_len > 0) {
                for (int j = 0; j <= ssid_len; j++) s_stats.aps[idx].ssid[j] = ssid[j];
                s_stats.version++;
            }
        } else {
            idx = find_free_ap();
            if (idx >= 0) {
                for (int j = 0; j < 6; j++) s_stats.aps[idx].bssid[j] = bss[j];
                for (int j = 0; j <= ssid_len; j++) s_stats.aps[idx].ssid[j] = ssid[j];
                s_stats.aps[idx].ch      = ch;
                s_stats.aps[idx].rssi    = rssi;
                s_stats.aps[idx].beacons = 1;
                s_stats.ap_count = (uint8_t)(idx + 1);
                s_stats.version++;
            }
            // 表满了就丢弃新 AP：这是被动观察工具，不做换出策略，
            // 16 个槽对"看周围有什么"够用；要看全量请用 /chan 扫描。
        }
        portEXIT_CRITICAL(&s_mux);
    }
}

// ---------------------------------------------------------------------------
//  跳频任务：1s 一个信道，1..13 循环。50ms 一片是为了 stop 能及时生效。
// ---------------------------------------------------------------------------
static void rfmon_task(void * arg)
{
    (void)arg;
    uint8_t ch = 1;
    while (s_run) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        portENTER_CRITICAL(&s_mux);
        s_stats.channel = ch;
        portEXIT_CRITICAL(&s_mux);
        ch = (ch >= 13) ? 1 : (uint8_t)(ch + 1);
        for (int i = 0; i < RFMON_DWELL_SLICES && s_run; i++)
            vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_task_done = true;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
void rfmon_clear(void)
{
    portENTER_CRITICAL(&s_mux);
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.active  = g_rfmonActive;
    s_stats.version = 1;             // 从 1 起：0 留给"从未有过数据"
    portEXIT_CRITICAL(&s_mux);
}

void rfmon_start(void)
{
    if (g_rfmonActive) return;

    rfmon_clear();

    g_rfmonActive = true;            // WiFi_Task 从此不再重连、不再受理扫描
    // ★ 顺序必须是"先关自动重连，再断开"：Arduino WiFi 默认 auto-reconnect，
    //   disconnect 事件会立刻触发内部重连，把信道抢回 AP 的信道，跳频就废了。
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);          // wifioff=false：射频保持开启
    delay(80);                       // 等断开事件走完

    // ★ 模式要显式钉成 STA：promiscuous 在 STA/NULL 下都能开，但若当前是
    //   WIFI_MODE_NULL（从来没配过网），esp_wifi_set_promiscuous 会返回
    //   ESP_ERR_WIFI_MODE。这里统一到 STA，行为可预测。
    WiFi.mode(WIFI_STA);
    delay(20);

    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;   // 只要管理帧，数据帧全丢
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(rfmon_rx_cb);

    esp_err_t e = esp_wifi_set_promiscuous(true);
    if (e != ESP_OK) {
        Serial.printf("[rfmon] set_promiscuous failed: %d\n", (int)e);
        g_rfmonActive = false;
        WiFi.setAutoReconnect(true);
        g_wifiForceReconnect = true;
        s_task_done = true;
        return;
    }

    s_run       = true;
    s_task_done = false;
    // core1 / 优先级 1：和 SD_Task 同级，压不过 WiFi_Task(2)，
    // 跳频任务被抢占也无所谓 —— 晚几毫秒换信道不影响统计。
    if (xTaskCreatePinnedToCore(rfmon_task, "rfmon", 3072, NULL, 1, &s_task, 1) != pdPASS) {
        Serial.println("[rfmon] task create FAILED");
        esp_wifi_set_promiscuous(false);
        g_rfmonActive = false;
        WiFi.setAutoReconnect(true);
        g_wifiForceReconnect = true;
        s_task_done = true;
        return;
    }

    portENTER_CRITICAL(&s_mux);
    s_stats.active = true;
    portEXIT_CRITICAL(&s_mux);
    Serial.println("[rfmon] promiscuous MGMT capture started, hopping CH1..13 @1s");
}

void rfmon_stop(void)
{
    if (!g_rfmonActive) return;

    s_run = false;
    // 等跳频任务退出（最长 50ms 片 + 裕量）
    for (int i = 0; i < 40 && !s_task_done; i++) delay(5);

    esp_wifi_set_promiscuous(false);

    portENTER_CRITICAL(&s_mux);
    s_stats.active = false;
    portEXIT_CRITICAL(&s_mux);

    g_rfmonActive = false;
    WiFi.setAutoReconnect(true);
    g_wifiForceReconnect = true;     // WiFi_Task 下一拍立即重连
    Serial.println("[rfmon] stopped, WiFi will reconnect");
}

bool rfmon_is_active(void)
{
    return g_rfmonActive;
}

void rfmon_get_stats(rfmon_stats_t * out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&s_mux);
    memcpy(out, &s_stats, sizeof(*out));     // ≈790B，微秒级
    portEXIT_CRITICAL(&s_mux);
}
