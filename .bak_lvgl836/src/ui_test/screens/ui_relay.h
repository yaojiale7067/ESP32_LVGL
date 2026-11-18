// ============================================================================
//  ui_relay.h - 继电器（设备开关）控制屏
//
//  硬件：继电器接 GPIO41（RELAY_PIN 定义在 src/LVGL_Demos.cpp）。
//  分工与 ui_wscan 一致 ——
//      ui_relay.c      只画：大开关 + ON/OFF 状态 + 说明卡片（不认识 Arduino）
//      LVGL_Demos.cpp  只干活：digitalWrite + 串口命令，通过下面两个
//                      C 链接回调和屏体通信。
// ============================================================================

#ifndef UI_RELAY_H
#define UI_RELAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// ---------------- 布局（320x240，纵向梯子见 ui_kit.h） ----------------
//      0..37     顶栏：[← Back]        Relay Control
//      38..154   状态卡片：色条 + "RELAY • GPIO 41" + 大字 OFF/ON + [大开关]
//      158..208  信息卡片：有效电平 / 串口命令说明
//      212..239  状态条
#define RL_CARD_Y       UIK_CONTENT_Y                          // 38
#define RL_CARD_H       116                                    // 38..154
#define RL_INFO_Y       (RL_CARD_Y + RL_CARD_H + UIK_PANEL_GAP) // 158
#define RL_INFO_H       (UIK_STATUS_Y - RL_INFO_Y - UIK_PANEL_GAP) // 50 -> 158..208

// ---------------- 控件句柄 ----------------
extern lv_obj_t * ui_relay;                 // 屏幕本体
extern lv_obj_t * ui_relay_back_btn;
extern lv_obj_t * ui_relay_sw;              // 大开关（lv_switch）
extern lv_obj_t * ui_relay_state_lbl;       // "ON" / "OFF" 大字
extern lv_obj_t * ui_relay_status_label;    // 底部状态

// ---------------- 接口 ----------------
void ui_relay_screen_init(void);
void ui_relay_screen_destroy(void);

// 把界面同步到给定状态（开关位置、ON/OFF 大字、状态条）。
// ★ 程序化改开关状态不会触发 VALUE_CHANGED，所以不会和事件回调打乒乓。
// 屏未建时全部内部判 NULL，业务层可以随时调（串口命令也走这里）。
void ui_relay_set_state(bool on);

// ---------------- 业务回调（实现在 src/LVGL_Demos.cpp） ----------------
void ui_relay_on_toggle_requested(bool on);  // 用户拨了开关：写 GPIO 并回同步界面
bool ui_relay_get_hw_state(void);            // 当前硬件状态（建屏时同步用）

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
