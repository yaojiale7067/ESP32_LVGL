#include "ui_wifi.h"
#include "../ui_kit.h"

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

// 密码框失焦（用户点了别处）就把键盘收起来，否则键盘会一直占着底部
static void password_defocus_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DEFOCUSED) {
        if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
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

// =====================================================================
//  布局（全部改用 ui_kit 的统一常量，320x240）
//  ---------------------------------------------------------------------
//  键盘占底部 120px，所以“非键盘区”只有 y = 0..119：
//      顶栏(0~34)  +  网络列表(38~118)
//  密码框 / Connect / 状态栏 放在 120 以下（键盘没弹出时的区域），
//  键盘弹出会盖住 120~239 —— 此时列表尾部被压住一点，收起键盘即恢复。
//  历史问题：列表曾铺到 y=165，而密码框/Connect 按“贴底 -60”算出来是
//  y=140~180，两者直接压在一起；状态栏(215) 又整条落在键盘区域里。
// =====================================================================
#define WIFI_LIST_Y    38
#define WIFI_LIST_H    80      // 38 + 80 = 118，正好停在键盘上沿之前
#define WIFI_ROW_Y     166     // 密码框与 Connect 同一行
#define WIFI_ROW_H     30
#define WIFI_TA_W      190
#define WIFI_CONN_W    90
#define WIFI_STATUS_Y  200

void ui_wifi_screen_init(void) {
    ui_wifi = uik_screen_init();

    // ---------------- 顶栏：左“返回” / 中“标题” / 右“扫描” ----------------
    uik_topbar(ui_wifi, "WiFi Settings", LV_SYMBOL_REFRESH " Scan",
               back_btn_ui_cb, scan_btn_ui_cb, &ui_wifi_back_btn, &ui_wifi_scan_btn);
    if (ui_wifi_scan_btn != NULL) {
        lv_obj_set_style_bg_color(ui_wifi_scan_btn, lv_color_hex(UIK_C_PRIMARY), LV_STATE_DEFAULT);
    }

    // ---------------- 网络列表（只占键盘上方） ----------------
    lv_obj_t * list_cont = uik_panel(ui_wifi, UIK_PAD, WIFI_LIST_Y,
                                     UIK_W - 2 * UIK_PAD, WIFI_LIST_H);
    lv_obj_set_style_pad_all(list_cont, 2, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);

    ui_wifi_list = lv_list_create(list_cont);
    lv_obj_set_size(ui_wifi_list, UIK_W - 2 * UIK_PAD - 10, LV_SIZE_CONTENT);
    lv_obj_align(ui_wifi_list, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ui_wifi_list, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_wifi_list, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_wifi_list, 0, LV_STATE_DEFAULT);

    // ---------------- 密码 + 连接（同一行，互不重叠） ----------------
    ui_wifi_password_ta = uik_ta(ui_wifi, UIK_PAD, WIFI_ROW_Y, WIFI_TA_W, WIFI_ROW_H,
                                 "Password", true);
    lv_textarea_set_password_mode(ui_wifi_password_ta, true);
    lv_obj_add_event_cb(ui_wifi_password_ta, password_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui_wifi_password_ta, password_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    ui_wifi_connect_btn = uik_btn(ui_wifi, "Connect",
                                  UIK_W - UIK_PAD - WIFI_CONN_W, WIFI_ROW_Y,
                                  WIFI_CONN_W, WIFI_ROW_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(ui_wifi_connect_btn, connect_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 状态栏 ----------------
    ui_wifi_status_label = uik_label(ui_wifi, "Tap Scan to list networks",
                                     UIK_PAD, WIFI_STATUS_Y, UIK_C_TEXT_DIM,
                                     UIK_FONT_BODY, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(ui_wifi_status_label, UIK_W - 2 * UIK_PAD);
    lv_label_set_long_mode(ui_wifi_status_label, LV_LABEL_LONG_WRAP);

    // ---------------- 键盘（隐藏，聚焦密码框时弹出） ----------------
    keyboard = lv_keyboard_create(ui_wifi);
    lv_obj_set_size(keyboard, UIK_W, UIK_KB_H);
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