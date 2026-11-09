// ============================================================================
//  ui_Screen1.c - 主菜单（可滚动列表）
//
//  视觉结构（320x240）：
//      0..39    标题条：左边 "ESP32-S3" 20pt，右边 "LVGL 8.3.6" 10pt 弱化
//      40       1px 分隔线
//      44..236  ★ 可滚动菜单容器：行高 35、行距 4，右侧 6px 滚动条槽。
//               菜单条目已超过一屏（8 行 = 308px > 183px），拖动滚动，
//               滚动条只在滚动时出现（AUTO）。第 5 行被下沿裁掉一截，
//               这本身就是"还能往下滚"的视觉提示。
//      236..240 底部留白
//
//  为什么从"固定 6 行"改成滚动列表：
//    1) 加到第 7 行（LED Strip）后 240px 已经放不下 —— 继续压行高就要
//       牺牲触摸目标（29px 已经贴近下限，再压就难点了）。
//    2) 滚动列表对"菜单会继续长"是开放的：以后加第 8、9 项不用动布局，
//       固定行高方案每加一行都要全菜单重算。
//    3) 行高恢复标准 UIK_ROW_H(35)，徽章恢复 27，和其它屏的节奏一致。
//
//  对外符号保持不变（ui_Screen1 / ui_Button2/3/5/8 / ui_Label2/5/7/11/12），
//  这样 ui_events.h 里由 SquareLine 生成的 event 函数仍能正常链接。
//  注意：菜单行现在是滚动容器的子对象，不再是屏幕直挂 —— 通过句柄访问的代码
//  不受影响，但 lv_obj_get_parent(ui_Button3) 拿到的是容器而不是屏幕。
// ============================================================================

