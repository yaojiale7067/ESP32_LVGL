#ifndef UI_WIFI_H
#define UI_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// 这些变量名必须与 .c 文件中的定义完全一致
extern lv_obj_t * ui_wifi;
extern lv_obj_t * ui_WifiList;
extern lv_obj_t * ui_PasswordArea;
extern lv_obj_t * ui_ScanButton;
extern lv_obj_t * ui_ConnectButton;
extern lv_obj_t * ui_BackButton;
extern lv_obj_t * ui_StatusLabel;

void ui_wifi_screen_init(void);
void ui_wifi_screen_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif