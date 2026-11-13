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
#include "../ui_kit.h"

// ===== 布局常量（屏幕 320x240，底部键盘占 120px） =====
// 几何和别的屏一样从 ui_kit 推导，缝隙统一用 ui_kit 的 4：
//      0..38      顶栏 [← Back]  BLE UART  [Clear]
//      40..56     连接状态行（● Connected  对端MAC  RX/TX）
//      58..202    接收区（凹陷底色，等宽感，可滚动）
//      206..236   发送行：[输入框 212x30]  [Send 92x30]
//   键盘弹出时（120..239）整块上移：
//      58..82     接收区压到"一行高"，只显示最新收到的内容
//      86..116    发送行紧贴键盘上沿 —— 打字时输入框始终可见
//   ★ 旧版发送行固定 y=174，键盘一弹就把整行盖住，输入时看不到自己打的字。
#define BLE_STATUS_Y     (UIK_CONTENT_Y + 2)              // 40
#define BLE_RX_Y         (UIK_CONTENT_Y + 20)             // 58
#define BLE_RX_H         144                              // 58..202
#define BLE_COMP_Y       206                              // 发送行（键盘收起时）
#define BLE_COMP_H       30
#define BLE_SEND_W       92
#define BLE_TA_W         (UIK_CONTENT_W - UIK_PANEL_GAP - BLE_SEND_W)   // 212
#define BLE_KB_H         UIK_KB_H                         // 120
#define BLE_KB_TOP       (UIK_H - BLE_KB_H)               // 120
#define BLE_COMP_Y_KB    (BLE_KB_TOP - BLE_COMP_H - 4)    // 86：键盘弹出时的发送行
#define BLE_RX_Y_KB      (UIK_CONTENT_Y + 2)              // 40：键盘弹出时状态行让位
#define BLE_RX_H_KB      (BLE_COMP_Y_KB - 4 - BLE_RX_Y_KB) // 42：约两行
#define BLE_RX_MAX_CHARS 400    // 接收区最多保留的字符数，防止重绘变慢

// ===== 控件句柄 =====
extern lv_obj_t * ui_ble;                   // 屏幕本体
extern lv_obj_t * ui_ble_back_btn;
extern lv_obj_t * ui_ble_status_label;      // "● Connected  11:22:33:44:55:66"
extern lv_obj_t * ui_ble_rx_ta;             // 接收区（只读）
extern lv_obj_t * ui_ble_send_ta;           // 发送输入框
extern lv_obj_t * ui_ble_send_btn;
extern lv_obj_t * ui_ble_clear_btn;         // 清空接收区

// ===== 接口 =====
void ui_ble_screen_init(void);
void ui_ble_screen_destroy(void);

// 进入 BLE 屏之前调用，记住来源屏幕，返回键才能回到原处。
// 不调用的话返回键默认回主菜单。
void ble_set_prev_screen(lv_obj_t * scr);

// 业务回调：由本文件里的按钮/键盘触发，实现在 src/ble_uart.cpp。
// 必须在这里声明，否则 C 编译单元会因为缺原型而报 implicit-declaration 警告。
void ui_ble_on_send_requested(void);
void ui_ble_on_clear_requested(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif