#ifndef UI_NRF_H
#define UI_NRF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// 布局（320x240）：
//   0..37     顶栏
//   38..82    四格统计卡片（PKTS / CH / RATE / HW）
//   84..104   左侧动态标题 + 右侧 [Pkts][Spec] 模式开关
//   106..206  内容区：抓包列表（4x25 行）或频谱图（312x100）
//   212..239  状态条
#define NRF_CARD_Y      UIK_CONTENT_Y                              // 38
#define NRF_CARD_H      44                                         // 38..82
#define NRF_HDR_Y       (NRF_CARD_Y + NRF_CARD_H + 2)              // 84
#define NRF_HDR_H       20
#define NRF_LIST_Y      (NRF_HDR_Y + NRF_HDR_H + 2)                // 106
#define NRF_LIST_H      100
#define NRF_COL_W       75

#define NRF_SPEC_W      312
#define NRF_SPEC_H      100
#define UI_NRF_CH_NUM   126        // RF_CH 0..125

#define UI_NRF_MAX_ROWS 8

typedef struct {
    char name[52];
    char right[16];
} ui_nrf_entry_t;

extern lv_obj_t * ui_nrf;
extern lv_obj_t * ui_nrf_back_btn;
extern lv_obj_t * ui_nrf_run_btn;
extern lv_obj_t * ui_nrf_header;
extern lv_obj_t * ui_nrf_list;
extern lv_obj_t * ui_nrf_status_label;

void ui_nrf_screen_init(void);
void ui_nrf_screen_destroy(void);

void ui_nrf_set_running(bool running, bool available);
void ui_nrf_set_stats(uint32_t packets, uint8_t ch, uint8_t rate_idx,
                      bool hopping, bool available);
void ui_nrf_set_list(uint32_t version, int n, const ui_nrf_entry_t * entries);
void ui_nrf_set_header(const char * text, uint32_t color);
void ui_nrf_set_status(const char * text, uint32_t color);

// 频谱视图：把一次扫描的活动度数组画进当前这一列
//   vals[126] 0..255，lo..hi 是扫描窗口，commit=true 表示"这一轮扫完了，翻新一列"
void ui_nrf_spectrum_push(const uint8_t * vals, uint8_t lo, uint8_t hi, bool commit);
bool ui_nrf_spectrum_visible(void);
bool ui_nrf_spectrum_ready(void);
bool ui_nrf_canvas_hidden(void);
bool ui_nrf_list_hidden(void);
void ui_nrf_set_mode(bool spectrum);

void ui_nrf_on_toggle(void);
void ui_nrf_on_screen_ready(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif