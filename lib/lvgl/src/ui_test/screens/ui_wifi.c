#include "ui_wifi.h"

lv_obj_t * ui_wifi = NULL;
lv_obj_t * ui_wifi_list = NULL;
lv_obj_t * ui_wifi_password_ta = NULL;
lv_obj_t * ui_wifi_scan_btn = NULL;
lv_obj_t * ui_wifi_connect_btn = NULL;
lv_obj_t * ui_wifi_back_btn = NULL;
lv_obj_t * ui_wifi_status_label = NULL;

static lv_obj_t * keyboard = NULL;

static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void password_focus_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        if (keyboard) {
            lv_keyboard_set_textarea(keyboard, ui_wifi_password_ta);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 返回按钮：加载主屏幕（假设主屏幕是 ui_dht11）
static void back_btn_ui_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        extern lv_obj_t * ui_dht11;
        if (ui_dht11) lv_scr_load(ui_dht11);
    }
}

// 扫描和连接按钮留空，业务逻辑在 LVGL_Demos.cpp 中绑定
static void scan_btn_ui_cb(lv_event_t * e) { (void)e; }
static void connect_btn_ui_cb(lv_event_t * e) { (void)e; }

void ui_wifi_screen_init(void) {
    ui_wifi = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_wifi, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);

    lv_obj_t * title = lv_label_create(ui_wifi);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    ui_wifi_scan_btn = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_wifi_scan_btn, 70, 30);
    lv_obj_align(ui_wifi_scan_btn, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_t * scan_label = lv_label_create(ui_wifi_scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);
    lv_obj_add_event_cb(ui_wifi_scan_btn, scan_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    ui_wifi_back_btn = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_wifi_back_btn, 50, 34);
    lv_obj_align(ui_wifi_back_btn, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_t * back_label = lv_label_create(ui_wifi_back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(ui_wifi_back_btn, back_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * list_cont = lv_obj_create(ui_wifi);
    lv_obj_set_size(list_cont, 300, 120);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_cont, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);
    ui_wifi_list = lv_list_create(list_cont);
    lv_obj_set_size(ui_wifi_list, 280, LV_SIZE_CONTENT);
    lv_obj_center(ui_wifi_list);
    lv_obj_set_style_bg_color(ui_wifi_list, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);

    ui_wifi_password_ta = lv_textarea_create(ui_wifi);
    lv_obj_set_size(ui_wifi_password_ta, 200, 40);
    lv_obj_align(ui_wifi_password_ta, LV_ALIGN_BOTTOM_LEFT, 10, -60);
    lv_textarea_set_placeholder_text(ui_wifi_password_ta, "Password");
    lv_textarea_set_password_mode(ui_wifi_password_ta, true);
    lv_obj_set_style_bg_color(ui_wifi_password_ta, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_wifi_password_ta, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_wifi_password_ta, password_focus_cb, LV_EVENT_FOCUSED, NULL);

    ui_wifi_connect_btn = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_wifi_connect_btn, 80, 40);
    lv_obj_align(ui_wifi_connect_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -60);
    lv_obj_t * conn_label = lv_label_create(ui_wifi_connect_btn);
    lv_label_set_text(conn_label, "Connect");
    lv_obj_center(conn_label);
    lv_obj_add_event_cb(ui_wifi_connect_btn, connect_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    ui_wifi_status_label = lv_label_create(ui_wifi);
    lv_obj_set_width(ui_wifi_status_label, 300);
    lv_obj_align(ui_wifi_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(ui_wifi_status_label, "");
    lv_obj_set_style_text_align(ui_wifi_status_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);

    keyboard = lv_keyboard_create(ui_wifi);
    lv_obj_set_size(keyboard, 320, 120);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, ui_wifi_password_ta);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
}

void ui_wifi_screen_destroy(void) {
    if (ui_wifi) lv_obj_del(ui_wifi);
    ui_wifi = NULL;
    ui_wifi_list = NULL;
    ui_wifi_password_ta = NULL;
    ui_wifi_scan_btn = NULL;
    ui_wifi_connect_btn = NULL;
    ui_wifi_back_btn = NULL;
    ui_wifi_status_label = NULL;
    keyboard = NULL;
}