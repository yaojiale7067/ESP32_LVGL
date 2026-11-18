// ============================================================================
//  ui_relay.c - 继电器（设备开关）控制屏
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]        Relay Control
//      38..154   状态卡片：
//                  ┌────────────────────────────────────┐
//                  │▔▔▔▔ 琥珀色条 ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔│
//                  │  RELAY • GPIO 41                    │
//                  │                                     │
//                  │       OFF            [ ○══════ ]    │ ← 大字 + 大开关
//                  │       ON（绿）                      │
//                  └────────────────────────────────────┘
//      158..208  信息卡片：有效电平 / 串口命令说明
//      212..239  状态条
//
//  设计取舍：
//    1) 主控件用 lv_switch 而不是按钮对（ON/OFF 两个按钮）：
//       开关的当前状态是"可见的"（旋钮在哪边一眼看到），
//       按钮对永远只有一个亮，容易误判。
//    2) 开关做大（96x48）：这是本屏唯一的操作，而且操作的是**实体设备**，
//       误触有实际后果。目标越大，越不容易点错。
//    3) 大字状态放左边、开关放右边，分开但同一行：
//       状态是"读"的信息，开关是"写"的控件，各自留足空间不打架。
//    4) 颜色语义：OFF = 中性灰，ON = 成功绿。
//       继电器"通电"在多数场景是正常工作态，不是危险动作，
//       用绿色；红色留给真正危险的确认。
//    5) 信息卡片写清**有效电平**：市售继电器模块大多是低电平触发
//       （IN 拉低吸合），也有高电平触发的。接线的人必须知道
//       固件用的是哪种，否则会接反。
// ============================================================================

#include "ui_relay.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "../ui_kit.h"

lv_obj_t * ui_relay = NULL;
lv_obj_t * ui_relay_back_btn = NULL;
lv_obj_t * ui_relay_sw = NULL;
lv_obj_t * ui_relay_state_lbl = NULL;
lv_obj_t * ui_relay_status_label = NULL;

static lv_obj_t * relay_card = NULL;        // 状态卡片（色条要按它内容宽）
static lv_obj_t * relay_state_dot = NULL;   // 状态卡片标题旁的小圆点（可选指示）

// 卡片内容宽：uik_panel pad_all=4 + 1px 边框 -> 宽 - 10
#define RL_CARD_CW  (UIK_CONTENT_W - 10)    // 302
#define RL_INFO_CW  (UIK_CONTENT_W - 10)    // 302

// 大开关尺寸
#define RL_SW_W     96
#define RL_SW_H     48

// ---------------------------------------------------------------------------
//  把界面的"开关位置 + 大字 + 颜色"同步到给定状态。
//  ★ 用 lv_obj_add_state / clear_state 而不是 lv_switch 的 toggle：
//    toggle 会翻转，而这里要的是"对齐到 on"，语义不同。
//  ★ 程序化改状态不会触发 VALUE_CHANGED（那个事件只在用户交互时发），
//    所以 set_state -> 开关位置变 -> 不会再回调 on_toggle_requested，不会打乒乓。
// ---------------------------------------------------------------------------
static void relay_apply_state(bool on)
{
    if (ui_relay_sw != NULL) {
        if (on) lv_obj_add_state(ui_relay_sw, LV_STATE_CHECKED);
        else    lv_obj_clear_state(ui_relay_sw, LV_STATE_CHECKED);
    }
    if (ui_relay_state_lbl != NULL) {
        lv_label_set_text(ui_relay_state_lbl, on ? "ON" : "OFF");
        lv_obj_set_style_text_color(ui_relay_state_lbl,
                                    lv_color_hex(on ? UIK_C_OK_TEXT : UIK_C_TEXT_DIM),
                                    LV_STATE_DEFAULT);
    }
}

// ---------------------------------------------------------------------------
static void back_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen1_screen_init);
}

// 用户拨动开关：只把"用户想要的状态"报给业务层，
// 界面本身的同步交给 ui_relay_set_state()（业务层写完 GPIO 后回调）。
// 这样"界面显示的"永远等于"硬件真实的"，不会各说各话。
static void switch_value_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * sw = lv_event_get_target(e);
    bool want = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_relay_on_toggle_requested(want);
}

// ---------------------------------------------------------------------------
void ui_relay_screen_init(void)
{
    ui_relay = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_relay, "Relay Control", NULL, back_btn_ui_cb, NULL,
               &ui_relay_back_btn, NULL);

    // ---- 状态卡片 ----
    relay_card = uik_panel(ui_relay, UIK_LEFT, RL_CARD_Y, UIK_CONTENT_W, RL_CARD_H);
    uik_accent_bar(relay_card, 0, 0, RL_CARD_CW, UIK_ACCENT_TEMP);

    // 标题：RELAY • GPIO 41
    lv_obj_t * title = uik_label(relay_card, "RELAY " LV_SYMBOL_BULLET " GPIO 41",
                                 0, 0, UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 8);

    // 大字状态（左）+ 大开关（右），同一水平带
    // 卡片内容区高 = 116 - 10 = 106；标题占 0..19，剩 19..106 给这行控件。
    ui_relay_state_lbl = uik_label(relay_card, "OFF", 0, 0, UIK_C_TEXT_DIM,
                                   UIK_FONT_NUM, LV_TEXT_ALIGN_LEFT);
    // 40pt 字号行高约 44；放在剩余带（19..106，中心 62）里略偏下
    lv_obj_align(ui_relay_state_lbl, LV_ALIGN_LEFT_MID, 26, 14);

    ui_relay_sw = lv_switch_create(relay_card);
    lv_obj_set_size(ui_relay_sw, RL_SW_W, RL_SW_H);
    lv_obj_align(ui_relay_sw, LV_ALIGN_RIGHT_MID, -22, 14);
    lv_obj_add_event_cb(ui_relay_sw, switch_value_cb, LV_EVENT_VALUE_CHANGED, NULL);
    // 开关配色跟 ui_kit：轨道关=中性灰，开=成功绿，旋钮=亮白
    lv_obj_set_style_bg_color(ui_relay_sw, lv_color_hex(UIK_C_NEUTRAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_relay_sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_relay_sw, lv_color_hex(UIK_C_SUCCESS),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(ui_relay_sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui_relay_sw, lv_color_hex(UIK_C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ui_relay_sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(ui_relay_sw, 0, LV_PART_KNOB);

    // ---- 信息卡片 ----
    lv_obj_t * info = uik_panel(ui_relay, UIK_LEFT, RL_INFO_Y, UIK_CONTENT_W, RL_INFO_H);
    lv_obj_t * info_l1 = uik_label(info, "Wiring: IN -> GPIO41", 0, 0,
                                   UIK_C_TEXT_DIM, UIK_FONT_BODY, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(info_l1, LV_ALIGN_TOP_LEFT, 2, 4);
    lv_obj_t * info_l2 = uik_label(info,
        "Serial: relay on | relay off | relay status", 0, 0,
        UIK_C_TEXT_FAINT, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(info_l2, LV_ALIGN_TOP_LEFT, 2, 22);

    // ---- 状态条 ----
    ui_relay_status_label = uik_status_bar(ui_relay);
    lv_label_set_text(ui_relay_status_label, "Relay output on GPIO 41");

    // 建屏后把界面同步到硬件当前状态（业务层读 GPIO）
    relay_apply_state(ui_relay_get_hw_state());

    (void)relay_state_dot;   // 预留，未用
}

// ---------------------------------------------------------------------------
void ui_relay_set_state(bool on)
{
    relay_apply_state(on);
    if (ui_relay_status_label != NULL) {
        lv_label_set_text(ui_relay_status_label,
                          on ? "Relay ON  " LV_SYMBOL_BULLET "  GPIO41 = active"
                             : "Relay OFF  " LV_SYMBOL_BULLET "  GPIO41 = idle");
        lv_obj_set_style_text_color(ui_relay_status_label,
                                    lv_color_hex(on ? UIK_C_OK_TEXT : UIK_C_TEXT_DIM),
                                    LV_STATE_DEFAULT);
    }
}

// ---------------------------------------------------------------------------
void ui_relay_screen_destroy(void)
{
    if (ui_relay) lv_obj_del(ui_relay);
    ui_relay = NULL;
    ui_relay_back_btn = NULL;
    ui_relay_sw = NULL;
    ui_relay_state_lbl = NULL;
    ui_relay_status_label = NULL;
    relay_card = NULL;
    relay_state_dot = NULL;
}
