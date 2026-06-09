#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 声明原有函数
extern void original_setup();
extern void original_loop();
extern void SD_Task(void *pvParameters);   // SD 卡后台任务

// LVGL 任务（负责所有 UI + SD 卡操作）
void LVGL_Task(void *pvParameters) {
    original_setup();                      // 执行原有 setup
    while (1) {
        original_loop();                   // 执行原有 loop
        vTaskDelay(pdMS_TO_TICKS(5));     // 保持原 loop 中的 delay 节奏
    }
} 

// SD 卡后台任务（仅占位，不实际访问 SPI，避免冲突）
void SD_Task(void *pvParameters) {
    Serial.println("[SD_Task] Running (idle)");
    while (1) {
        // 这里可以放一些不涉及 SPI 的后台工作（例如处理队列消息）
        // 如果你的应用需要独立写日志，请通过队列发送给 LVGL_Task 执行随便写的freertos lvgl 测试，可以读取SD卡
        //
    
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("FreeRTOS System");
    Serial.println("========================================");

    // 创建 LVGL 任务（核心 0，优先级 3）
    xTaskCreatePinnedToCore(LVGL_Task, "LVGL", 16010, NULL, 3, NULL, 0);

    // 创建 SD 后台任务（核心 1，优先级 1）
    xTaskCreatePinnedToCore(SD_Task, "SD", 4096, NULL, 1, NULL, 1);

    Serial.println("Tasks created");
}

void loop() {
    // 空闲，所有工作由任务完成
    vTaskDelay(pdMS_TO_TICKS(1000));
}