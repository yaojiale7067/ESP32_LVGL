// ui_led.c - WS2812 灯带控制屏

#include "ui_led.h"
#include "../ui.h"
#include "../ui_kit.h"

#include <stdlib.h>      // atoi

lv_obj_t * ui_led = NULL;
lv_obj_t * ui_led_back_btn = NULL;
lv_obj_t * ui_led_status_label = NULL;

static lv_obj_t * led_card = NULL;
static lv_obj_t * led_accent = NULL;
static lv_obj_t * led_sl[4];              // HUE / BRIGHT / SPEED 滑块；[COUNT] 恒为 NULL
static lv_obj_t * led_val[4];             // 对应数值标签；[COUNT] 恒为 NULL
static lv_obj_t * led_count_ta = NULL;
static lv_obj_t * led_kb = NULL;
static int32_t  led_count_shown = -1;     // 输入框当前回显的数量

// ---------------------------------------------------------------------------
static void back_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen1_screen_init);
}

// 滑块事件：把"哪个控件 + 新值"报给业务层。
static void led_slider_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    int what = (int)(uintptr_t)lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value((lv_obj_t *)lv_event_get_current_target(e));
    ui_led_on_change(what, v);
}

// ---- 数字键盘：弹出 / 收起 / 提交 -----------------------------------------
static void led_count_commit(void)
{
    if (led_count_ta == NULL) return;
    const char * txt = lv_textarea_get_text(led_count_ta);
    if (txt != NULL && txt[0] != '\0') {
        int v = atoi(txt);
        ui_led_on_change(UI_LED_COUNT, v);
        return;
    }
    ui_led_on_change(UI_LED_COUNT, led_count_shown);
}

static void led_kb_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        led_count_commit();
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (led_kb) lv_obj_add_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
        if (code == LV_EVENT_CANCEL && led_count_shown >= 0) {
            char buf[8];
            lv_snprintf(buf, sizeof(buf), "%d", (int)led_count_shown);
            lv_textarea_set_text(led_count_ta, buf);
        }
        if (ui_led_status_label) lv_obj_clear_flag(ui_led_status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void led_count_focus_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
    if (led_kb == NULL) return;
    lv_keyboard_set_textarea(led_kb, led_count_ta);
    lv_obj_clear_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
    if (ui_led_status_label) lv_obj_add_flag(ui_led_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void led_count_defocus_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_DEFOCUSED) return;
    if (led_kb) lv_obj_add_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
    if (ui_led_status_label) lv_obj_clear_flag(ui_led_status_label, LV_OBJ_FLAG_HIDDEN);
    if (led_count_shown >= 0) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", (int)led_count_shown);
        lv_textarea_set_text(led_count_ta, buf);
    }
}

