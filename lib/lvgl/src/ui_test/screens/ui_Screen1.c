// ============================================================================
//  ui_Screen1.c - 主菜单
//  统一后的样式：顶栏 + 5 行整行可点的菜单项（与蓝牙屏同一套视觉规范）
//  菜单项：Temp/RH、File Manager、Serial Tool、WiFi、Bluetooth
//
//  对外符号保持不变（ui_Screen1 / ui_Button2/3/5/8 / ui_Label2/5/7/11/12），
//  这样 ui_events.h 里由 SquareLine 生成的 event 函数仍能正常链接。
// ============================================================================

#include "../ui.h"
#include "../ui_kit.h"
#if ENABLE_BLE
#include "ui_ble.h"      // ui_ble / ble_set_prev_screen
#endif

lv_obj_t * uic_Screen1;
lv_obj_t * ui_Screen1 = NULL;
lv_obj_t * ui_Button5 = NULL;      // Uart
lv_obj_t * ui_Label7 = NULL;
lv_obj_t * ui_Button3 = NULL;      // Temp
lv_obj_t * ui_Label2 = NULL;
lv_obj_t * ui_Label11 = NULL;      // 左下角版本号
lv_obj_t * ui_Button2 = NULL;      // File
lv_obj_t * ui_Label5 = NULL;
lv_obj_t * ui_Button8 = NULL;      // WiFi
lv_obj_t * ui_Label12 = NULL;

static lv_obj_t * ui_ButtonBle = NULL;    // BLE（新增）
static lv_obj_t * ui_LabelBle = NULL;

// ---- 菜单行的点击处理（统一用无动画切换，视觉上更干净）--------------------
static void go_serial_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_SerialTool, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_SerialTool_screen_init);
}
static void go_dht11_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_dht11, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_dht11_screen_init);
}
static void go_file_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_file, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_file_screen_init);
}
static void go_wifi_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_wifi_screen_init);
}

// 注意：本文件按 C 编译，**不能用 lambda**，回调必须是普通静态函数
#if ENABLE_BLE
static void go_ble_cb(lv_event_t * e)
{
    (void)e;
    if (ui_ble != NULL) {
        ble_set_prev_screen(lv_scr_act());   // 记住来源屏，BLE 屏的返回键才回得来
        lv_scr_load(ui_ble);
    }
}
#endif

// ---- 菜单几何：5 行，行高 32，间距 4 ----
#define MENU_X      16
#define MENU_W      (UIK_W - 2 * MENU_X)      // 288
#define MENU_Y0     56
#define MENU_ROW_H  32
#define MENU_GAP    4
#define MENU_STEP   (MENU_ROW_H + MENU_GAP)   // 36

void ui_Screen1_screen_init(void)
{
    ui_Screen1 = uik_screen_init();

    // 标题区（主菜单没有"返回"，顶栏只放标题）
    lv_obj_t * bar = lv_obj_create(ui_Screen1);
    lv_obj_set_size(bar, UIK_W, UIK_CONTENT_Y + 8);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(bar);
    lv_label_set_text(title, "ESP32-S3 TEST");
    lv_obj_set_style_text_font(title, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t * sub = lv_label_create(bar);
    lv_label_set_text(sub, LV_SYMBOL_LIST " Main Menu");
    lv_obj_set_style_text_font(sub, UIK_FONT_HINT, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(sub, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -2);

    // ---- 5 行菜单 ----
    // 1) Temp / RH
    ui_Button3 = uik_menu_row(ui_Screen1, MENU_X, MENU_Y0, MENU_W, MENU_ROW_H,
                              LV_SYMBOL_HOME "  Temp / RH", UIK_C_SURFACE,
                              go_dht11_cb, NULL);
    ui_Label2 = lv_obj_get_child(ui_Button3, 0);      // 行内文字标签，保持旧符号有效

    // 2) File Manager
    ui_Button2 = uik_menu_row(ui_Screen1, MENU_X, MENU_Y0 + MENU_STEP, MENU_W, MENU_ROW_H,
                              LV_SYMBOL_DIRECTORY "  File Manager", UIK_C_SURFACE,
                              go_file_cb, NULL);
    ui_Label5 = lv_obj_get_child(ui_Button2, 0);

    // 3) Serial Tool
    ui_Button5 = uik_menu_row(ui_Screen1, MENU_X, MENU_Y0 + 2 * MENU_STEP, MENU_W, MENU_ROW_H,
                              LV_SYMBOL_KEYBOARD "  Serial Tool", UIK_C_SURFACE,
                              go_serial_cb, NULL);
    ui_Label7 = lv_obj_get_child(ui_Button5, 0);

    // 4) WiFi
    ui_Button8 = uik_menu_row(ui_Screen1, MENU_X, MENU_Y0 + 3 * MENU_STEP, MENU_W, MENU_ROW_H,
                              LV_SYMBOL_WIFI "  WiFi", UIK_C_SURFACE,
                              go_wifi_cb, NULL);
    ui_Label12 = lv_obj_get_child(ui_Button8, 0);

    // 5) Bluetooth —— 用主色蓝突出；进入前记录来源屏，返回键才能回得来
#if ENABLE_BLE
    ui_ButtonBle = uik_menu_row(ui_Screen1, MENU_X, MENU_Y0 + 4 * MENU_STEP, MENU_W, MENU_ROW_H,
                                LV_SYMBOL_BLUETOOTH "  Bluetooth", UIK_C_PRIMARY,
                                go_ble_cb, NULL);
    ui_LabelBle = lv_obj_get_child(ui_ButtonBle, 0);
#endif

    // ---- 左下角版本号（原 ui_Label11）----
    ui_Label11 = uik_label(ui_Screen1, "lvgl v8.3.6", UIK_PAD, UIK_H - 16,
                           UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);

    uic_Screen1 = ui_Screen1;
}

void ui_Screen1_screen_destroy(void)
{
    if (ui_Screen1) lv_obj_del(ui_Screen1);

    uic_Screen1 = NULL;
    ui_Screen1 = NULL;
    ui_Button5 = NULL;
    ui_Label7 = NULL;
    ui_Button3 = NULL;
    ui_Label2 = NULL;
    ui_Label11 = NULL;
    ui_Button2 = NULL;
    ui_Label5 = NULL;
    ui_Button8 = NULL;
    ui_Label12 = NULL;
    ui_ButtonBle = NULL;
    ui_LabelBle = NULL;
}
