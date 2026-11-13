// ============================================================================
//  ui_nrf.h - nRF24L01+ 被动嗅探屏
//
//  接线在 src/nrf24.h（CE14 CSN15 SCK16 MISO17 MOSI18）。
//  模块没接/握手失败时这一屏照样能进，顶栏按钮置灰、状态条直接给出
//  "Handshake failed + 引脚表"，不要"点了没反应"。
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]        nRF24        [Scan/Stop]
//      38..88    统计卡片：PKTS / CH / RATE / HW 四格
//      92..104   动态标题（"CAPTURED • n pkts" / "NO MODULE"）
//      106..206  列表（4 x 25 行整）：最近抓到的包（hex 前缀 + CH/速率）
//      212..239  状态条
//
//  和 ui_rfmon 的分工：
//      ui_rfmon -> 802.11 管理帧（WiFi 芯片自己的射频）
//      ui_nrf   -> 2.4G 私有协议 ShockBurst 帧（外挂 nRF24L01+ 模块）
//  两者都只收不发。
// ============================================================================

#ifndef UI_NRF_H
#define UI_NRF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

#define NRF_CARD_Y      UIK_CONTENT_Y                              // 38
#define NRF_CARD_H      50                                         // 38..88
#define NRF_HDR_Y       (NRF_CARD_Y + NRF_CARD_H + UIK_PANEL_GAP)  // 92
#define NRF_HDR_H       12
#define NRF_LIST_Y      (NRF_HDR_Y + NRF_HDR_H + 2)                // 106
#define NRF_LIST_H      100                                        // 106..206 = 4 x 25
#define NRF_COL_W       75

#define UI_NRF_MAX_ROWS 8

typedef struct {
    char name[52];      // 左列：序号 + 载荷 hex 前缀
    char right[16];     // 右列："CH12 1M"
} ui_nrf_entry_t;

extern lv_obj_t * ui_nrf;
extern lv_obj_t * ui_nrf_back_btn;
extern lv_obj_t * ui_nrf_run_btn;
extern lv_obj_t * ui_nrf_header;
extern lv_obj_t * ui_nrf_list;
extern lv_obj_t * ui_nrf_status_label;

void ui_nrf_screen_init(void);
void ui_nrf_screen_destroy(void);

// available = 引脚已配置且握手成功；false 时按钮置灰并提示排障
void ui_nrf_set_running(bool running, bool available);

// 四格统计：包数 / 当前信道 / 速率档（0=1M 1=2M 2=250K）/ 硬件状态
void ui_nrf_set_stats(uint32_t packets, uint8_t ch, uint8_t rate_idx,
                      bool hopping, bool available);

// (version, n) 变化才重建；否则原地改文本（先比后写）
void ui_nrf_set_list(uint32_t version, int n, const ui_nrf_entry_t * entries);

void ui_nrf_set_header(const char * text, uint32_t color);
void ui_nrf_set_status(const char * text, uint32_t color);

// ---- 业务回调（实现在 src/LVGL_Demos.cpp）----
void ui_nrf_on_toggle(void);
void ui_nrf_on_screen_ready(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif