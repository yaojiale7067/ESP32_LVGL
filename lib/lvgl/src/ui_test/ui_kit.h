// ============================================================================
//  ui_kit.h - 全工程统一的 UI 视觉规范 / 组件工厂
//
//  设计语言（源自蓝牙屏那一版，现在推广到所有屏）：
//      背景 0x1E1E1E  →  面板/输入框 0x2D2D2D  →  边框 0x555555
//      主色(蓝) 0x2D6CB5   成功/发送(绿) 0x2E8B57
//      次要按钮(灰) 0x3D3D3D   危险(红) 0xFF5555
//      文本：主 0xFFFFFF / 次要 0xAAAAAA / 成功 0x88FF88 / 警告 0xFFFF88 / 错误 0xFF8888
//      圆角 5px，左右留白 10px，顶栏高 30px
//
//  统一后的每屏结构：
//      [← Back]      标题        [右侧按钮(可选)]      ← 顶栏 y 2~32
//      y 38 起：内容区（面板 / 列表 / 大号数值）
//      y 208~236：底部状态提示（横向滚动）
// ============================================================================

#ifndef UI_KIT_H
#define UI_KIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// ---------------- 尺寸常量 ----------------
#define UIK_W            320
#define UIK_H            240
#define UIK_PAD          10      // 左右留白
#define UIK_TOPBAR_H     30      // 顶栏按钮高度
#define UIK_TOPBAR_Y     2
#define UIK_CONTENT_Y    38      // 顶栏下方内容起始 y
#define UIK_STATUS_H     28      // 底部状态条高度
#define UIK_STATUS_Y     (UIK_H - UIK_STATUS_H)      // 212
#define UIK_KB_H         120     // 键盘高度
#define UIK_RADIUS       5

// ---------------- 配色 ----------------
#define UIK_C_BG         0x1E1E1E
#define UIK_C_SURFACE    0x2D2D2D
#define UIK_C_BORDER     0x555555
#define UIK_C_DARK       0x101010
#define UIK_C_PRIMARY    0x2D6CB5
#define UIK_C_SUCCESS    0x2E8B57
#define UIK_C_NEUTRAL    0x3D3D3D
#define UIK_C_DANGER     0xFF5555
#define UIK_C_TEXT       0xFFFFFF
#define UIK_C_TEXT_DIM   0xAAAAAA
#define UIK_C_OK_TEXT    0x88FF88
#define UIK_C_WARN_TEXT  0xFFFF88
#define UIK_C_ERR_TEXT   0xFF8888

// ---------------- 字号（全工程只用这几档，不要再手写 lv_font_montserrat_XX）-
//
//   TITLE  14pt  顶栏标题 / 按钮文字 / 菜单行
//   BODY   12pt  正文、输入框、状态条、列表
//   HINT   10pt  次要说明、单位、卡片小标题
//   NUM    28pt  仅用于温湿度这类"要大号显示"的数值
//
//  历史上这里混用过 12/14/16/44 四种，导致同一层级的文字大小不一致，
//  而且 16pt 的菜单行和 44pt 的数值明显偏大。改字号只改这一处即可。
#define UIK_FONT_TITLE   (&lv_font_montserrat_14)
#define UIK_FONT_BODY    (&lv_font_montserrat_12)
#define UIK_FONT_HINT    (&lv_font_montserrat_10)
#define UIK_FONT_NUM     (&lv_font_montserrat_28)

// ---------------- 屏幕外壳 ----------------
lv_obj_t * uik_screen_init(void);                       // 建屏 + 统一底色
lv_obj_t * uik_status_bar(lv_obj_t * parent);           // 底部状态提示条（返回标签）

// ---------------- 顶栏（三件套） ----------------
// title 可以为 NULL；right_btn_text 为 NULL 时不建右侧按钮。
// back_out / right_out 可以为 NULL；传入非空指针时会回传对应按钮句柄
// （避免用 lv_obj_get_child 猜索引那种脆弱写法）。
// 返回值：顶栏容器本身。
lv_obj_t * uik_topbar(lv_obj_t * parent, const char * title,
                      const char * right_btn_text,
                      lv_event_cb_t back_cb, lv_event_cb_t right_cb,
                      lv_obj_t ** back_out, lv_obj_t ** right_out);

void uik_topbar_set_title(lv_obj_t * topbar, const char * title);

// ---------------- 常用控件 ----------------
lv_obj_t * uik_panel(lv_obj_t * parent, int x, int y, int w, int h);   // 面板/卡片

lv_obj_t * uik_btn(lv_obj_t * parent, const char * text,
                   int x, int y, int w, int h, uint32_t bg);           // 普通按钮

lv_obj_t * uik_ta(lv_obj_t * parent, int x, int y, int w, int h,
                  const char * placeholder, bool one_line);            // 文本框

lv_obj_t * uik_label(lv_obj_t * parent, const char * text,
                     int x, int y, uint32_t color,
                     const lv_font_t * font, lv_text_align_t align);   // 文本

// ---------------- 菜单行 ----------------
// 用于主菜单之类"整行可点"的条目；返回该行按钮
lv_obj_t * uik_menu_row(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, uint32_t bg,
                        lv_event_cb_t cb, void * user_data);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
