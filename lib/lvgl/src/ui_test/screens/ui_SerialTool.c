// ============================================================================
//  ui_SerialTool.c - 串口调试工具屏
//  统一后的样式：顶栏(返回) + 接收框 + 发送行 + 隐藏键盘
//  布局（320x240，键盘占底部 120）：
//      顶栏          y 2~32
//      RX 标签       y 36
//      RX 文本框     x 10~310 / y 50~166（绿字深底，可滚动）
//      发送行        x 10~200 输入框 + x 220~310 Send，y 174~204
//      键盘          y 120~239（隐藏，聚焦输入框时弹出）
//
//  对外符号保留：ui_labelrx（业务层会往里写串口命令回显）、
//  ui_TextArea1 / ui_Keyboard1（键盘事件函数依赖）
// ============================================================================

#include "../ui.h"
#include "../ui_kit.h"
#include <string.h>

// 声明串口发送函数
extern void serial_send(const char *buf);

lv_obj_t * ui_SerialTool = NULL;
lv_obj_t * ui_PanelRX = NULL;      // 接收区容器
lv_obj_t * ui_labelrx = NULL;      // 接收文本
lv_obj_t * ui_TextArea1 = NULL;    // 发送输入框
lv_obj_t * ui_Keyboard1 = NULL;
lv_obj_t * ui_Button6 = NULL;      // 返回
lv_obj_t * ui_Label9 = NULL;
lv_obj_t * ui_Spinner1 = NULL;     // 保留符号（新布局里不再使用）

// 键盘绑定与显示/隐藏
static void ta_focus_cb(lv_event_t * e)
{
    (void)e;
    if (ui_Keyboard1 == NULL) return;
    lv_keyboard_set_textarea(ui_Keyboard1, ui_TextArea1);
    lv_obj_clear_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
}
static void ta_defocus_cb(lv_event_t * e)
{
    (void)e;
    if (ui_Keyboard1) lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
}

// ---- 原有事件（保持函数签名不变，供 ui.h 链接）-----------------------------
void ui_event_Keyboard1(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_READY || event_code == LV_EVENT_CANCEL) {
        if (ui_Keyboard1) lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // 键盘上按 √ 的兼容路径（部分 LVGL 版本走 VALUE_CHANGED）
    if (event_code == LV_EVENT_VALUE_CHANGED && ui_Keyboard1 != NULL) {
        uint16_t btn = lv_keyboard_get_selected_btn(ui_Keyboard1);
        const char * txt = lv_keyboard_get_btn_text(ui_Keyboard1, btn);
        if (txt != NULL && strcmp(txt, LV_SYMBOL_OK) == 0) {
            const char * data = lv_textarea_get_text(ui_TextArea1);
            if (data != NULL && *data != '\0') {
                serial_send((char *)data);
                lv_textarea_set_text(ui_TextArea1, "");
            }
        }
    }
}

void ui_event_Button6(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Screen1_screen_init);
    }
}

void ui_event_Label9(lv_event_t * e)
{
    ui_event_Button6(e);
}

static void back_cb(lv_event_t * e) { ui_event_Button6(e); }

// Send 按钮
static void send_cb(lv_event_t * e)
{
    (void)e;
    if (ui_TextArea1 == NULL) return;
    const char * data = lv_textarea_get_text(ui_TextArea1);
    if (data == NULL || *data == '\0') return;
    serial_send((char *)data);
    lv_textarea_set_text(ui_TextArea1, "");
    if (ui_Keyboard1) lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
}

void ui_SerialTool_screen_init(void)
{
    ui_SerialTool = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_SerialTool, "Serial Tool", NULL, back_cb, NULL, &ui_Button6, NULL);
    if (ui_Button6 != NULL) ui_Label9 = lv_obj_get_child(ui_Button6, 0);

    // ---- 接收区 ----
    lv_obj_t * rx_title = uik_label(ui_SerialTool, LV_SYMBOL_DOWNLOAD " Received",
                                    UIK_PAD, 34, UIK_C_TEXT_DIM,
                                    UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    (void)rx_title;

    ui_PanelRX = uik_panel(ui_SerialTool, UIK_PAD, 50, UIK_W - 2 * UIK_PAD, 116);
    lv_obj_set_style_bg_color(ui_PanelRX, lv_color_hex(UIK_C_DARK), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_PanelRX, 4, LV_STATE_DEFAULT);

    ui_labelrx = lv_label_create(ui_PanelRX);
    lv_obj_set_width(ui_labelrx, LV_PCT(100));
    lv_label_set_long_mode(ui_labelrx, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ui_labelrx, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_labelrx, lv_color_hex(UIK_C_OK_TEXT), LV_STATE_DEFAULT);
    lv_label_set_text(ui_labelrx, "");
    lv_obj_align(ui_labelrx, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---- 发送行 ----
    ui_TextArea1 = uik_ta(ui_SerialTool, UIK_PAD, 174, 190, 30,
                          "Type and press Send...", true);
    lv_textarea_set_max_length(ui_TextArea1, 200);
    lv_obj_add_event_cb(ui_TextArea1, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_TextArea1, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t * send = uik_btn(ui_SerialTool, LV_SYMBOL_UPLOAD " Send",
                              UIK_W - UIK_PAD - 90, 174, 90, 30, UIK_C_SUCCESS);
    lv_obj_add_event_cb(send, send_cb, LV_EVENT_CLICKED, NULL);

    // ---- 键盘 ----
    ui_Keyboard1 = lv_keyboard_create(ui_SerialTool);
    lv_obj_set_size(ui_Keyboard1, UIK_W, UIK_KB_H);
    lv_obj_align(ui_Keyboard1, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ui_Keyboard1, ui_TextArea1);
    lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_Keyboard1, ui_event_Keyboard1, LV_EVENT_ALL, NULL);
}

void ui_SerialTool_screen_destroy(void)
{
    if (ui_SerialTool) lv_obj_del(ui_SerialTool);

    ui_SerialTool = NULL;
    ui_PanelRX = NULL;
    ui_labelrx = NULL;
    ui_TextArea1 = NULL;
    ui_Keyboard1 = NULL;
    ui_Button6 = NULL;
    ui_Label9 = NULL;
    ui_Spinner1 = NULL;
}
