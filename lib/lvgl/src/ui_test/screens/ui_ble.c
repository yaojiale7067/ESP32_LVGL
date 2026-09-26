// ============================================================================
//  ui_ble.c - 蓝牙(BLE) 串口屏：连接状态 + 数据接收 + 数据发送
//
//  布局（320x240，底部键盘 120px）：
//    y   2 ~  32   顶栏：[← Back]        BLE UART      [Clear]
//    y  34 ~  47   状态行：● Connected / ● Advertising / ● Off   + 对端 MAC
//    y  50 ~ 168   接收区（只读文本框，可上下滚动）
//    y 174 ~ 204   发送行：[输入框 190x30]           [Send 90x30]
//    键盘弹出时占 120 ~ 239，会盖住发送行——但输入框获得焦点后
//    LVGL 会把光标滚进可视区，所以输入过程可见；按键盘 √ / × 收起键盘。
// ============================================================================

#include "ui_ble.h"
#include "../ui_kit.h"

lv_obj_t * ui_ble = NULL;
lv_obj_t * ui_ble_back_btn = NULL;
lv_obj_t * ui_ble_status_label = NULL;
lv_obj_t * ui_ble_rx_ta = NULL;
lv_obj_t * ui_ble_send_ta = NULL;
lv_obj_t * ui_ble_send_btn = NULL;
lv_obj_t * ui_ble_clear_btn = NULL;

static lv_obj_t * ble_keyboard = NULL;

// ---------------------------------------------------------------------------
// 事件
// ---------------------------------------------------------------------------
// 只有“发送框”获得焦点才弹键盘；接收区不该弹（它是只读的）
static void ta_focus_cb(lv_event_t * e)
{
    if (ble_keyboard == NULL) return;
    if (lv_event_get_target(e) != ui_ble_send_ta) return;
    lv_keyboard_set_textarea(ble_keyboard, ui_ble_send_ta);
    lv_obj_clear_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 失焦就把键盘收起来，否则会一直占着底部 120px
static void ta_defocus_cb(lv_event_t * e)
{
    (void)e;
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 进入 BLE 屏之前的那个屏（由入口按钮设置）
static lv_obj_t * ble_prev_scr = NULL;

void ble_set_prev_screen(lv_obj_t * scr)
{
    if (scr != NULL && scr != ui_ble) ble_prev_scr = scr;
}

static void back_btn_cb(lv_event_t * e)
{
    (void)e;
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (ble_prev_scr != NULL) {
        lv_scr_load(ble_prev_scr);      // 回到进来的那个屏
        return;
    }
    extern lv_obj_t * ui_dht11;
    if (ui_dht11) lv_scr_load(ui_dht11);   // 兜底：和 WiFi 屏返回键行为一致
}

// 接收区清空：业务层的显示缓冲由 LVGL_Demos.cpp 里的 ui_ble_on_clear_requested() 负责
static void clear_btn_cb(lv_event_t * e)
{
    (void)e;
    if (ui_ble_rx_ta) lv_textarea_set_text(ui_ble_rx_ta, "");
    ui_ble_on_clear_requested();
}

// 发送/连接/扫描按钮的真实逻辑都在 LVGL_Demos.cpp 里绑定
static void send_btn_ui_cb(lv_event_t * e) { (void)e; }

// 键盘：按 √ = 发送并收起；按 × = 只收起
static void kb_ready_cb(lv_event_t * e)
{
    (void)e;
    ui_ble_on_send_requested();
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void kb_cancel_cb(lv_event_t * e)
{
    (void)e;
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
void ui_ble_screen_init(void)
{
    ui_ble = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ble, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ble, 0, LV_STATE_DEFAULT);

    // ---------------- 顶栏 ----------------
    ui_ble_back_btn = lv_btn_create(ui_ble);
    lv_obj_set_size(ui_ble_back_btn, 60, BLE_TOP_BAR_H);
    lv_obj_align(ui_ble_back_btn, LV_ALIGN_TOP_LEFT, BLE_PAD, 2);
    lv_obj_set_style_bg_color(ui_ble_back_btn, lv_color_hex(0x3D3D3D), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_ble_back_btn, 5, LV_STATE_DEFAULT);
    lv_obj_t * back_label = lv_label_create(ui_ble_back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ble_back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * title = lv_label_create(ui_ble);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " BLE UART");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 9);

    ui_ble_clear_btn = lv_btn_create(ui_ble);
    lv_obj_set_size(ui_ble_clear_btn, 64, BLE_TOP_BAR_H);
    lv_obj_align(ui_ble_clear_btn, LV_ALIGN_TOP_RIGHT, -BLE_PAD, 2);
    lv_obj_set_style_bg_color(ui_ble_clear_btn, lv_color_hex(0x5D5D5D), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_ble_clear_btn, 5, LV_STATE_DEFAULT);
    lv_obj_t * clear_label = lv_label_create(ui_ble_clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);
    lv_obj_set_style_text_color(clear_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ble_clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 状态行 ----------------
    ui_ble_status_label = lv_label_create(ui_ble);
    lv_obj_set_width(ui_ble_status_label, 320 - 2 * BLE_PAD);
    lv_obj_set_style_text_font(ui_ble_status_label, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_align(ui_ble_status_label, LV_ALIGN_TOP_LEFT, BLE_PAD, BLE_STATUS_Y);
    lv_label_set_text(ui_ble_status_label, LV_SYMBOL_BULLET " BLE off");
    lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);

    // ---------------- 接收区（只读、可滚动） ----------------
    ui_ble_rx_ta = lv_textarea_create(ui_ble);
    lv_obj_set_size(ui_ble_rx_ta, 320 - 2 * BLE_PAD, BLE_RX_H);
    lv_obj_align(ui_ble_rx_ta, LV_ALIGN_TOP_LEFT, BLE_PAD, BLE_RX_Y);
    lv_obj_set_style_bg_color(ui_ble_rx_ta, lv_color_hex(0x101010), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_ble_rx_ta, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_ble_rx_ta, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_ble_rx_ta, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_ble_rx_ta, lv_color_hex(0x555555), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ble_rx_ta, 4, LV_STATE_DEFAULT);
    lv_textarea_set_placeholder_text(ui_ble_rx_ta, "Waiting for data...");
    lv_textarea_set_max_length(ui_ble_rx_ta, BLE_RX_MAX_CHARS);
    // 接收区也接上焦点回调：LVGL 的文本框只有在“活动输入组”里才能被拖动滚动，
    // 而键盘只跟发送框绑定，所以这里点一下不会弹出键盘。
    lv_obj_add_event_cb(ui_ble_rx_ta, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_ble_rx_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    // ---------------- 发送行 ----------------
    ui_ble_send_ta = lv_textarea_create(ui_ble);
    lv_obj_set_size(ui_ble_send_ta, BLE_TA_W, BLE_SEND_H);
    lv_obj_align(ui_ble_send_ta, LV_ALIGN_TOP_LEFT, BLE_PAD, BLE_SEND_Y);
    lv_textarea_set_one_line(ui_ble_send_ta, true);
    lv_textarea_set_max_length(ui_ble_send_ta, 200);
    lv_textarea_set_placeholder_text(ui_ble_send_ta, "Data to send...");
    lv_obj_set_style_bg_color(ui_ble_send_ta, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_ble_send_ta, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_ble_send_ta, lv_color_hex(0x555555), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ble_send_ta, 4, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ble_send_ta, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_ble_send_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    ui_ble_send_btn = lv_btn_create(ui_ble);
    lv_obj_set_size(ui_ble_send_btn, BLE_SEND_W, BLE_SEND_H);
    lv_obj_align(ui_ble_send_btn, LV_ALIGN_TOP_RIGHT, -BLE_PAD, BLE_SEND_Y);
    lv_obj_set_style_bg_color(ui_ble_send_btn, lv_color_hex(0x2E8B57), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_ble_send_btn, 5, LV_STATE_DEFAULT);
    lv_obj_t * send_label = lv_label_create(ui_ble_send_btn);
    lv_label_set_text(send_label, LV_SYMBOL_UPLOAD " Send");
    lv_obj_center(send_label);
    lv_obj_set_style_text_color(send_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ble_send_btn, send_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 键盘（隐藏，聚焦发送框时弹出） ----------------
    ble_keyboard = lv_keyboard_create(ui_ble);
    lv_obj_set_size(ble_keyboard, 320, BLE_KB_H);
    lv_obj_align(ble_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ble_keyboard, ui_ble_send_ta);
    lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ble_keyboard, kb_ready_cb,  LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(ble_keyboard, kb_cancel_cb, LV_EVENT_CANCEL, NULL);
}

void ui_ble_screen_destroy(void)
{
    if (ui_ble) lv_obj_del(ui_ble);
    ui_ble = NULL;
    ui_ble_back_btn = NULL;
    ui_ble_status_label = NULL;
    ui_ble_rx_ta = NULL;
    ui_ble_send_ta = NULL;
    ui_ble_send_btn = NULL;
    ui_ble_clear_btn = NULL;
    ble_keyboard = NULL;
}
