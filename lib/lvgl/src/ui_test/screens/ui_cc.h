#ifndef UI_CC_H
#define UI_CC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// 布局与 nRF 屏一致：
//   0..37     顶栏 [← Back] CC1101 [Scan]
//   38..82    四格：PEAK(dBm) / FREQ / CUR(dBm) / HW
//   84..104   动态标题
//   106..206  频谱画布 312x100
//   212..239  状态条
#define CC_CARD_Y       UIK_CONTENT_Y
#define CC_CARD_H       44
#define CC_HDR_Y        (CC_CARD_Y + CC_CARD_H + 2)
#define CC_HDR_H        20
#define CC_SPEC_Y       (CC_HDR_Y + CC_HDR_H + 2)
#define CC_SPEC_W       312
#define CC_SPEC_H       100
#define CC_COL_W        75
#define UI_CC_STEPS     128
#define UI_CC_MAX_ROWS  8

typedef struct {
    char name[52];
    char right[16];
} ui_cc_entry_t;

extern lv_obj_t * ui_cc;
extern lv_obj_t * ui_cc_back_btn;
extern lv_obj_t * ui_cc_run_btn;
extern lv_obj_t * ui_cc_header;
extern lv_obj_t * ui_cc_status_label;

void ui_cc_screen_init(void);
void ui_cc_screen_destroy(void);

void ui_cc_set_running(bool running, bool available);
void ui_cc_set_stats(int8_t peak_dbm, float cur_mhz, int8_t cur_dbm, bool available);
void ui_cc_set_header(const char * text, uint32_t color);
void ui_cc_set_status(const char * text, uint32_t color);

// 推入一列：dbm[128] 与 min/max MHz；commit = 一轮扫完
void ui_cc_spectrum_push(const int8_t * dbm, float min_mhz, float max_mhz, bool commit);
bool ui_cc_spectrum_ready(void);

// 模式：false = 频谱(sweep)，true = 抓包(pkts)
void ui_cc_set_mode(bool pkts);
bool ui_cc_pkts_visible(void);
void ui_cc_set_list(uint32_t version, int n, const ui_cc_entry_t * entries);

void ui_cc_on_toggle(void);
void ui_cc_on_screen_ready(void);
void ui_cc_on_mode(bool pkts);

#ifdef __cplusplus
}
#endif

#endif