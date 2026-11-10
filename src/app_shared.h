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

// ============================================================================
//  WiFi 信道扫描（2.4GHz 信道占用分析）
//  ----------------------------------------------------------------------------
//  和上面的"网络列表扫描"是两件事，所以走独立的队列、独立的结果结构：
//    · 列表扫描只关心 SSID（show_hidden=false，找的是能连的名字）；
//    · 信道扫描关心"哪个信道干净"：隐藏 SSID 的 AP 一样占频谱，必须算进去，
//      而且要按信道聚合成占用图。
//  2.4GHz 只有 1..13（ESP32 在 CN 区不用 14），20MHz 带宽实际横跨 5 个信道，
//  所以"信道 n 有多脏"看的是 n-2..n+2 范围内的 AP 总数（load），不是只看 n。
// ============================================================================
#define APP_CSCAN_MAX_AP  12            // 详情表最多保留多少个 AP（按 RSSI 取最强的）
#define WCH_MIN           1
#define WCH_MAX           13
#define WCH_SLOTS         (WCH_MAX + 1) // 按信道号当下标：[0] 不用，[1..13] 有效

typedef struct {
    char    ssid[APP_SSID_MAX_LEN];     // 33
    int8_t  rssi;
    uint8_t channel;
    uint8_t enc;                        // wifi_auth_mode_t
} ChanScanAp_t;                         // 36B：全是 char/uint8，没有填充

// 只带一个 request 标志：永远全信道扫。
// 不做"只扫某一个信道"：那样 ap_per_ch[] 里其余 12 格只能置 0 或沿用旧值，
// 前者会把占用图画成"只有一个信道有东西"，后者是悄悄过期 —— 两种都比重扫一次更糟。
// 全扫在 active 300ms/信道下约 4s，和单信道扫（0.3s）的差距换不来正确性。
typedef struct {
    bool    request;
} ChanScanReq_t;

typedef struct {
    int16_t      scan_ret;              // WiFi.scanNetworks() 原样返回，<0 表示失败
    uint16_t     count;                 // aps[] 里的有效条数（可能被 MAX_AP 截断）
    uint8_t      ap_per_ch[WCH_SLOTS];  // 每信道 AP 数（精确值，不受 MAX_AP 截断影响）
    int8_t       best_rssi[WCH_SLOTS];  // 每信道最强 RSSI，空信道为 -127
    uint8_t      load[WCH_SLOTS];       // 干扰负载 = ch-2..ch+2 的 AP 数之和
    uint8_t      best_ch;               // 推荐信道（load 最小，并列偏向 1/6/11）
    uint8_t      best_1611;             // 1/6/11 三个非重叠信道里最干净的
    ChanScanAp_t aps[APP_CSCAN_MAX_AP];
} ChanScanResult_t;                     // sizeof ≈ 1202B

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
extern QueueHandle_t wifiChanScanReqQueue;   // UI(core0)  -> WiFi(core1)  信道扫描请求
extern QueueHandle_t wifiChanScanResQueue;   // WiFi(core1)-> UI(core0)  信道扫描结果
extern QueueHandle_t serialCmdQueue;         // loop(core1)-> UI(core0)

extern WifiConfigMsg_t   g_wifiBootCfg;      // 开机配置（SD 加载或内置兜底）
extern SemaphoreHandle_t g_sdMutex;          // SD 文件系统递归锁

// ---- 802.11 被动监听（rf_monitor.cpp）与 WiFi_Task 之间的协作标志 ----
// g_rfmonActive：监听期间为 true。WiFi_Task 必须据此停下来 ——
//   · 不做 reconnect（会和跳频抢信道）
//   · 不受理扫描请求（scanNetworks 会打断跳频，且拖十秒以上）
// g_wifiForceReconnect：监听结束时置位，让 WiFi_Task 跳过 10s 退避立刻重连。
extern volatile bool g_rfmonActive;
extern volatile bool g_wifiForceReconnect;

// ---- WiFi / BLE 初始化顺序协作 ----
// esp_wifi_init 需要 4 块连续的静态 RX 缓冲（内部 DMA RAM），
// Bluedroid 协议栈一次要吃几十 KB 内部堆。谁先起决定成败：
// BLE 先起 -> WiFi 初始化报 "Expected to init 4 rx buffer, actual is 3"（NO_MEM 257）
// -> begin 失败 -> 永远 Reconnecting。所以 BLE_Task 要等这个标志。
extern volatile bool g_wifiInitDone;

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
