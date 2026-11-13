// ============================================================================
//  rf_monitor.h - 802.11 管理帧被动监听（只收不发）
//
//  定位：这是"诊断"工具，不是"攻击"工具。
//  它把 WiFi 调到混杂模式，只接收管理帧（beacon / probe request /
//  deauth / disassoc），统计并展示：
//    · 谁在发 deauth/disassoc（源 MAC）—— 回答"我家网络为什么老掉线"
//    · 周围有哪些 AP（SSID / 信道 / RSSI / 厂商 OUI / 信标数）
//    · 活跃设备的 probe request 数量趋势
//
//  ★ 代码里没有任何发送路径：不调用 esp_wifi_80211_tx，不伪造帧，
//    不发送 deauth，不广播 SSID。射频上本设备全程静默（除了 STA 正常关联）。
// ============================================================================

#ifndef RF_MONITOR_H
#define RF_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RFMON_MAX_AP          16    // 记录多少个 AP
#define RFMON_MAX_DEAUTH_SRC   8    // 记录多少个 deauth 源
#define RFMON_NAME_MAX        48    // UI 一行主文本的最大长度

typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ch;
    int8_t   rssi;
    uint16_t beacons;
} rfmon_ap_t;

typedef struct {
    uint8_t  bssid[6];
    uint16_t frames;                // 该源发出的 deauth+disassoc 帧数
} rfmon_deauth_src_t;

typedef struct {
    bool     active;
    uint8_t  channel;               // 当前停留的信道
    uint32_t version;               // 表结构变化（新条目/新 SSID）时 +1，UI 据此重建列表
    uint32_t beacons;               // 累计计数（饱和于 0xFFFFFFFF）
    uint32_t probes;
    uint32_t deauths;
    uint32_t disassoc;
    uint8_t  ap_count;
    uint8_t  deauth_src_count;
    rfmon_ap_t aps[RFMON_MAX_AP];
    rfmon_deauth_src_t deauth_srcs[RFMON_MAX_DEAUTH_SRC];
} rfmon_stats_t;                    // ≈ 790B

void rfmon_start(void);             // 开始监听（会断开 STA 并占用射频）
void rfmon_stop(void);              // 停止监听（WiFi_Task 随即重连）
bool rfmon_is_active(void);
void rfmon_clear(void);             // 清空计数与表
void rfmon_get_stats(rfmon_stats_t * out);   // 临界区内的整块快照

// 工具函数（UI 层拼字符串用）
void        rfmon_mac_str(const uint8_t * mac, char * out);   // out 至少 18 字节
const char * rfmon_oui_vendor(const uint8_t * mac);           // 未收录返回 "?" 

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif