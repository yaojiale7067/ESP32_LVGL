#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <WiFi.h>

extern void original_setup();
extern void original_loop();

#define FIXED_SSID     "ChinaNet-qMyM"
#define FIXED_PASSWORD "4r9mec6e"

QueueHandle_t wifiConfigQueue = NULL;
QueueHandle_t wifiScanRequestQueue = NULL;
QueueHandle_t wifiScanResultQueue = NULL;

typedef struct {
    char ssid[64];
    char password[64];
} WifiConfigMsg_t;

typedef struct {
    bool request;
} ScanRequestMsg_t;

typedef struct {
    int count;
    char ssids[50][64];
} ScanResultMsg_t;

void LVGL_Task(void *pvParameters) {
    original_setup();
    while (1) {
        original_loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SD_Task(void *pvParameters) {
    Serial.println("[SD_Task] Running (idle)");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

void WiFi_Task(void *pvParameters) {
    Serial.println("[WiFi_Task] Starting...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    Serial.printf("[WiFi_Task] Connecting to fixed SSID: %s\n", FIXED_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(FIXED_SSID, FIXED_PASSWORD);

    WifiConfigMsg_t cfg;
    bool hasConfig = true;   // 已经启动了连接
    bool wasConnected = false;

    while (1) {
        // 接收来自 UI 的新配置（如果用户通过界面连接了其他网络，这里会覆盖）
        if (xQueueReceive(wifiConfigQueue, &cfg, pdMS_TO_TICKS(100)) == pdTRUE) {
            Serial.printf("[WiFi_Task] User overrides with new config: SSID=%s\n", cfg.ssid);
            WiFi.disconnect(true);
            WiFi.begin(cfg.ssid, cfg.password);
            hasConfig = true;
            wasConnected = false;
        }

        // 处理扫描请求（与之前相同）
        ScanRequestMsg_t scanReq;
        if (xQueueReceive(wifiScanRequestQueue, &scanReq, pdMS_TO_TICKS(0)) == pdTRUE) {
            Serial.println("[WiFi_Task] Scanning...");
            int n = WiFi.scanNetworks();
            if (n < 0) n = 0;
            if (n > 50) n = 50;
            ScanResultMsg_t result;
            result.count = n;
            for (int i = 0; i < n; i++) {
                strncpy(result.ssids[i], WiFi.SSID(i).c_str(), 63);
                result.ssids[i][63] = '\0';
            }
            xQueueSend(wifiScanResultQueue, &result, pdMS_TO_TICKS(100));
            Serial.printf("[WiFi_Task] Scan complete, %d networks\n", n);
        }

        // 维护连接状态
        bool isConnected = (WiFi.status() == WL_CONNECTED);
        if (isConnected && !wasConnected) {
            Serial.println("[WiFi_Task] Connected to WiFi");
        }
        wasConnected = isConnected;
        if (!isConnected && hasConfig) {
            static unsigned long lastReconnect = 0;
            if (millis() - lastReconnect > 5000) {
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
    Serial.println("FreeRTOS System with WiFi Task");
    Serial.println("========================================");

    wifiConfigQueue = xQueueCreate(1, sizeof(WifiConfigMsg_t));
    wifiScanRequestQueue = xQueueCreate(1, sizeof(ScanRequestMsg_t));
    wifiScanResultQueue = xQueueCreate(1, sizeof(ScanResultMsg_t));

    if (!wifiConfigQueue || !wifiScanRequestQueue || !wifiScanResultQueue) {
        Serial.println("Failed to create queues!");
    }

    xTaskCreatePinnedToCore(LVGL_Task, "LVGL", 16010, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(WiFi_Task, "WiFi", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(SD_Task, "SD", 4096, NULL, 1, NULL, 1);

    Serial.println("All tasks created");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}