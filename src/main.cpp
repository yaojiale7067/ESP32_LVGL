#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <WiFi.h>

// 声明原有函数
extern void original_setup();
extern void original_loop();

// 队列句柄（定义在 LVGL_Demos.cpp 中）
extern QueueHandle_t ntpRequestQueue;
extern QueueHandle_t wifiConfigQueue;

// 消息结构
typedef struct {
    char ssid[64];
    char password[64];
} WifiConfigMsg_t;

typedef struct {
    bool request;
} NTPRequestMsg_t;

// LVGL 任务
void LVGL_Task(void *pvParameters) {
    original_setup();
    while (1) {
        original_loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// SD 卡后台任务（占位）
void SD_Task(void *pvParameters) {
    Serial.println("[SD_Task] Running (idle)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// WiFi 后台任务（负责执行连接和处理 NTP 请求）
void WiFi_Task(void *pvParameters) {
    Serial.println("[WiFi_Task] Starting...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    WifiConfigMsg_t cfg;
    bool hasConfig = false;

    // 配置 NTP（中国时区 UTC+8）
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    while (1) {
        // 尝试从队列接收新的 WiFi 配置
        if (xQueueReceive(wifiConfigQueue, &cfg, pdMS_TO_TICKS(100)) == pdTRUE) {
            Serial.printf("[WiFi_Task] Received config: SSID=%s\n", cfg.ssid);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.begin(cfg.ssid, cfg.password);
            hasConfig = true;
        }

        // 维持连接
        if (hasConfig && WiFi.status() != WL_CONNECTED) {
            static unsigned long lastReconnect = 0;
            if (millis() - lastReconnect > 5000) {
                Serial.println("[WiFi_Task] WiFi disconnected, reconnecting...");
                WiFi.reconnect();
                lastReconnect = millis();
            }
        }

        // 处理 NTP 请求
        NTPRequestMsg_t ntpMsg;
        if (xQueueReceive(ntpRequestQueue, &ntpMsg, pdMS_TO_TICKS(0)) == pdTRUE) {
            if (WiFi.status() == WL_CONNECTED) {
                struct tm timeinfo;
                if (getLocalTime(&timeinfo, 5000)) {
                    char timeStr[64];
                    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    Serial.printf("Current time: %s\n", timeStr);
                } else {
                    Serial.println("Failed to obtain NTP time.");
                }
            } else {
                Serial.println("WiFi not connected, cannot get NTP time.");
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

    // 创建队列
    ntpRequestQueue = xQueueCreate(1, sizeof(NTPRequestMsg_t));
    wifiConfigQueue = xQueueCreate(1, sizeof(WifiConfigMsg_t));
    if (ntpRequestQueue == NULL || wifiConfigQueue == NULL) {
        Serial.println("Failed to create queues!");
    }

    // 创建 LVGL 任务（核心 0）
    xTaskCreatePinnedToCore(LVGL_Task, "LVGL", 16010, NULL, 3, NULL, 0);
    // 创建 WiFi 任务（核心 1）
    xTaskCreatePinnedToCore(WiFi_Task, "WiFi", 8192, NULL, 2, NULL, 1);
    // 创建 SD 后台任务（核心 1）
    xTaskCreatePinnedToCore(SD_Task, "SD", 4096, NULL, 1, NULL, 1);

    Serial.println("All tasks created");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}