// ---------------------------------------------------------------------------
static void led_style_slider(lv_obj_t * sl)
{
    lv_obj_set_style_bg_color(sl, lv_color_hex(UIK_C_SUNKEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sl, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(sl, lv_color_hex(UIK_C_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(sl, lv_color_hex(UIK_C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sl, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
}

// 滑块行：标题（左）+ 数值（右）+ 滑块（下一行）
static void led_make_slider_row(lv_obj_t * card, int idx, int y, const char * title,
                                int32_t min, int32_t max, int32_t init)
{
    lv_obj_t * t = uik_label(card, title, 0, 0, UIK_C_TEXT_DIM, UIK_FONT_HINT,
                             LV_TEXT_ALIGN_LEFT);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, y + 2);

    led_val[idx] = uik_label(card, "--", 0, 0, UIK_C_TEXT, UIK_FONT_BODY,
                             LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_width(led_val[idx], LED_CW);
    lv_obj_align(led_val[idx], LV_ALIGN_TOP_LEFT, 0, y);

    led_sl[idx] = lv_slider_create(card);
    lv_obj_set_size(led_sl[idx], LED_CW, 12);
    lv_obj_align(led_sl[idx], LV_ALIGN_TOP_LEFT, 0, y + 18);
    lv_slider_set_range(led_sl[idx], min, max);
    lv_slider_set_value(led_sl[idx], init, LV_ANIM_OFF);
    led_style_slider(led_sl[idx]);
    lv_obj_add_event_cb(led_sl[idx], led_slider_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)idx);
}

// ---------------------------------------------------------------------------
void ui_led_screen_init(void)
{
    ui_led = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_led, "LED Strip", NULL, back_btn_ui_cb, NULL,
               &ui_led_back_btn, NULL);

    // ---- 控制卡片 ----
    led_card = uik_panel(ui_led, UIK_LEFT, LED_CARD_Y, UIK_CONTENT_W, LED_CARD_H);
    led_accent = uik_accent_bar(led_card, 0, 0, LED_CW, UIK_C_PRIMARY);

    // ---- 第一行：LED COUNT ----
    {
        int y = 10;
        lv_obj_t * t = uik_label(led_card, "LED COUNT", 0, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, y + 8);

        lv_obj_t * hint = uik_label(led_card, "1-120, tap to edit", 0, 0,
                                    UIK_C_TEXT_FAINT, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(hint, LED_CW - LED_COUNT_TA_W - 94);
        lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 90, y + 8);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

        led_count_ta = uik_ta(led_card, LED_CW - LED_COUNT_TA_W, y + 1,
                              LED_COUNT_TA_W, LED_COUNT_TA_H, "0", true);
        lv_textarea_set_accepted_chars(led_count_ta, "0123456789");
        lv_textarea_set_max_length(led_count_ta, 3);
        lv_obj_set_style_text_align(led_count_ta, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_add_event_cb(led_count_ta, led_count_focus_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(led_count_ta, led_count_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    }

    // ---- 三根滑块：COLOR / BRIGHTNESS / SPEED ----
    led_make_slider_row(led_card, UI_LED_HUE,    10 + LED_ROW_STEP,     "COLOR",
                        0, UI_LED_HUE_MAX,   200);
    led_make_slider_row(led_card, UI_LED_BRIGHT, 10 + 2 * LED_ROW_STEP, "BRIGHTNESS",
                        0, UI_LED_BRIGHT_MAX, 190);
    led_make_slider_row(led_card, UI_LED_SPEED,  10 + 3 * LED_ROW_STEP, "SPEED",
                        0, UI_LED_SPEED_MAX,  40);

    // ---- 状态条 ----
    ui_led_status_label = uik_status_bar(ui_led);
    lv_label_set_text(ui_led_status_label, "WS2812 " LV_SYMBOL_BULLET " changes apply live");

    // ---- 数字键盘 ----
    led_kb = lv_keyboard_create(ui_led);
    lv_obj_set_size(led_kb, UIK_W, UIK_KB_H);
    lv_obj_align(led_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(led_kb);
    lv_keyboard_set_mode(led_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(led_kb, led_count_ta);
    lv_obj_add_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(led_kb, led_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(led_kb, led_kb_event_cb, LV_EVENT_CANCEL, NULL);

    ui_led_on_screen_ready();
}

// ---------------------------------------------------------------------------
void ui_led_set_state(int32_t hue, int32_t bright, int32_t speed,
                      int32_t count, uint32_t rgb)
{
    if (ui_led == NULL) return;

    if (led_sl[UI_LED_HUE])    lv_slider_set_value(led_sl[UI_LED_HUE],    hue,    LV_ANIM_OFF);
    if (led_sl[UI_LED_BRIGHT]) lv_slider_set_value(led_sl[UI_LED_BRIGHT], bright, LV_ANIM_OFF);
    if (led_sl[UI_LED_SPEED])  lv_slider_set_value(led_sl[UI_LED_SPEED],  speed,  LV_ANIM_OFF);

    if (led_val[UI_LED_HUE])
        lv_label_set_text_fmt(led_val[UI_LED_HUE], "%d\xC2\xB0", (int)hue);
    if (led_val[UI_LED_BRIGHT])
        lv_label_set_text_fmt(led_val[UI_LED_BRIGHT], "%d%%", (int)(bright * 100 / 255));
    if (led_val[UI_LED_SPEED]) {
        if (speed == 0) lv_label_set_text(led_val[UI_LED_SPEED], "STATIC");
        else            lv_label_set_text_fmt(led_val[UI_LED_SPEED], "%d%%", (int)speed);
    }

    // 数量回显进输入框（先比较再写，避免无谓刷新和光标跳动）
    if (led_count_ta != NULL && count != led_count_shown) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", (int)count);
        lv_textarea_set_text(led_count_ta, buf);
        led_count_shown = count;
    }

    // 当前颜色刷到卡片色条 + 色相滑块填充
    lv_color_t c = lv_color_hex(rgb);
    if (led_accent)
        lv_obj_set_style_bg_color(led_accent, c, LV_STATE_DEFAULT);
    if (led_sl[UI_LED_HUE])
        lv_obj_set_style_bg_color(led_sl[UI_LED_HUE], c, LV_PART_INDICATOR);
}

void ui_led_set_status(const char * text, uint32_t color)
{
    if (ui_led_status_label == NULL || text == NULL) return;
    lv_label_set_text(ui_led_status_label, text);
    lv_obj_set_style_text_color(ui_led_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
}

// ---------------------------------------------------------------------------
void ui_led_screen_destroy(void)
{
    if (ui_led) lv_obj_del(ui_led);
    ui_led = NULL;
    ui_led_back_btn = NULL;
    ui_led_status_label = NULL;
    led_card = NULL;
    led_accent = NULL;
    led_count_ta = NULL;
    led_kb = NULL;
    led_count_shown = -1;
    for (int i = 0; i < 4; i++) {
        led_sl[i]  = NULL;
        led_val[i] = NULL;
    }
}