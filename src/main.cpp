#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <WiFi.h>

#include "app_shared.h"

extern void original_setup();
extern void original_loop();

// ---------------------------------------------------------------------------
// 内置兜底 WiFi（只有在 SD 卡里没有 /wifi.cfg 时才使用）
// 注意：这是明文凭据，正式产品请删掉或用 nvs 保存
// ---------------------------------------------------------------------------
#define FIXED_SSID     "ChinaNet-qMyM"
#define FIXED_PASSWORD "4r9mec6e"

// ---------------------------------------------------------------------------
// 全局对象（app_shared.h 里 extern 声明）
// ---------------------------------------------------------------------------
QueueHandle_t wifiConfigQueue      = NULL;
QueueHandle_t wifiScanRequestQueue = NULL;
QueueHandle_t wifiScanResultQueue  = NULL;
QueueHandle_t serialCmdQueue       = NULL;

WifiConfigMsg_t   g_wifiBootCfg;
SemaphoreHandle_t g_sdMutex = NULL;
SemaphoreHandle_t g_spiBusMutex = NULL;      // TFT 与 SD 共用 SPI 总线的互斥

// 在 setup() 之前就把锁建好：这样 storage_mount() 无论从哪里被调用都一定受保护
// （以前靠 SD_LOCK() 里的 NULL 判断，一旦顺序错了就会静默地无锁访问）
namespace { struct SdMutexInit { SdMutexInit() { g_sdMutex = xSemaphoreCreateRecursiveMutex(); } } sdMutexInit; }

// SPI 总线锁：TFT 与 SD 卡共用 SCLK/MOSI/MISO（12/11/13），必须串行化。
// 用递归锁是因为 SD 路径上存在"总线锁里再进 SD 锁"的嵌套。
namespace { struct SpiBusMutexInit {
    SpiBusMutexInit() { g_spiBusMutex = xSemaphoreCreateRecursiveMutex(); }
} spiBusMutexInit; }

// --- BLE ---------------------------------------------------------------------
// BLE 的全局量（环形缓冲、状态、portMUX 临界区锁）全部定义在 src/ble_uart.cpp，
// 它们的锁都是静态初始化的 portMUX_TYPE，不需要在这里建任何东西。
// 这里曾经放过一份重复定义 + 一个互斥量，前者导致 multiple definition，
// 后者会在高优先级的协议栈线程里触发 priority-inheritance 断言，都已移除。

// ---------------------------------------------------------------------------
// 由 LVGL_Demos.cpp 提供的 SD 挂载 / 配置读写（内部已经带 SD_LOCK）
// ---------------------------------------------------------------------------
extern bool  storage_mount();                                     // 挂载 SD（可重复调用）
extern bool  wifi_load_config_from_sd(char* ssid, char* pwd, size_t ssid_size, size_t pwd_size);

// ---------------------------------------------------------------------------
// 任务
// ---------------------------------------------------------------------------
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
// BLE 只放在 core1，且和 WiFi 任务错开优先级。
// 注意：BLE 和 WiFi 共用同一个射频，同时大流量跑会互相抢时间片（正常现象）。
void BLE_Task(void* pvParameters) {
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(500));     // 让串口先打印完启动日志
    bleuart_init();

    while (1) {
        bleuart_pump();                 // 环形缓冲 -> 显示缓冲（不碰 LVGL）
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

// 说明：LVGL 任务固定在 core0，本任务固定在 core1（board JSON 里
// ARDUINO_RUNNING_CORE=1，所以 core1 只有一个 Arduino loop 任务和本任务）。
// 所有 SD 访问都必须走 SD_LOCK()，否则 core0/core1 会同时操作 HSPI。
void WiFi_Task(void* pvParameters) {
    (void)pvParameters;
    Serial.println("[WiFi_Task] Starting...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // g_wifiBootCfg 在 setup() 里已由 SD 的 /wifi.cfg 或内置兜底填好
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
        // --- 接收来自 UI 的新配置 ---
        WifiConfigMsg_t incoming;
        if (xQueueReceive(wifiConfigQueue, &incoming, 0) == pdTRUE) {
            Serial.printf("[WiFi_Task] User override: SSID=%s\n", incoming.ssid);
            WiFi.disconnect(true);
            delay(50);
            WiFi.begin(incoming.ssid, incoming.password);
            hasConfig    = true;
            wasConnected = false;
            lastReconnect = millis();     // 避免刚下发就又触发 reconnect
        }

        // --- 扫描请求 ---
        ScanRequestMsg_t scanReq;
        if (xQueueReceive(wifiScanRequestQueue, &scanReq, 0) == pdTRUE) {
            Serial.println("[WiFi_Task] Scanning...");

            ScanResultMsg_t result;                 // 1654B，任务栈 12KB 够用
            memset(&result, 0, sizeof(result));     // 关键：先把整块清零，
                                                    // 否则 count<50 时后面是栈垃圾
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

            // 结果队列深度 1：正常情况下 UI 侧已经排空，这里再等 1s 足够
            if (xQueueSend(wifiScanResultQueue, &result, pdMS_TO_TICKS(1000)) != pdTRUE) {
                Serial.println("[WiFi_Task] result queue full, scan dropped");
            }
            Serial.printf("[WiFi_Task] Scan complete, %d networks\n", n);
        }

        // --- 维护连接状态 ---
        bool isConnected = (WiFi.status() == WL_CONNECTED);
        if (isConnected && !wasConnected) {
            Serial.printf("[WiFi_Task] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
        }
        wasConnected = isConnected;

        if (!isConnected && hasConfig) {
            // WiFi.reconnect() 会阻塞 2s 以上，因此放慢到 10s 一次，
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

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("FreeRTOS System Start");
    Serial.println("========================================");

    // 1) 锁已经在静态初始化阶段建好（见文件顶部的 SdMutexInit）
    if (!g_sdMutex) Serial.println("FATAL: SD mutex missing!");

    // 2) 建队列
    wifiConfigQueue      = xQueueCreate(2, sizeof(WifiConfigMsg_t));
    wifiScanRequestQueue = xQueueCreate(1, sizeof(ScanRequestMsg_t));
    wifiScanResultQueue  = xQueueCreate(1, sizeof(ScanResultMsg_t));
    serialCmdQueue       = xQueueCreate(4, sizeof(SerialCmdMsg_t));

    if (!wifiConfigQueue || !wifiScanRequestQueue ||
        !wifiScanResultQueue || !serialCmdQueue) {
        Serial.println("FATAL: Failed to create queues!");
    }

    // 3) 挂 SD 并读取 /wifi.cfg（必须在 WiFi_Task 起来之前完成）
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

    // 4) 建任务（扫描结果的 1654B 结构体入队 → WiFi 任务栈从 8192 提到 12288）
    xTaskCreatePinnedToCore(LVGL_Task, "LVGL", 16010, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(WiFi_Task, "WiFi", 12288, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(SD_Task,   "SD",    4096, NULL, 1, NULL, 1);
#if ENABLE_BLE
    // BLE 的控制器/BT 栈初始化开销大（几十 KB 堆），放在任务里做，
    // 别拖慢 setup()。优先级给 2，和 WiFi 同级。
    xTaskCreatePinnedToCore(BLE_Task,  "BLE",   8192, NULL, 2, NULL, 1);
#endif

    Serial.println("All tasks created");
}

void loop() {
    // 串口命令统一交给 LVGL 任务执行（它才拥有 LVGL 对象）
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
