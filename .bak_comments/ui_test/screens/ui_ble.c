// ============================================================================
//  ui_ble.c - 蓝牙(BLE) 串口屏：连接状态 + 数据接收 + 数据发送
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]        BLE UART        [Clear]
//      40..56    状态行：● Connected  对端MAC  RX/TX
//      58..202   接收区（凹陷底色，绿字，可滚动）
//      206..236  发送行：[输入框 212x30]  [Send 92x30]
//
//  键盘弹出时（占底部 120px）整块重排，接收区与发送行一起上移：
//      40..82    接收区（压到两行高，只留最新内容）—— 此时状态行隐藏
//      86..116   发送行，紧贴键盘上沿
//  ★ 旧版发送行固定 y=174，键盘一弹（120..239）就把整行连输入框一起盖住，
//    打字时根本看不到自己输入的内容。现在输入框永远在键盘上方。
//
//  相比旧版：
//    1) 左右留白从 10 改成 ui_kit 的 4，和其它四屏对齐（以前 BLE 屏往里缩，
//       从主菜单点进来会看到内容"跳"一下）。
//    2) 顶栏改用 uik_topbar()，返回/标题/右侧按钮的位置与其它屏完全一致。
//    3) 状态行的 ● 用颜色区分：绿=已连接、黄=广播中、红=未启动。
//    4) 接收区换成 uik 的凹陷底色（0x0F0F0F）+ 更柔和的绿色（0x79D08A），
//       原来的 0x88FF88 在近黑底上会溢光。占位文字说明"等待数据"。
//    5) 发送框/按钮统一 uik_ta()/uik_btn()，圆角、内边距、按下反馈全屏一致。
//    6) 键盘套 uik_keyboard() 皮肤，键帽和本工程配色统一（默认主题偏蓝灰）。
// ============================================================================

#include "ui_ble.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "../ui_kit.h"

// ============================================================================
//  整个 BLE 屏由 ENABLE_BLE 控制（见 platformio.ini）。
//  关闭时本文件编译为空 —— 否则这里对 ui_ble_on_send_requested() 的调用
//  会指向已被裁掉的 ble_uart.cpp，链接期报 undefined reference。
// ============================================================================
#if ENABLE_BLE

lv_obj_t * ui_ble = NULL;
lv_obj_t * ui_ble_back_btn = NULL;
lv_obj_t * ui_ble_status_label = NULL;
lv_obj_t * ui_ble_rx_ta = NULL;
lv_obj_t * ui_ble_send_ta = NULL;
lv_obj_t * ui_ble_send_btn = NULL;
lv_obj_t * ui_ble_clear_btn = NULL;

static lv_obj_t * ble_keyboard = NULL;

// 键盘弹起 / 收起时两套坐标，见 ui_ble.h 的注释
static void ble_layout_keyboard_up(bool up)
{
    if (ui_ble_rx_ta) {
        lv_obj_align(ui_ble_rx_ta, LV_ALIGN_TOP_LEFT, UIK_LEFT,
                     up ? BLE_RX_Y_KB : BLE_RX_Y);
        lv_obj_set_height(ui_ble_rx_ta, up ? BLE_RX_H_KB : BLE_RX_H);
    }
    if (ui_ble_send_ta) {
        lv_obj_align(ui_ble_send_ta, LV_ALIGN_TOP_LEFT, UIK_LEFT,
                     up ? BLE_COMP_Y_KB : BLE_COMP_Y);
    }
    if (ui_ble_send_btn) {
        lv_obj_align(ui_ble_send_btn, LV_ALIGN_TOP_RIGHT, -UIK_PAD,
                     up ? BLE_COMP_Y_KB : BLE_COMP_Y);
    }
    // 键盘占掉了状态行那块地方，收起它（否则会压在接收区上面）
    if (ui_ble_status_label) {
        if (up) lv_obj_add_flag(ui_ble_status_label, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_clear_flag(ui_ble_status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// 事件
// ---------------------------------------------------------------------------
// 只有"发送框"获得焦点才弹键盘；接收区不该弹（它是只读的）
static void ta_focus_cb(lv_event_t * e)
{
    if (ble_keyboard == NULL) return;
    if (lv_event_get_target(e) != ui_ble_send_ta) return;
    lv_keyboard_set_textarea(ble_keyboard, ui_ble_send_ta);
    ble_layout_keyboard_up(true);
    lv_obj_clear_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 失焦就把键盘收起来，否则会一直占着底部 120px
static void ta_defocus_cb(lv_event_t * e)
{
    (void)e;
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
    ble_layout_keyboard_up(false);
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
    ble_layout_keyboard_up(false);
    if (ble_prev_scr != NULL) {
        uik_screen_load(ble_prev_scr);  // 回到进来的那个屏（走统一入口）
        return;
    }
    // 兜底：和其它屏的返回键一致，都回主菜单。
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen1_screen_init);
}

// 接收区清空：业务层的显示缓冲由 LVGL_Demos.cpp 里的 ui_ble_on_clear_requested() 负责
static void clear_btn_cb(lv_event_t * e)
{
    (void)e;
    if (ui_ble_rx_ta) lv_textarea_set_text(ui_ble_rx_ta, "");
    ui_ble_on_clear_requested();
}

// 发送按钮的真实逻辑在 LVGL_Demos.cpp 里绑定（那边会调用 bleuart_send）
static void send_btn_ui_cb(lv_event_t * e) { (void)e; }

// 键盘：按 √ = 发送并收起；按 × = 只收起
static void kb_ready_cb(lv_event_t * e)
{
    (void)e;
    ui_ble_on_send_requested();
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
    ble_layout_keyboard_up(false);
}

static void kb_cancel_cb(lv_event_t * e)
{
    (void)e;
    if (ble_keyboard) lv_obj_add_flag(ble_keyboard, LV_OBJ_FLAG_HIDDEN);
    ble_layout_keyboard_up(false);
}

// ---------------------------------------------------------------------------
void ui_ble_screen_init(void)
{
    ui_ble = uik_screen_init();

    // ---------------- 顶栏 ----------------
    // 右侧 "Clear" 用 DANGER 红：它是不可撤销的破坏性操作（清掉接收记录）。
    uik_topbar(ui_ble, "BLE UART", "Clear", back_btn_cb, clear_btn_cb,
               &ui_ble_back_btn, &ui_ble_clear_btn);
    // uik_topbar 建成 PRIMARY 蓝，这里改成红
    if (ui_ble_clear_btn != NULL) uik_btn_states(ui_ble_clear_btn, UIK_C_DANGER);

    // ---------------- 状态行 ----------------
    ui_ble_status_label = lv_label_create(ui_ble);
    lv_obj_set_width(ui_ble_status_label, UIK_CONTENT_W);
    lv_obj_set_style_text_font(ui_ble_status_label, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_ble_status_label, lv_color_hex(UIK_C_ERR_TEXT), LV_STATE_DEFAULT);
    lv_label_set_long_mode(ui_ble_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui_ble_status_label, LV_SYMBOL_BULLET " BLE off");
    lv_obj_align(ui_ble_status_label, LV_ALIGN_TOP_LEFT, UIK_LEFT, BLE_STATUS_Y);

    // ---------------- 接收区（只读、可滚动） ----------------
    // 用 uik_ta_readonly 建：凹陷底色 + 绿字 + 无焦点环（详见 ui_kit.c 注释）
    ui_ble_rx_ta = uik_ta_readonly(ui_ble, UIK_LEFT, BLE_RX_Y, UIK_CONTENT_W, BLE_RX_H,
                                   "Waiting for data...");
    lv_obj_set_style_text_font(ui_ble_rx_ta, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ble_rx_ta, 5, LV_STATE_DEFAULT);
    // 不要给接收区设 max_length：lv_textarea_set_text() 一旦发现 max_length
    // 非 0，就会把文本一个字符一个字符地 add_char 进去，到达上限后**丢掉后面
    // 的内容**——也就是保留最老的、丢掉最新的，接收区看起来就永远停在旧内容上。
    // 接收缓冲本身已经由 ble_uart.cpp 的 rx_disp[BLE_RX_MAX_CHARS+1] 限长，
    // 这里不设上限，set_text 走 lv_label_set_text 快路径，一次性替换整段文本。
    //
    // 接收区也接上焦点回调：LVGL 的文本框只有在"活动输入组"里才能被拖动滚动，
    // 而键盘只跟发送框绑定，所以这里点一下不会弹出键盘。
    lv_obj_add_event_cb(ui_ble_rx_ta, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_ble_rx_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    // ---------------- 发送行 ----------------
    ui_ble_send_ta = uik_ta(ui_ble, UIK_LEFT, BLE_COMP_Y, BLE_TA_W, BLE_COMP_H,
                            "Data to send...", true);
    lv_textarea_set_max_length(ui_ble_send_ta, 200);
    lv_obj_add_event_cb(ui_ble_send_ta, ta_focus_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ui_ble_send_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    ui_ble_send_btn = uik_btn(ui_ble, LV_SYMBOL_UPLOAD " Send",
                              UIK_W - UIK_PAD - BLE_SEND_W, BLE_COMP_Y,
                              BLE_SEND_W, BLE_COMP_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(ui_ble_send_btn, send_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 键盘（隐藏，聚焦发送框时弹出） ----------------
    ble_keyboard = lv_keyboard_create(ui_ble);
    lv_obj_set_size(ble_keyboard, UIK_W, BLE_KB_H);
    lv_obj_align(ble_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(ble_keyboard);                 // 换成本工程的配色
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

#endif  // ENABLE_BLE —— 关闭时本文件为空