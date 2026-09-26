// ============================================================================
//  app_shared.h - 跨任务共享的“消息/队列/锁”契约
//  ---------------------------------------------------------------------------
//  这个头文件只放“按值拷贝的小数据结构 + freeRTOS 队列句柄声明 + 一把
//  全局递归互斥锁”，目的是：
//    1) 消灭 main.cpp 与 LVGL_Demos.cpp 里各写一份的 WifiConfigMsg_t /
//       ScanRequestMsg_t / ScanResultMsg_t（以前字段必须手工同步，极易踩坑）
//    2) 给 SD 卡一把跨核互斥锁（SD 走 HSPI 12/13/11 + CS4，LVGL 任务在 core0、
//       WiFi 任务在 core1，以前两边同时发 SPI 事务 → 文件系统可能被写坏）
//    3) 让 core1 的串口命令通过队列投递给 core0 的 LVGL 任务，避免跨核直接
//       调用 lv_label_set_text()
//
//  注意：main.cpp 必须先 #include <Arduino.h> 再包含本文件（用到 FreeRTOS 类型）
// ============================================================================

#pragma once

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ---------------------------------------------------------------------------
// 队列长度 / 字符串容量
// ---------------------------------------------------------------------------
#define APP_SSID_MAX_LEN 33      // WPA2 SSID 最长 32 + '\0'
#define APP_PWD_MAX_LEN  65      // 最长 64 + '\0'
#define APP_SCAN_MAX_AP  50      // 单次扫描最多展示的 AP 数

// ---------------------------------------------------------------------------
// 消息结构（必须是 POD，跨任务按值拷贝）
// ---------------------------------------------------------------------------
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
} ScanResultMsg_t;              // sizeof ≈ 1654B（原 50*64 版本是 3208B，容易压爆任务栈）

typedef struct {
    char line[96];              // 串口命令行
} SerialCmdMsg_t;

// ---------------------------------------------------------------------------
// BLE 接收缓冲（生产者 = BLE 协议栈回调线程，消费者 = LVGL 任务 core0）
// 用环形缓冲而不是队列：BLE 一次 notify 可能来 20~244 字节，按字节流处理最省事，
// 而且不会因为 UI 没来得及取就把数据丢掉。
// ---------------------------------------------------------------------------
#define BLE_RX_RING_SIZE 1024                       // 必须是 2 的幂，便于用掩码取模
extern volatile uint8_t  g_bleRxRing[BLE_RX_RING_SIZE];
extern volatile uint16_t g_bleRxHead;               // 只在 BLE 回调线程写
extern volatile uint16_t g_bleRxTail;               // 只在 LVGL 任务写

// 保护 head/tail 读写配对。
// 【为什么必须是临界区而不是互斥量】
//   g_bleRxRing 的写者是 Bluedroid 的 BTC 任务（优先级 19），
//   读者是 LVGL 任务（优先级 3）。FreeRTOS 的互斥量不支持“高优先级任务
//   等待低优先级持有者”以外的情形——BTC 线程阻塞在互斥量上会触发
//   xTaskPriorityDisinherit 断言，直接在协议栈里 panic。
//   这里全是几微秒的内存拷贝，用临界区最合适。
extern portMUX_TYPE g_bleRxMux;

// BLE 连接状态：由回调线程写，LVGL 任务只读
extern volatile bool     g_bleConnected;
extern volatile uint32_t g_bleRxTotal;              // 累计收到的字节数
extern volatile bool     g_bleRxPending;            // core1 说有新数据，等 core0 刷界面
extern char              g_blePeerAddr[20];         // 对端 MAC 字符串

// ---------------------------------------------------------------------------
// BLE UART 对外接口（实现在 src/ble_uart.cpp）
// 这里不加 extern "C"：包含本头文件的只有 C++ 编译单元（main.cpp /
// LVGL_Demos.cpp / ble_uart.cpp），链接名一致即可。
// ui_ble.c 用到的两个回调单独声明在 ui_test/screens/ui_ble.h 里。
// ---------------------------------------------------------------------------
void   bleuart_init(void);                 // 在 BLE 任务里调用一次
void   bleuart_pump(void);                 // core1：环形缓冲 -> 显示缓冲
void   bleuart_ui_service(void);           // core0：刷新 LVGL 控件
bool   bleuart_is_connected(void);
bool   bleuart_is_ready(void);
size_t bleuart_send(const char* data, size_t len);

// ---------------------------------------------------------------------------
// 全局对象：定义在 main.cpp
// ---------------------------------------------------------------------------
extern QueueHandle_t wifiConfigQueue;        // UI(core0)  -> WiFi(core1)
extern QueueHandle_t wifiScanRequestQueue;   // UI(core0)  -> WiFi(core1)
extern QueueHandle_t wifiScanResultQueue;    // WiFi(core1)-> UI(core0)
extern QueueHandle_t serialCmdQueue;         // loop(core1)-> UI(core0)

extern WifiConfigMsg_t   g_wifiBootCfg;      // 开机预置配置（由 SD 加载或内置兜底）
extern SemaphoreHandle_t g_sdMutex;          // SD 卡总线/文件系统递归锁

// ---------------------------------------------------------------------------
// SPI 总线互斥（TFT 与 SD 卡共用同一组引脚！）
// ---------------------------------------------------------------------------
//  硬件事实（见 lib/TFT_eSPI/User_Setup.h 与本文件 SD 的初始化）：
//      TFT：SCLK=12  MOSI=11  MISO=13  CS=10   ← TFT_eSPI 内部 SPIClass(HSPI)
//      SD ：SCLK=12  MOSI=11  MISO=13  CS=4    ← 另一个 SPIClass(HSPI)
//  两个设备共用同一个 HSPI 主机，只有 CS 不同；而代码里是两份独立的
//  SPIClass 对象，Arduino 的 SPI 库不会替它们互斥。
//  所以：core0 的 lvgl_flush_cb() 和 core1 的 SD 操作会同时写同一组
//  SPI 寄存器 → 屏幕花屏。SD_LOCK() 只能挡住 SD↔SD，挡不住 TFT↔SD，
//  因此这里再加一把"总线级"的锁，两个方向都要拿。
extern SemaphoreHandle_t g_spiBusMutex;

#define SPI_BUS_LOCK()                                                                 \
    do {                                                                               \
        if (g_spiBusMutex) xSemaphoreTakeRecursive(g_spiBusMutex, portMAX_DELAY);      \
    } while (0)

#define SPI_BUS_UNLOCK()                                                               \
    do {                                                                               \
        if (g_spiBusMutex) xSemaphoreGiveRecursive(g_spiBusMutex);                     \
    } while (0)

// ---------------------------------------------------------------------------
// SD 锁：一次拿两把（顺序永远是 SD → SPI 总线，绝不可反过来）
//   g_sdMutex     : 保护 SD 文件系统本身（多个 SD 操作之间串行）
//   g_spiBusMutex : 保护 HSPI 硬件（和 TFT 刷新互斥）
// 因为 lvgl_flush_cb() 只拿总线锁、不拿 SD 锁，所以不存在环路死锁。
// ---------------------------------------------------------------------------
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
