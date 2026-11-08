#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <WiFi.h>

#include "app_shared.h"

extern void original_setup();
extern void original_loop();

// 无 /wifi.cfg 时的兜底 WiFi（明文凭据，正式产品请移除）
#define FIXED_SSID     "ChinaNet-qMyM"
#define FIXED_PASSWORD "4r9mec6e"

QueueHandle_t wifiConfigQueue      = NULL;
QueueHandle_t wifiScanRequestQueue = NULL;
QueueHandle_t wifiScanResultQueue  = NULL;
QueueHandle_t wifiChanScanReqQueue = NULL;
QueueHandle_t wifiChanScanResQueue = NULL;
QueueHandle_t serialCmdQueue       = NULL;

WifiConfigMsg_t   g_wifiBootCfg;
SemaphoreHandle_t g_sdMutex = NULL;

// 静态初始化阶段建锁，保证 storage_mount() 在任何调用点都有锁可拿
namespace { struct SdMutexInit { SdMutexInit() { g_sdMutex = xSemaphoreCreateRecursiveMutex(); } } sdMutexInit; }

// g_spiBusMutex 定义在 display_port.cpp，BLE 全局量定义在 ble_uart.cpp，这里都不要重复定义
// （否则报 multiple definition）

extern bool storage_mount();
extern bool wifi_load_config_from_sd(char* ssid, char* pwd, size_t ssid_size, size_t pwd_size);

