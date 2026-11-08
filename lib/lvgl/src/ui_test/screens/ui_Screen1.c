// ============================================================================
//  ui_Screen1.c - 主菜单
//
//  视觉结构（320x240）：
//      0..39    标题条：左边 "ESP32-S3" 20pt，右边 "LVGL 8.3.6" 10pt 弱化
//      40       1px 分隔线
//      44..238  6 行菜单，每行 29 高、间隔 4：
//                  [徽章]  Temp / RH                              ›
//      238..240 底部留白
//
//  相比旧版：
//    1) 每行左侧加了一个 25px 的图标徽章，按功能上色（琥珀/青绿/黄绿/蓝/靛紫）。
//       以前五行都是同一个灰底 + 同一档灰白图标，扫过去分不出谁是谁。
//    2) 标题条从"两行居中文字"改成"左对齐大标题 + 右侧版本号"：
//       菜单本身才是这屏的主体，标题没必要占两行居中。
//    3) 去掉了压在屏幕左下角的版本号 —— 它和第五行只差 7px，视觉上像是
//       菜单的一部分。版本号移到标题条右侧，作为背景信息存在。
//    4) 按下态改成提亮（见 uik_btn_states），点击有明确反馈。
//
//  对外符号保持不变（ui_Screen1 / ui_Button2/3/5/8 / ui_Label2/5/7/11/12），
//  这样 ui_events.h 里由 SquareLine 生成的 event 函数仍能正常链接。
// ============================================================================

#include "../ui.h"
#include "../ui_kit.h"
#include "ui_relay.h"    // ui_relay / ui_relay_screen_init
#if ENABLE_BLE
#include "ui_ble.h"      // ui_ble / ble_set_prev_screen
#endif

lv_obj_t * uic_Screen1;
lv_obj_t * ui_Screen1 = NULL;
lv_obj_t * ui_Button5 = NULL;      // Uart
lv_obj_t * ui_Label7 = NULL;
lv_obj_t * ui_Button3 = NULL;      // Temp
lv_obj_t * ui_Label2 = NULL;
lv_obj_t * ui_Label11 = NULL;      // 版本号（现在在标题条右侧）
lv_obj_t * ui_Button2 = NULL;      // File
lv_obj_t * ui_Label5 = NULL;
lv_obj_t * ui_Button8 = NULL;      // WiFi
lv_obj_t * ui_Label12 = NULL;

static lv_obj_t * ui_ButtonBle = NULL;    // BLE
static lv_obj_t * ui_LabelBle = NULL;
static lv_obj_t * ui_ButtonRelay = NULL;  // Relay（GPIO41 设备开关）
static lv_obj_t * ui_LabelRelay = NULL;

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
// Relay 屏和 wscan 一样是懒创建的：第一次点这行时才建。
static void go_relay_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_relay, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_relay_screen_init);
}

// 注意：本文件按 C 编译，**不能用 lambda**，回调必须是普通静态函数
#if ENABLE_BLE
static void go_ble_cb(lv_event_t * e)
{
    (void)e;
    if (ui_ble != NULL) {
        ble_set_prev_screen(lv_scr_act());   // 记住来源屏，BLE 屏的返回键才回得来
        uik_screen_load(ui_ble);             // 统一入口：加载后补一次整屏失效
    }
}
#endif

// ---- 菜单几何 --------------------------------------------------------------
// 标题条 40 + 分隔线 1 + 一个缝隙 3 = 44 起。
// ★ 6 行菜单（加了 Relay）：行高压到 29、缝隙仍是 4 → 步距 33，
//   44 + 5*33 + 29 = 238，距屏幕底还有 2px。
//   29 高的行放 14pt 标题（行高 16）绰绰有余；徽章由 uik_menu_row_icon
//   自动缩到 23（h-6），不会再顶满整行。
//   注意：UIK_ROW_H(35) 是"标准行高"，别的屏还在用，这里不动它，
//   主菜单用自己的 MENU_ROW_H。
#define MENU_X      UIK_PANEL_GAP                    // 4（= 左右边距）
#define MENU_W      (UIK_W - 2 * MENU_X)             // 312
#define TITLEBAR_H  40                               // 标题条高度
#define MENU_Y0     (TITLEBAR_H + UIK_PANEL_GAP)     // 44：标题条 + 分隔线 + 缝隙
#define MENU_ROW_H  29                               // 6 行版行高
#define MENU_STEP   (MENU_ROW_H + UIK_PANEL_GAP)     // 33
// Relay 的槽位：BLE 开着就是第 6 槽（下标 5），关了自动补到第 5 槽（下标 4）
#if ENABLE_BLE
#define RELAY_SLOT  5
#else
#define RELAY_SLOT  4
#endif

