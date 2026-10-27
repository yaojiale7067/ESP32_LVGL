// display_port.h - 显示驱动移植层（LCD + 触摸 + 显示缓冲）
//
// 改代码前必读：
//  1) 显示缓冲【必须】放 PSRAM，不能用内部 RAM。内部 RAM 是 BLE/WiFi 协议栈的
//     生命线，被占掉后 WiFi 会直接初始化失败（alloc pp wdev funcs fail）。
//     宁可屏幕有瑕疵，也不能把射频饿死。
//  2) 缓冲保持整屏（DISP_BUF_DIV = 1）：实测 1/4 屏(60行) 撕裂反而比整屏更严重，
//     1/10 屏(24行) 撕裂"还行"但界面略卡。根治只能接面板 TE 脚做同步。
//  3) full_refresh 必须保持 0，打开会让撕裂更严重。
#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// 横屏 320x240，rotation = 1
#define DISP_W 320
#define DISP_H 240

// TFT 与 SD 共用 SPI（SCLK=12 MOSI=11 MISO=13，仅 CS 不同），
// 所有对 TFT 的直接写操作都必须持有这把锁。锁的定义与创建在 display_port.cpp。
extern SemaphoreHandle_t g_spiBusMutex;

#define DISP_BUS_LOCK()                                                                \
    do {                                                                               \
        if (g_spiBusMutex) xSemaphoreTakeRecursive(g_spiBusMutex, portMAX_DELAY);      \
    } while (0)

#define DISP_BUS_UNLOCK()                                                              \
    do {                                                                               \
        if (g_spiBusMutex) xSemaphoreGiveRecursive(g_spiBusMutex);                     \
    } while (0)

// 内部顺序：LCD 初始化 -> 旋转/清屏 -> touch_init -> lv_init -> 分配缓冲 -> 注册驱动。
// 调用它之前不要碰 LVGL 或 TFT。
void disp_port_init_all(void);

// 直接访问 LCD（已内建总线锁，调用方不需要再加锁）
void     disp_fill_screen(uint16_t color);
void     disp_push_image(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t* data);
uint16_t disp_width(void);
uint16_t disp_height(void);

// 触摸当前是否按下。业务层用这个，不要直接 include touch.h。
bool disp_touch_pressed(void);

// TJpg_Decoder 的输出回调适配（内部已加总线锁）
bool disp_tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

// LVGL 日志输出回调，仅当 LV_USE_LOG 且等级非 NONE 时存在
void disp_lv_log_cb(const char* buf);

#endif  // DISPLAY_PORT_H
