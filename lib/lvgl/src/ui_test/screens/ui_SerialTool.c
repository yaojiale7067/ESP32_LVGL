// ============================================================================
//  ui_SerialTool.c - 串口调试工具屏
//
//  布局（320x240）：
//      0..37    顶栏：[← Back]        Serial Tool
//      40..54   小节标题 "RECEIVED"
//      56..200  接收区（凹陷底色 + 绿字，可滚动）
//      206..236 发送行：[输入框 212x30]  [Send 92x30]
//
//  键盘弹出时（占底部 120px）重排，输入框始终在键盘上方：
//      40..82   接收区（压到两行高）—— 小节标题隐藏
//      86..116  发送行
//
//  相比旧版：
//    1) 键盘弹出不再盖住输入框（旧版发送行固定 y=174，键盘 120..239 直接压住它，
//       打字时看不到自己输入了什么）。
//    2) 接收区加了一行小节标题，说明这块区域是什么；不再是无名黑框。
//    3) 左右留白统一 ui_kit 的 4，与其它屏对齐。
//    4) 键盘改成 uik_keyboard() 配色。
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

static lv_obj_t * sr_section_label = NULL;
static lv_obj_t * sr_send_btn = NULL;

// ---- 几何 ----------------------------------------------------------------
#define SR_SEC_Y       (UIK_CONTENT_Y + 2)              // 40：小节标题
#define SR_RX_Y        (UIK_CONTENT_Y + 18)             // 56
#define SR_RX_H        144                              // 56..200
#define SR_SEND_Y      206
#define SR_SEND_H      30
#define SR_SEND_W      92
#define SR_TA_W        (UIK_CONTENT_W - UIK_PANEL_GAP - SR_SEND_W)   // 212
// 键盘弹起时
#define SR_RX_Y_KB     (UIK_CONTENT_Y + 2)              // 40
#define SR_SEND_Y_KB   (UIK_KB_TOP - SR_SEND_H - 4)     // 86
#define SR_RX_H_KB     (SR_SEND_Y_KB - 4 - SR_RX_Y_KB)  // 42

// 键盘弹起/收起的两套坐标
static void sr_layout_keyboard_up(bool up)
{
    if (ui_PanelRX) {
        lv_obj_align(ui_PanelRX, LV_ALIGN_TOP_LEFT, UIK_LEFT, up ? SR_RX_Y_KB : SR_RX_Y);
        lv_obj_set_height(ui_PanelRX, up ? SR_RX_H_KB : SR_RX_H);
    }
    if (ui_TextArea1) {
        lv_obj_align(ui_TextArea1, LV_ALIGN_TOP_LEFT, UIK_LEFT, up ? SR_SEND_Y_KB : SR_SEND_Y);
    }
    if (sr_send_btn) {
        lv_obj_align(sr_send_btn, LV_ALIGN_TOP_RIGHT, -UIK_PAD, up ? SR_SEND_Y_KB : SR_SEND_Y);
    }
    // 键盘占掉了小节标题那条，收起它免得压在接收区上
    if (sr_section_label) {
        if (up) lv_obj_add_flag(sr_section_label, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_clear_flag(sr_section_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// 键盘绑定与显示/隐藏
static void ta_focus_cb(lv_event_t * e)
{
    (void)e;
    if (ui_Keyboard1 == NULL) return;
    lv_keyboard_set_textarea(ui_Keyboard1, ui_TextArea1);
    sr_layout_keyboard_up(true);
    lv_obj_clear_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
}
static void ta_defocus_cb(lv_event_t * e)
{
    (void)e;
    if (ui_Keyboard1) lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    sr_layout_keyboard_up(false);
}

// ---- 原有事件（保持函数签名不变，供 ui.h 链接）-----------------------------
void ui_event_Keyboard1(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_READY || event_code == LV_EVENT_CANCEL) {
        if (ui_Keyboard1) lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
        sr_layout_keyboard_up(false);
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
    sr_layout_keyboard_up(false);
}

void ui_SerialTool_screen_init(void)
{
    ui_SerialTool = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_SerialTool, "Serial Tool", NULL, back_cb, NULL, &ui_Button6, NULL);
    if (ui_Button6 != NULL) ui_Label9 = lv_obj_get_child(ui_Button6, 0);

    // ---- 小节标题 ----
    // 一行小字说明"下面是收到的数据"。旧版这里放的是
    // LV_SYMBOL_DOWNLOAD " Received"，图标和文字挤在一起且没有对齐基准。
    sr_section_label = uik_label(ui_SerialTool, "RECEIVED", UIK_LEFT, SR_SEC_Y,
                                 UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);

    // ---- 接收区 ----
    // 用 uik_panel 做外框（圆角+1px 暗边），内部是凹陷底色 + 绿字。
    ui_PanelRX = uik_panel(ui_SerialTool, UIK_LEFT, SR_RX_Y, UIK_CONTENT_W, SR_RX_H);
    lv_obj_set_style_bg_color(ui_PanelRX, lv_color_hex(UIK_C_SUNKEN), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_PanelRX, 5, LV_STATE_DEFAULT);

    ui_labelrx = lv_label_create(ui_PanelRX);
    lv_obj_set_width(ui_labelrx, LV_PCT(100));
    lv_label_set_long_mode(ui_labelrx, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ui_labelrx, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_labelrx, lv_color_hex(UIK_C_OK_TEXT), LV_STATE_DEFAULT);
    lv_label_set_text(ui_labelrx, "");
    lv_obj_align(ui_labelrx, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---- 发送行 ----
    ui_TextArea1 = uik_ta(ui_SerialTool, UIK_LEFT, SR_SEND_Y, SR_TA_W, SR_SEND_H,
                          "Type and press Send...", true);
    lv_textarea_set_max_length(ui_TextArea1, 200);
    lv_obj_add_event_cb(ui_TextArea1, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_TextArea1, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    sr_send_btn = uik_btn(ui_SerialTool, LV_SYMBOL_UPLOAD " Send",
                          UIK_W - UIK_PAD - SR_SEND_W, SR_SEND_Y,
                          SR_SEND_W, SR_SEND_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(sr_send_btn, send_cb, LV_EVENT_CLICKED, NULL);

    // ---- 键盘 ----
    ui_Keyboard1 = lv_keyboard_create(ui_SerialTool);
    lv_obj_set_size(ui_Keyboard1, UIK_W, UIK_KB_H);
    lv_obj_align(ui_Keyboard1, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(ui_Keyboard1);                 // 统一配色
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
    sr_section_label = NULL;
    sr_send_btn = NULL;
}