void LVGL_Task(void* pvParameters) {
    (void)pvParameters;
    original_setup();
    while (1) {
        original_loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SD_Task(void* pvParameters) {
    (void)pvParameters;
    Serial.println("[SD_Task] Running (idle)");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

#if ENABLE_BLE

void BLE_Task(void* pvParameters) {
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    bleuart_init();

    while (1) {
        bleuart_pump();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

// 注意：所有 SD 访问都必须走 SD_LOCK()，否则 core0/core1 会同时操作 HSPI

// ---------------------------------------------------------------------------
//  把一个 AP 插入 aps[]（按 RSSI 降序的定长窗口，容量 APP_CSCAN_MAX_AP）。
//  满了就只保留更强的，最弱的那个被挤掉 —— 详情列表要显示的是"信号最好的那些"。
//  ★ 这里只做展示截断；ap_per_ch[] 的计数是全量的，不受这个窗口影响。
// ---------------------------------------------------------------------------
static void cscan_insert_ap(ChanScanResult_t* r, const char* ssid, int32_t rssi,
                            uint8_t ch, uint8_t enc)
{
    const int cap = APP_CSCAN_MAX_AP;
    int pos;
    if (r->count < cap) {
        pos = r->count;                       // 没满：直接在尾部腾位
    } else {
        if (rssi <= r->aps[cap - 1].rssi) return;   // 满了且比最弱的还弱，丢弃
        pos = cap - 1;                        // 满了：最弱的那个被移位覆盖掉
    }
    while (pos > 0 && r->aps[pos - 1].rssi < rssi) {
        r->aps[pos] = r->aps[pos - 1];
        pos--;
    }
    memset(&r->aps[pos], 0, sizeof(r->aps[pos]));
    strncpy(r->aps[pos].ssid, ssid ? ssid : "", APP_SSID_MAX_LEN - 1);
    r->aps[pos].rssi    = (int8_t)rssi;
    r->aps[pos].channel = ch;
    r->aps[pos].enc     = enc;
    if (r->count < cap) r->count++;
}

// ---------------------------------------------------------------------------
//  信道扫描：一次 scanNetworks 拿全量 AP，然后在本地聚合成信道占用图。
//
//  ★ 不逐信道扫（13 次 scanNetworks）：每信道 active 停留 300ms，13 次要 4s 以上，
//    而且 WiFi_Task 在这期间完全阻塞。全信道一次扫同样是 4s 量级，
//    拿到的信息却更多（还能给出"哪个信道最干净"的推荐）。
//  ★ show_hidden = true：隐藏 SSID 的 AP 不发 SSID 但照样发信标、照样占频谱，
//    信道占用分析必须把它算进去。网络列表那边反而是 false（隐藏的名字连不上）。
// ---------------------------------------------------------------------------
static void wifi_run_channel_scan(void)
{
    ChanScanResult_t r;
    memset(&r, 0, sizeof(r));
    for (int c = WCH_MIN; c <= WCH_MAX; c++) r.best_rssi[c] = -127;

    int16_t n = WiFi.scanNetworks(false, true, false, 300, 0);
    r.scan_ret = n;
    Serial.printf("[WiFi_Task] chan scan -> %d APs\n", (int)n);
    if (n < 0) n = 0;
    // WiFi.SSID(i)/channel(i)/RSSI(i) 的下标是 uint8_t，n 超过 255 会绕回 0 变成重复计数。
    // 实际扫不到这么多（驱动的记录表远小于此），但绕回是静默错误，必须先夹住。
    if (n > 255) n = 255;

    for (int i = 0; i < n; i++) {
        int32_t ch = WiFi.channel(i);
        int32_t rs = WiFi.RSSI(i);
        // 5GHz AP 或驱动给出的异常信道号直接跳过：数组只有 1..13
        if (ch < WCH_MIN || ch > WCH_MAX) continue;

        if (r.ap_per_ch[ch] < 255) r.ap_per_ch[ch]++;
        if (rs > r.best_rssi[ch]) r.best_rssi[ch] = (int8_t)rs;
        cscan_insert_ap(&r, WiFi.SSID(i).c_str(), rs, (uint8_t)ch,
                        (uint8_t)WiFi.encryptionType(i));
    }
    WiFi.scanDelete();

    // 干扰负载：20MHz 信道横跨 ±2，所以看邻道之和，而不是只看本信道。
    // 只按本信道数挑信道是常见错误 —— 6 号看着空，但 4/5/7/8 全满时一样吵。
    for (int c = WCH_MIN; c <= WCH_MAX; c++) {
        int sum = 0;
        for (int k = c - 2; k <= c + 2; k++) {
            if (k < WCH_MIN || k > WCH_MAX) continue;
            sum += r.ap_per_ch[k];
        }
        r.load[c] = (sum > 255) ? 255 : (uint8_t)sum;
    }

    // 推荐：先在 1/6/11（互不重叠的三个）里挑，再用全 13 信道严格更优者覆盖。
    // 用严格 '<' 保证并列时保留先选中的那个（1/6/11 优先、再按信道号从小到大）。
    static const uint8_t prefer[3] = { 1, 6, 11 };
    for (int p = 0; p < 3; p++) {
        uint8_t c = prefer[p];
        if (r.best_1611 == 0 || r.ap_per_ch[c] < r.ap_per_ch[r.best_1611]) r.best_1611 = c;
        if (r.best_ch   == 0 || r.load[c]      < r.load[r.best_ch])        r.best_ch   = c;
    }
    for (uint8_t c = WCH_MIN; c <= WCH_MAX; c++) {
        if (r.load[c] < r.load[r.best_ch]) r.best_ch = c;
    }

    if (xQueueSend(wifiChanScanResQueue, &r, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[WiFi_Task] chan-scan result queue full, dropped");
    }
}

void WiFi_Task(void* pvParameters) {
    (void)pvParameters;
    Serial.println("[WiFi_Task] Starting...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    WifiConfigMsg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, g_wifiBootCfg.ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, g_wifiBootCfg.password, sizeof(cfg.password) - 1);

    WiFi.mode(WIFI_STA);
    if (cfg.ssid[0] != '\0') {
        Serial.printf("[WiFi_Task] Connecting to: %s\n", cfg.ssid);
        WiFi.begin(cfg.ssid, cfg.password);
    } else {
        Serial.println("[WiFi_Task] No config, staying idle until UI provides one");
    }

    bool hasConfig     = (cfg.ssid[0] != '\0');
    bool wasConnected  = false;
    unsigned long lastReconnect = 0;

    while (1) {
        // 接收来自 UI 的新配置
        WifiConfigMsg_t incoming;
        if (xQueueReceive(wifiConfigQueue, &incoming, 0) == pdTRUE) {
            Serial.printf("[WiFi_Task] User override: SSID=%s\n", incoming.ssid);
            WiFi.disconnect(true);
            delay(50);
            WiFi.begin(incoming.ssid, incoming.password);
            hasConfig    = true;
            wasConnected = false;
            lastReconnect = millis();   
        }

        // 扫描请求
        ScanRequestMsg_t scanReq;
        if (xQueueReceive(wifiScanRequestQueue, &scanReq, 0) == pdTRUE) {
            Serial.println("[WiFi_Task] Scanning...");

            ScanResultMsg_t result;                 // 1654B，任务栈 12KB 够用
            memset(&result, 0, sizeof(result));     // 必须整块清零，否则 count<50 时后面是栈垃圾
            int n = WiFi.scanNetworks();
            if (n < 0) {
                Serial.printf("[WiFi_Task] scanNetworks failed: %d\n", n);
                n = 0;
            }
            if (n > APP_SCAN_MAX_AP) n = APP_SCAN_MAX_AP;

            result.count = n;
            for (int i = 0; i < n; i++) {
                strncpy(result.ssids[i], WiFi.SSID(i).c_str(), APP_SSID_MAX_LEN - 1);
                result.ssids[i][APP_SSID_MAX_LEN - 1] = '\0';
            }
            WiFi.scanDelete();

            if (xQueueSend(wifiScanResultQueue, &result, pdMS_TO_TICKS(1000)) != pdTRUE) {
                Serial.println("[WiFi_Task] result queue full, scan dropped");
            }
            Serial.printf("[WiFi_Task] Scan complete, %d networks\n", n);
        }

        // 信道扫描请求（信道占用分析工具）
        ChanScanReq_t chanReq;
        if (xQueueReceive(wifiChanScanReqQueue, &chanReq, 0) == pdTRUE) {
            wifi_run_channel_scan();
        }

        // 维护连接状态
        bool isConnected = (WiFi.status() == WL_CONNECTED);
        if (isConnected && !wasConnected) {
            Serial.printf("[WiFi_Task] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
        }
        wasConnected = isConnected;

        if (!isConnected && hasConfig) {
            // reconnect() 会阻塞 2s 以上，放慢到 10s 一次，
            // 避免反复打断 core1 上的 DHT11 读取（读一次要关中断 1~2s）
            if (millis() - lastReconnect > 10000UL) {
                Serial.println("[WiFi_Task] Reconnecting...");
                WiFi.reconnect();
                lastReconnect = millis();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("FreeRTOS System Start");
    Serial.println("========================================");

    // 锁已在静态初始化阶段建好（见文件顶部的 SdMutexInit）
    if (!g_sdMutex) Serial.println("FATAL: SD mutex missing!");

    wifiConfigQueue      = xQueueCreate(2, sizeof(WifiConfigMsg_t));
    wifiScanRequestQueue = xQueueCreate(1, sizeof(ScanRequestMsg_t));
    wifiScanResultQueue  = xQueueCreate(1, sizeof(ScanResultMsg_t));
    wifiChanScanReqQueue = xQueueCreate(1, sizeof(ChanScanReq_t));
    wifiChanScanResQueue = xQueueCreate(1, sizeof(ChanScanResult_t));
    serialCmdQueue       = xQueueCreate(4, sizeof(SerialCmdMsg_t));

    if (!wifiConfigQueue || !wifiScanRequestQueue || !wifiChanScanReqQueue ||
        !wifiScanResultQueue || !wifiChanScanResQueue || !serialCmdQueue) {
        Serial.println("FATAL: Failed to create queues!");
    }

    // 挂 SD 并读取 /wifi.cfg（必须在 WiFi_Task 起来之前完成）
    bool sd_mounted = storage_mount();
    memset(&g_wifiBootCfg, 0, sizeof(g_wifiBootCfg));
    if (sd_mounted && wifi_load_config_from_sd(g_wifiBootCfg.ssid, g_wifiBootCfg.password,
                                               sizeof(g_wifiBootCfg.ssid),
                                               sizeof(g_wifiBootCfg.password))) {
        Serial.printf("WiFi config loaded from SD: %s\n", g_wifiBootCfg.ssid);
    } else {
        strncpy(g_wifiBootCfg.ssid, FIXED_SSID, sizeof(g_wifiBootCfg.ssid) - 1);
        strncpy(g_wifiBootCfg.password, FIXED_PASSWORD, sizeof(g_wifiBootCfg.password) - 1);
        Serial.printf("Using built-in WiFi fallback: %s\n", g_wifiBootCfg.ssid);
    }

    // 扫描结果的 1654B 结构体要入队，信道扫描结果另有 1202B（两者不会同时活跃在
    // 同一栈帧里：信道扫描抽成了独立函数 wifi_run_channel_scan），
    // 所以 WiFi 任务栈给 12288
    xTaskCreatePinnedToCore(LVGL_Task, "LVGL", 16010, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(WiFi_Task, "WiFi", 12288, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(SD_Task,   "SD",    4096, NULL, 1, NULL, 1);
#if ENABLE_BLE
    // BLE 协议栈初始化开销大（几十 KB 堆），放在任务里做，别拖慢 setup()
    xTaskCreatePinnedToCore(BLE_Task,  "BLE",   8192, NULL, 2, NULL, 1);
#endif

    Serial.println("All tasks created");
}

void loop() {
    // 串口命令统一交给 LVGL 任务执行
    if (Serial.available() > 0) {
        SerialCmdMsg_t msg;
        String rx = Serial.readStringUntil('\n');
        rx.trim();
        if (rx.length() > 0 && serialCmdQueue) {
            strncpy(msg.line, rx.c_str(), sizeof(msg.line) - 1);
            msg.line[sizeof(msg.line) - 1] = '\0';
            if (xQueueSend(serialCmdQueue, &msg, 0) != pdTRUE) {
                Serial.println("[loop] cmd queue full, dropped");
            }
        }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
}