#include "../ui.h"
#include "../ui_kit.h"
#include "ui_relay.h"    // ui_relay / ui_relay_screen_init
#include "ui_led.h"      // ui_led / ui_led_screen_init
#include "ui_rfmon.h"    // ui_rfmon / ui_rfmon_screen_init
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
static lv_obj_t * ui_ButtonLed = NULL;    // LED Strip（WS2812 / GPIO39）
static lv_obj_t * ui_LabelLed = NULL;
static lv_obj_t * ui_ButtonRfmon = NULL;  // RF Monitor（802.11 被动监听）
static lv_obj_t * ui_LabelRfmon = NULL;
static lv_obj_t * menu_cont = NULL;       // 可滚动菜单容器

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
// Relay / wscan / LED 屏都是懒创建的：第一次点这行时才建。
static void go_relay_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_relay, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_relay_screen_init);
}
static void go_led_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_led, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_led_screen_init);
}
static void go_rfmon_cb(lv_event_t * e)
{
    (void)e;
    _ui_screen_change(&ui_rfmon, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_rfmon_screen_init);
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
// 标题条 40 + 分隔线 1 + 一个缝隙 3 = 44 起；容器高 183，到 227 为止。
// 行高恢复标准 UIK_ROW_H(35)，行距 4：8 行内容高 = 8*35 + 7*4 = 308 > 183，滚动。
// ★ 183 不是随手取的：183 = 4*39 + 27，第 5 行（Bluetooth）刚好被下沿裁掉
//   8px —— "被切了一半的行"就是最直白的"下面还有，可以滚"提示。
//   若容器高取 192，前 5 行会【完整】放下（4*39+35=191≤192），屏幕上没有任何
//   滚动线索，用户根本不知道后面还有几行（Relay / LED / RF Monitor 全被藏没）。
#define MENU_X      UIK_PANEL_GAP                    // 4（= 左右边距）
#define TITLEBAR_H  40                               // 标题条高度
#define MENU_Y0     (TITLEBAR_H + UIK_PANEL_GAP)     // 44
#define MENU_H      183                              // 容器可视高：4 行整 + 第 5 行裁 8px
#define MENU_W      (UIK_W - 2 * MENU_X)             // 312：容器宽
// 行宽 = 容器内容宽：容器 pad_right 6 留给滚动条槽，行不压在滚动条下面
#define MENU_ROW_W  (MENU_W - 6)                     // 306

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

    // ---- 可滚动菜单容器 ----
    // 底色和屏幕一致（BG）：容器只是"滚动视口"，不该看起来像又一层卡片。
    // pad_right 6 是滚动条槽：行宽 = 容器内容宽，永远不压在滚动条下面。
    menu_cont = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(menu_cont);
    lv_obj_set_size(menu_cont, MENU_W, MENU_H);
    lv_obj_align(menu_cont, LV_ALIGN_TOP_LEFT, MENU_X, MENU_Y0);
    lv_obj_set_style_bg_color(menu_cont, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(menu_cont, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(menu_cont, UIK_PANEL_GAP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(menu_cont, 6, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(menu_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(menu_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(menu_cont, LV_SCROLLBAR_MODE_AUTO);
    // 滚动条：4px 宽、抬起色、圆角，只在滚动时出现（AUTO）
    lv_obj_set_style_width(menu_cont, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(menu_cont, lv_color_hex(UIK_C_NEUTRAL_HI), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(menu_cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(menu_cont, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(menu_cont, 1, LV_PART_SCROLLBAR);

    // ---- 菜单行（flex 列，行顺序即视觉顺序）----
    // 每行：左侧功能身份色的图标徽章 + 标题 + 右侧箭头（由 uik_menu_row_icon 画）。
    // flex 父容器会接管行的位置，uik_menu_row_icon 里的 x/y/align 参数被忽略，
    // 传 0 即可；行高恢复标准 UIK_ROW_H(35)。
    ui_Button3 = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                   LV_SYMBOL_TINT, "Temp / RH",
                                   UIK_ACCENT_TEMP, UIK_C_SURFACE,
                                   go_dht11_cb, NULL, &ui_Label2);

    ui_Button2 = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                   LV_SYMBOL_DIRECTORY, "File Manager",
                                   UIK_ACCENT_FILE, UIK_C_SURFACE,
                                   go_file_cb, NULL, &ui_Label5);

    ui_Button5 = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                   LV_SYMBOL_KEYBOARD, "Serial Tool",
                                   UIK_ACCENT_SERIAL, UIK_C_SURFACE,
                                   go_serial_cb, NULL, &ui_Label7);

    ui_Button8 = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                   LV_SYMBOL_WIFI, "WiFi",
                                   UIK_ACCENT_WIFI, UIK_C_SURFACE,
                                   go_wifi_cb, NULL, &ui_Label12);

    // Bluetooth —— 和其它行同底色（UIK_C_SURFACE）。主色只用在真正的主操作上。
#if ENABLE_BLE
    ui_ButtonBle = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                     LV_SYMBOL_BLUETOOTH, "Bluetooth",
                                     UIK_ACCENT_BLE, UIK_C_SURFACE,
                                     go_ble_cb, NULL, &ui_LabelBle);
#endif

    // Relay —— 徽章用电源符号（0xF011），身份色芯片暖红：它控制实体设备。
    ui_ButtonRelay = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                       LV_SYMBOL_POWER, "Relay / Device",
                                       UIK_ACCENT_CHIP, UIK_C_SURFACE,
                                       go_relay_cb, NULL, &ui_LabelRelay);

    // LED Strip —— 闪电符号（0xF0E7），身份色用浅青（HUM）：
    // 菜单里还没有这一相，和暖红(Relay)/靛紫(BLE)都拉得开。
    ui_ButtonLed = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                     LV_SYMBOL_CHARGE, "LED Strip",
                                     UIK_ACCENT_HUM, UIK_C_SURFACE,
                                     go_led_cb, NULL, &ui_LabelLed);

    // RF Monitor —— 眼睛符号（0xF06E），品红身份色。
    // 说明：这一屏是**被动接收**的 802.11 管理帧分析（deauth 源 / AP 表 /
    // probe 统计），只收不发。发射类功能（deauth 注入、beacon 洪泛、
    // Sour Apple 之类）不在这里，见 rf_monitor.cpp 顶部说明。
    ui_ButtonRfmon = uik_menu_row_icon(menu_cont, 0, 0, MENU_ROW_W, UIK_ROW_H,
                                       LV_SYMBOL_EYE_OPEN, "RF Monitor",
                                       UIK_ACCENT_MON, UIK_C_SURFACE,
                                       go_rfmon_cb, NULL, &ui_LabelRfmon);

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
    ui_ButtonLed = NULL;
    ui_LabelLed = NULL;
    ui_ButtonRfmon = NULL;
    ui_LabelRfmon = NULL;
    menu_cont = NULL;
}