void ui_Screen1_screen_init(void)
{
    ui_Screen1 = uik_screen_init();

    // ---- 标题条 ----
    // 不用 uik_topbar()：主菜单没有"返回"，也不需要居中标题，
    // 用左对齐大标题 + 右侧版本号更省纵向空间。
    lv_obj_t * bar = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UIK_W, TITLEBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * title = lv_label_create(bar);
    lv_label_set_text(title, "ESP32-S3");
    lv_obj_set_style_text_font(title, UIK_FONT_BIG, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, UIK_PAD, 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    // 版本号：原来贴在屏幕左下角，和第五行只差 7px，看起来像菜单的一部分。
    // 现在放到标题条右侧，弱化成背景信息。
    ui_Label11 = lv_label_create(bar);
    lv_label_set_text(ui_Label11, "LVGL 8.3.6");
    lv_obj_set_style_text_font(ui_Label11, UIK_FONT_HINT, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Label11, lv_color_hex(UIK_C_TEXT_FAINT), LV_STATE_DEFAULT);
    lv_obj_align(ui_Label11, LV_ALIGN_RIGHT_MID, -UIK_PAD, 0);
    lv_obj_clear_flag(ui_Label11, LV_OBJ_FLAG_CLICKABLE);

    // 标题条下的分隔线
    uik_separator(ui_Screen1, 0, TITLEBAR_H, UIK_W);

    // ---- 6 行菜单 ----
    // 每行：左侧功能身份色的图标徽章 + 标题 + 右侧箭头（由 uik_menu_row_icon 画）
    ui_Button3 = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0, MENU_W, MENU_ROW_H,
                                   LV_SYMBOL_TINT, "Temp / RH",
                                   UIK_ACCENT_TEMP, UIK_C_SURFACE,
                                   go_dht11_cb, NULL, &ui_Label2);

    ui_Button2 = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0 + MENU_STEP, MENU_W, MENU_ROW_H,
                                   LV_SYMBOL_DIRECTORY, "File Manager",
                                   UIK_ACCENT_FILE, UIK_C_SURFACE,
                                   go_file_cb, NULL, &ui_Label5);

    ui_Button5 = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0 + 2 * MENU_STEP, MENU_W, MENU_ROW_H,
                                   LV_SYMBOL_KEYBOARD, "Serial Tool",
                                   UIK_ACCENT_SERIAL, UIK_C_SURFACE,
                                   go_serial_cb, NULL, &ui_Label7);

    ui_Button8 = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0 + 3 * MENU_STEP, MENU_W, MENU_ROW_H,
                                   LV_SYMBOL_WIFI, "WiFi",
                                   UIK_ACCENT_WIFI, UIK_C_SURFACE,
                                   go_wifi_cb, NULL, &ui_Label12);

    // 5) Bluetooth —— 和上面 4 行同底色（UIK_C_SURFACE）。
    //    原来这里写的是 UIK_C_PRIMARY（主色蓝），于是主菜单上唯一一个蓝按钮
    //    显得像"被选中/已激活"，其实它和其它行一样只是一级入口。
    //    主色只用在真正的主操作上（例如各屏的 Send 按钮）。
#if ENABLE_BLE
    ui_ButtonBle = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0 + 4 * MENU_STEP, MENU_W, MENU_ROW_H,
                                     LV_SYMBOL_BLUETOOTH, "Bluetooth",
                                     UIK_ACCENT_BLE, UIK_C_SURFACE,
                                     go_ble_cb, NULL, &ui_LabelBle);
#endif

    // 6) Relay —— 徽章用电源符号（0xF011，字形表里有），身份色是芯片暖红：
    //    它控制的是实体设备，色相上和"只读数据"的几行区分开。
    //    槽位跟着 BLE 开关联动：BLE 关掉时 Relay 自动补到第 5 槽，
    //    菜单中间不会留一条空带。
    ui_ButtonRelay = uik_menu_row_icon(ui_Screen1, MENU_X, MENU_Y0 + RELAY_SLOT * MENU_STEP, MENU_W, MENU_ROW_H,
                                       LV_SYMBOL_POWER, "Relay / Device",
                                       UIK_ACCENT_CHIP, UIK_C_SURFACE,
                                       go_relay_cb, NULL, &ui_LabelRelay);

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
    ui_ButtonRelay = NULL;
    ui_LabelRelay = NULL;
}