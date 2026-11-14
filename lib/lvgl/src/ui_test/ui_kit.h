// ui_kit.h - 全工程统一的 UI 视觉规范

#ifndef UI_KIT_H
#define UI_KIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// ---------------- 尺寸常量 ----------------
#define UIK_W            320
#define UIK_H            240
#define UIK_PANEL_GAP    4
#define UIK_PAD          UIK_PANEL_GAP
#define UIK_LEFT         UIK_PAD                        // 4：左边界
#define UIK_RIGHT_EDGE   (UIK_W - UIK_PAD)              // 316：右边界
#define UIK_CONTENT_W    (UIK_RIGHT_EDGE - UIK_LEFT)    // 312：通栏元素宽度

#define UIK_TOPBAR_H     30      // 顶栏按钮高度
#define UIK_TOPBAR_Y     UIK_PANEL_GAP
#define UIK_TOPBAR_BOT   (UIK_TOPBAR_Y + UIK_TOPBAR_H)    // 34：顶栏按钮下沿
#define UIK_CONTENT_Y    (UIK_TOPBAR_BOT + UIK_PANEL_GAP) // 38：内容起点
#define UIK_STATUS_H     28      // 底部状态条高度
#define UIK_STATUS_Y     (UIK_H - UIK_STATUS_H)      // 212
#define UIK_KB_H         120     // 键盘高度
#define UIK_KB_TOP       (UIK_H - UIK_KB_H)   // 120：键盘上沿
#define UIK_RADIUS       5       // 面板 / 按钮 / 输入框
#define UIK_RADIUS_SM    3       // 徽章、小色条、列表容器

#define UIK_ROW_H        35
#define UIK_BADGE        27

// ---------------- 明度台阶 ----------------
#define UIK_C_BG         0x1A1A1A   // 屏幕底色
#define UIK_C_SURFACE    0x262626   // 面板 / 卡片 / 输入框
#define UIK_C_SURFACE_HI 0x333333   // 按下态、列表选中底
#define UIK_C_SUNKEN     0x0F0F0F   // 凹陷：终端 / 接收区底色
#define UIK_C_BORDER     0x343434   // 分隔线 / 面板描边
#define UIK_C_DARK       UIK_C_SUNKEN   // 旧名，保留兼容

// ---------------- 语义色 ----------------
#define UIK_C_PRIMARY    0x2D6CB5   // 主操作（蓝）
#define UIK_C_PRIMARY_HI 0x3D86D8   // 主操作按下
#define UIK_C_SUCCESS    0x2E8B57   // 确认 / 发送 / 新建（绿）
#define UIK_C_SUCCESS_HI 0x3CA86E   // 绿按下
#define UIK_C_NEUTRAL    0x3A3A3A   // 次要操作（灰）
#define UIK_C_NEUTRAL_HI 0x4D4D4D   // 灰按下
#define UIK_C_DANGER     0xC94A4A   // 破坏性操作（红）
#define UIK_C_DANGER_HI  0xE06060

#define UIK_C_TEXT       0xF0F0F0   // 主文本
#define UIK_C_TEXT_DIM   0x9C9C9C   // 次要：卡片小标题、单位、提示
#define UIK_C_TEXT_FAINT 0x8A8A8A   // 更弱：版本号、辅助说明
#define UIK_C_OK_TEXT    0x79D08A   // 成功 / 终端回显
#define UIK_C_WARN_TEXT  0xE8C468
#define UIK_C_ERR_TEXT   0xE88383

// ---------------- 功能入口的身份色 ----------------
#define UIK_ACCENT_TEMP    0xD4845C   // 温湿度：土橙
#define UIK_ACCENT_FILE    0x48988C   // 文件：灰松石
#define UIK_ACCENT_SERIAL  0x86A85F   // 串口：灰草绿
#define UIK_ACCENT_WIFI    0x497FA8   // WiFi：灰蓝
#define UIK_ACCENT_BLE     0x746299   // 蓝牙：灰紫
#define UIK_ACCENT_HUM     0x5898A3   // 湿度：灰冰青
#define UIK_ACCENT_CHIP    0xB85C5C   // 片内温度：暗红
#define UIK_ACCENT_MON     0xA84F78   // RF Monitor：灰玫红
#define UIK_ACCENT_NRF     0x633974   // rfmon-nrf：暗灰紫


// ---------------- 字体 ----------------
LV_FONT_DECLARE(ui_font_cjk12)

const lv_font_t * uik_font_body(void);    // montserrat_12 + 中文 fallback
const lv_font_t * uik_font_title(void);   // montserrat_14 + 中文 fallback

#define UIK_FONT_TITLE   (uik_font_title())
#define UIK_FONT_BODY    (uik_font_body())
#define UIK_FONT_HINT    (&lv_font_montserrat_10)
#define UIK_FONT_ICON    (&lv_font_montserrat_16)
#define UIK_FONT_NUM     (&lv_font_montserrat_40)
#define UIK_FONT_BIG     (&lv_font_montserrat_20)

// ---------------- 屏幕外壳 ----------------
lv_obj_t * uik_screen_init(void);
lv_obj_t * uik_status_bar(lv_obj_t * parent);
lv_obj_t * uik_separator(lv_obj_t * parent, int x, int y, int w);

void uik_screen_load(lv_obj_t * scr);

// ---------------- 顶栏（三件套） ----------------
lv_obj_t * uik_topbar(lv_obj_t * parent, const char * title,
                      const char * right_btn_text,
                      lv_event_cb_t back_cb, lv_event_cb_t right_cb,
                      lv_obj_t ** back_out, lv_obj_t ** right_out);

void uik_topbar_set_title(lv_obj_t * topbar, const char * title);

// ---------------- 常用控件 ----------------
lv_obj_t * uik_panel(lv_obj_t * parent, int x, int y, int w, int h);

lv_obj_t * uik_btn(lv_obj_t * parent, const char * text,
                   int x, int y, int w, int h, uint32_t bg);

lv_obj_t * uik_ta(lv_obj_t * parent, int x, int y, int w, int h,
                  const char * placeholder, bool one_line);

lv_obj_t * uik_ta_readonly(lv_obj_t * parent, int x, int y, int w, int h,
                           const char * placeholder);

lv_obj_t * uik_label(lv_obj_t * parent, const char * text,
                     int x, int y, uint32_t color,
                     const lv_font_t * font, lv_text_align_t align);

// ---------------- 状态反馈 ----------------
void uik_btn_states(lv_obj_t * btn, uint32_t base);
void uik_ta_focus_ring(lv_obj_t * ta);

// ---------------- 复杂控件的统一皮肤 ----------------
void uik_keyboard(lv_obj_t * kb);

void uik_list(lv_obj_t * list);
void uik_list_row(lv_obj_t * row);

// ---------------- 菜单行 ----------------
lv_obj_t * uik_menu_row(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, uint32_t bg,
                        lv_event_cb_t cb, void * user_data);

// 带图标徽章的菜单行（推荐）
lv_obj_t * uik_menu_row_icon(lv_obj_t * parent, int x, int y, int w, int h,
                             const char * symbol, const char * title,
                             uint32_t accent, uint32_t bg,
                             lv_event_cb_t cb, void * user_data,
                             lv_obj_t ** title_out);

lv_obj_t * uik_badge(lv_obj_t * parent, int x, int y, int size,
                     const char * symbol, uint32_t accent);

lv_obj_t * uik_accent_bar(lv_obj_t * parent, int x, int y, int w, uint32_t accent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif