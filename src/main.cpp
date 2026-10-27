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
            lastReconnect = millis();     // 避免刚下发就又触发 reconnect
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
    serialCmdQueue       = xQueueCreate(4, sizeof(SerialCmdMsg_t));

    if (!wifiConfigQueue || !wifiScanRequestQueue ||
        !wifiScanResultQueue || !serialCmdQueue) {
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

    // 扫描结果的 1654B 结构体要入队，所以 WiFi 任务栈给 12288
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
