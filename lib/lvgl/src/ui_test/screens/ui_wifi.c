#include "ui_wifi.h"

// 定义在头文件中声明的全局控件变量
lv_obj_t * ui_wifi = NULL;
lv_obj_t * ui_WifiList = NULL;
lv_obj_t * ui_PasswordArea = NULL;
lv_obj_t * ui_ScanButton = NULL;
lv_obj_t * ui_ConnectButton = NULL;
lv_obj_t * ui_BackButton = NULL;
lv_obj_t * ui_StatusLabel = NULL;

void ui_wifi_screen_init(void) {
    ui_wifi = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_wifi, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);

    // 标题
    lv_obj_t * title = lv_label_create(ui_wifi);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // 扫描按钮
    ui_ScanButton = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_ScanButton, 80, 30);
    lv_obj_align(ui_ScanButton, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_t * scan_label = lv_label_create(ui_ScanButton);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    // 返回按钮
    ui_BackButton = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_BackButton, 80, 30);
    lv_obj_align(ui_BackButton, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_t * back_label = lv_label_create(ui_BackButton);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    // WiFi 列表容器
    lv_obj_t * list_cont = lv_obj_create(ui_wifi);
    lv_obj_set_size(list_cont, 300, 120);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_cont, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);
    
    ui_WifiList = lv_list_create(list_cont);
    lv_obj_set_size(ui_WifiList, 280, LV_SIZE_CONTENT);
    lv_obj_center(ui_WifiList);
    lv_obj_set_style_bg_color(ui_WifiList, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);

    // 密码输入框
    ui_PasswordArea = lv_textarea_create(ui_wifi);
    lv_obj_set_size(ui_PasswordArea, 200, 40);
    lv_obj_align(ui_PasswordArea, LV_ALIGN_BOTTOM_LEFT, 10, -50);
    lv_textarea_set_placeholder_text(ui_PasswordArea, "Password");
    lv_textarea_set_password_mode(ui_PasswordArea, true);
    lv_obj_set_style_bg_color(ui_PasswordArea, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_PasswordArea, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    // 连接按钮
    ui_ConnectButton = lv_btn_create(ui_wifi);
    lv_obj_set_size(ui_ConnectButton, 80, 40);
    lv_obj_align(ui_ConnectButton, LV_ALIGN_BOTTOM_RIGHT, -10, -50);
    lv_obj_t * conn_label = lv_label_create(ui_ConnectButton);
    lv_label_set_text(conn_label, "Connect");
    lv_obj_center(conn_label);

    // 状态标签
    ui_StatusLabel = lv_label_create(ui_wifi);
    lv_obj_set_width(ui_StatusLabel, 300);
    lv_obj_align(ui_StatusLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_label_set_text(ui_StatusLabel, "");
    lv_obj_set_style_text_align(ui_StatusLabel, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
}

void ui_wifi_screen_destroy(void) {
    if (ui_wifi) lv_obj_del(ui_wifi);
    ui_wifi = NULL;
    ui_WifiList = NULL;
    ui_PasswordArea = NULL;
    ui_ScanButton = NULL;
    ui_ConnectButton = NULL;
    ui_BackButton = NULL;
    ui_StatusLabel = NULL;
}