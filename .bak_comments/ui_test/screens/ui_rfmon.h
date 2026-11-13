// ============================================================================
//  ui_rfmon.h - RF Monitor 屏（802.11 管理帧被动监听，只收不发）
//
//  和 ui_wscan（主动扫描信道占用）互补：
//      ui_wscan  -> 发探测帧，回答"哪个信道干净"
//      ui_rfmon  -> 纯接收，回答"谁在发 deauth / 周围实际有哪些 AP 在发包"
//  屏幕本身不认识 esp_wifi：业务层（src/LVGL_Demos.cpp）把快照翻译成
//  下面的 set_* 调用，按钮通过 ui_rfmon_on_toggle() 反向回调。
// ============================================================================

#ifndef UI_RFMON_H
#define UI_RFMON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// ---------------- 布局（320x240） ----------------
//      0..37     顶栏：[← Back]      RF Monitor      [Start/Stop]
//      38..88    统计卡片：BEACON / PROBE / DEAUTH / DISAS 四个大数字
//      92..104   动态标题：有 deauth 源时 "DEAUTH SOURCES"（红），
//                否则 "BEACONS SEEN • n AP"
//      106..206  列表（4 x 25 行整）：deauth 源（MAC 厂商 ×N）或 AP 表
//      212..239  状态条：当前信道 / 提示 WiFi 已暂停
#define RFM_CARD_Y      UIK_CONTENT_Y                            // 38
#define RFM_CARD_H      50                                       // 38..88
#define RFM_HDR_Y       (RFM_CARD_Y + RFM_CARD_H + UIK_PANEL_GAP) // 92
#define RFM_HDR_H       12
#define RFM_LIST_Y      (RFM_HDR_Y + RFM_HDR_H + 2)              // 106
#define RFM_LIST_H      100                                      // 106..206 = 4 x 25

#define UI_RFMON_MAX_ROWS  16
#define RFM_COL_W         75      // 统计卡片 4 列，每列宽（内容宽 302）

// 列表一行：左主文本（SSID/MAC + 厂商），右侧元信息（"CH6 x123" / "x45"）
typedef struct {
    char name[48];
    char right[16];
} ui_rfmon_entry_t;

// ---------------- 控件句柄 ----------------
extern lv_obj_t * ui_rfmon;
extern lv_obj_t * ui_rfmon_back_btn;
extern lv_obj_t * ui_rfmon_run_btn;      // 顶栏右侧 Start/Stop
extern lv_obj_t * ui_rfmon_header;
extern lv_obj_t * ui_rfmon_list;
extern lv_obj_t * ui_rfmon_status_label;

// ---------------- 接口 ----------------
void ui_rfmon_screen_init(void);
void ui_rfmon_screen_destroy(void);

// Start/Stop 按钮的外观切换（业务层根据 rfmon_is_active() 调用）
void ui_rfmon_set_running(bool running);

// 四个统计数字（内部：先比较再写，避免无谓失效；>=100000 显示为 "123K"）
void ui_rfmon_set_counts(uint32_t beacons, uint32_t probes,
                         uint32_t deauths, uint32_t disassoc);

// 列表：(version, deauth_mode, n) 任一变化才重建行；
// 否则只对左右两列文本做"变了才写"的原地更新（防小块重绘撕裂）。
void ui_rfmon_set_list(uint32_t version, bool deauth_mode,
                       int n, const ui_rfmon_entry_t * entries);

void ui_rfmon_set_header(const char * text, uint32_t color);
void ui_rfmon_set_status(const char * text, uint32_t color);

// ---------------- 业务回调（实现在 src/LVGL_Demos.cpp） ----------------
void ui_rfmon_on_toggle(void);           // 点 Start/Stop
void ui_rfmon_on_screen_ready(void);     // 建屏完成，请推送当前快照

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif