#ifndef UI_WIFI_H
#define UI_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern lv_obj_t * ui_wifi;
extern lv_obj_t * ui_wifi_list;
extern lv_obj_t * ui_wifi_password_ta;
extern lv_obj_t * ui_wifi_scan_btn;
extern lv_obj_t * ui_wifi_connect_btn;
extern lv_obj_t * ui_wifi_back_btn;
extern lv_obj_t * ui_wifi_status_label;

void ui_wifi_screen_init(void);
void ui_wifi_screen_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif