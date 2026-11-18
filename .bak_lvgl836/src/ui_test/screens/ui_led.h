// ============================================================================
//  ui_led.h - WS2812 灯带控制屏
//
//  硬件：WS2812 数据线接 GPIO39（WS2812_PIN 定义在 src/LVGL_Demos.cpp）。
//  分工沿用本工程惯例 ——
//      ui_led.c        只画：四个滑块 + 数值 + 取色预览（不认识 RMT/Arduino）
//      LVGL_Demos.cpp  干活：RMT 驱动灯带、动画、串口命令
// ============================================================================

#ifndef UI_LED_H
#define UI_LED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// ---------------- 布局（320x240，纵向梯子见 ui_kit.h） ----------------
//      0..37     顶栏：[← Back]        LED Strip
//      38..204   控制卡片：四行
//                  LED COUNT  [  16 ]      ← 数字键盘输入（点击弹出，✓ 确认）
//                  COLOR   180°  [────────●──]
//                  BRIGHT   75%  [──────●────]
//                  SPEED    40%  [────●──────]
//      212..239  状态条
//      (120..239 数字键盘弹出时覆盖底部；LED COUNT 必须排在第一行 ——
//       它是唯一会弹键盘的行，屏幕 y≈53..83，键盘（y≥120）盖不到它，
//       打字时看得见自己输了什么。三根滑块不弹键盘，排后面无所谓。)
//  纵向核算：卡片高 166（38..204），pad4+边框1 → 内容高 156；
//  行距 38：四行起点 10/48/86/124，每行"滑块底 = 起点+30" → 末行止于 154 ≤ 156 ✓
#define LED_CARD_Y      UIK_CONTENT_Y                          // 38
#define LED_CARD_H      (UIK_STATUS_Y - UIK_CONTENT_Y - 2 * UIK_PANEL_GAP) // 166
#define LED_ROW_STEP    38                                     // 每行占 38px
#define LED_CW          (UIK_CONTENT_W - 10)                   // 302：卡片内容宽
#define LED_COUNT_TA_W  84                                     // 数字输入框宽
#define LED_COUNT_TA_H  26

// 控件编号（UI 层与业务层共用的整数约定，等价于枚举）
#define UI_LED_HUE      0     // 色相 0..359
#define UI_LED_BRIGHT   1     // 亮度 0..255
#define UI_LED_SPEED    2     // 渐变（彩虹流动）速度 0..100，0 = 静止纯色
#define UI_LED_COUNT    3     // 灯珠数量 1..120

#define UI_LED_HUE_MAX   359
#define UI_LED_BRIGHT_MAX 255
#define UI_LED_SPEED_MAX 100
#define UI_LED_COUNT_MAX 120
#define UI_LED_COUNT_MIN 1

// ---------------- 控件句柄 ----------------
extern lv_obj_t * ui_led;                  // 屏幕本体
extern lv_obj_t * ui_led_back_btn;
extern lv_obj_t * ui_led_status_label;

// ---------------- 接口 ----------------
void ui_led_screen_init(void);
void ui_led_screen_destroy(void);

// 把界面同步到业务状态：滑块位置 + 数值标签 + 取色预览。
//   rgb 是当前实际输出颜色（业务层算好传进来，UI 层不重复实现 HSV 转换），
//   用于色相滑块的填充色和卡片顶部的色条 —— 那是"灯现在是什么颜色"的唯一反馈。
// 屏未建时内部判 NULL，业务层可随时调。
void ui_led_set_state(int32_t hue, int32_t bright, int32_t speed,
                      int32_t count, uint32_t rgb);

// 状态条文字（业务层拼好，例如 "GPIO39 • 16 LEDs • rainbow"）
void ui_led_set_status(const char * text, uint32_t color);

// ---------------- 业务回调（实现在 src/LVGL_Demos.cpp） ----------------
void ui_led_on_change(int32_t what, int32_t value);  // 用户拖了某个滑块
void ui_led_on_screen_ready(void);                   // 建屏完成，请推送当前状态

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif