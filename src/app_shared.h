#pragma once

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// SD_LOCK 需要 g_spiBusMutex 的声明（TFT 与 SD 共用 HSPI，锁由 display_port 拥有）
#include "display_port.h"

#define APP_SSID_MAX_LEN 33      // 32 + '\0'
#define APP_PWD_MAX_LEN  65      // 64 + '\0'
#define APP_SCAN_MAX_AP  50

// 消息结构必须是 POD，跨任务按值拷贝
typedef struct {
    char ssid[APP_SSID_MAX_LEN];
    char password[APP_PWD_MAX_LEN];
} WifiConfigMsg_t;

typedef struct {
    bool request;
} ScanRequestMsg_t;

typedef struct {
    int  count;
    char ssids[APP_SCAN_MAX_AP][APP_SSID_MAX_LEN];
} ScanResultMsg_t;              // sizeof ≈ 1654B，入队时注意任务栈

typedef struct {
    char line[96];
} SerialCmdMsg_t;

#define BLE_RX_RING_SIZE 1024                       // 必须是 2 的幂，便于掩码取模
extern volatile uint8_t  g_bleRxRing[BLE_RX_RING_SIZE];
extern volatile uint16_t g_bleRxHead;               // 只在 BLE 回调线程写
extern volatile uint16_t g_bleRxTail;               // 只在 LVGL 任务写

// 必须用临界区而非互斥量：写者是优先级 19 的 BTC 任务，
// 在高优先级协议栈线程里阻塞于互斥量会触发优先级继承断言（直接 panic）。
extern portMUX_TYPE g_bleRxMux;

// BLE 连接状态：回调线程写，LVGL 任务只读
extern volatile bool     g_bleConnected;
extern volatile uint32_t g_bleRxTotal;
extern volatile bool     g_bleRxPending;            // core1 置位，等 core0 刷界面
extern char              g_blePeerAddr[20];

void   bleuart_init(void);                 // BLE 任务里调用一次
void   bleuart_pump(void);                 // core1：环形缓冲 -> 显示缓冲
void   bleuart_ui_service(void);           // core0：刷新 LVGL 控件
bool   bleuart_is_connected(void);
bool   bleuart_is_ready(void);
size_t bleuart_send(const char* data, size_t len);

extern QueueHandle_t wifiConfigQueue;        // UI(core0)  -> WiFi(core1)
extern QueueHandle_t wifiScanRequestQueue;   // UI(core0)  -> WiFi(core1)
extern QueueHandle_t wifiScanResultQueue;    // WiFi(core1)-> UI(core0)
extern QueueHandle_t serialCmdQueue;         // loop(core1)-> UI(core0)

extern WifiConfigMsg_t   g_wifiBootCfg;      // 开机配置（SD 加载或内置兜底）
extern SemaphoreHandle_t g_sdMutex;          // SD 文件系统递归锁

// 一次拿两把锁：顺序永远是 SD -> SPI 总线，不可反过来（否则可能环路死锁）
#define SD_LOCK()                                                                      \
    do {                                                                               \
        if (g_sdMutex) xSemaphoreTakeRecursive(g_sdMutex, portMAX_DELAY);              \
        if (g_spiBusMutex) xSemaphoreTakeRecursive(g_spiBusMutex, portMAX_DELAY);      \
    } while (0)

#define SD_UNLOCK()                                                                    \
    do {                                                                               \
        if (g_spiBusMutex) xSemaphoreGiveRecursive(g_spiBusMutex);                     \
        if (g_sdMutex) xSemaphoreGiveRecursive(g_sdMutex);                             \
    } while (0)
