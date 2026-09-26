// ============================================================================
//  ui_ble.h - 蓝牙(BLE) 串口屏
//  独立于 SquareLine 生成的 ui.c，由 LVGL_Demos.cpp 的 original_setup() 调
//  ui_ble_screen_init() 创建，避免动到 ui.c 被 SquareLine 重新生成时覆盖。
// ============================================================================

#ifndef UI_BLE_H
#define UI_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// ===== 布局常量（屏幕 320x240，底部键盘占 120px） =====
#define BLE_PAD          10     // 左右留白
#define BLE_TOP_BAR_H    30     // 返回 / 标题 这一行
#define BLE_STATUS_Y     34     // 状态行
#define BLE_RX_Y         50     // 接收区
#define BLE_RX_H         118
#define BLE_SEND_Y       174    // 发送行
#define BLE_SEND_H       30
#define BLE_TA_W         190
#define BLE_SEND_W       90
#define BLE_KB_H         120
#define BLE_RX_MAX_CHARS 400    // 接收区最多保留的字符数，防止重绘变慢

// ===== 控件句柄 =====
extern lv_obj_t * ui_ble;                   // 屏幕本体
extern lv_obj_t * ui_ble_back_btn;
extern lv_obj_t * ui_ble_status_label;      // “● Connected  11:22:33:44:55:66”
extern lv_obj_t * ui_ble_rx_ta;             // 接收区（只读）
extern lv_obj_t * ui_ble_send_ta;           // 发送输入框
extern lv_obj_t * ui_ble_send_btn;
extern lv_obj_t * ui_ble_clear_btn;         // 清空接收区

// ===== 接口 =====
void ui_ble_screen_init(void);
void ui_ble_screen_destroy(void);

// 进入 BLE 屏之前调用，记住来源屏幕，返回键才能回到原处。
// 不调用的话返回键默认回 ui_dht11。
void ble_set_prev_screen(lv_obj_t * scr);

// 业务回调：由本文件里的按钮/键盘触发，实现在 src/ble_uart.cpp。
// 必须在这里声明，否则 C 编译单元会因为缺原型而报 implicit-declaration 警告。
void ui_ble_on_send_requested(void);
void ui_ble_on_clear_requested(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
