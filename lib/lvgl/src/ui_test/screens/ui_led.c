// ============================================================================
//  ui_led.c - WS2812 灯带控制屏
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]        LED Strip
//      38..204   控制卡片（顶部色条 = 灯带当前颜色，全屏唯一的"取色预览"）
//                  LED COUNT   [  16 ]   ← 数字键盘输入（点击弹出，✓ 确认）
//                  COLOR   180°  [────────●──]
//                  BRIGHTNESS 75% [──────●────]
//                  SPEED   STATIC [────●──────]
//      212..239  状态条
//
//  设计取舍：
//    1) 颜色用【色相滑块】而不是 R/G/B 三根：灯带场景 99% 的需求是
//       "来个蓝色 / 换个暖色"，一根滑块一步到位；RGB 三根要来回试。
//       饱和度固定 255（WS2812 演示场景不需要灰调，需要就用亮度压）。
//    2) 灯珠数量用【数字键盘】而不是滑块：数量是要和实体灯带严格对上的数，
//       滑块拖到 "37 还是 38" 得盯着数值来回蹭，键盘直接输入三位数一步到位。
//       输入框限 3 位、只收 0-9（accepted_chars），越界值由业务层夹紧。
//    3) LED COUNT 排第一行：它是唯一会弹键盘的行。数字键盘占 120..239，
//       第一行在屏幕 y≈53..79，打字时输入框不会被键盘盖住
//       —— 和 WiFi 密码行"必须看得见自己输了什么"是同一条规则。
//    4) 反馈即时：滑块 VALUE_CHANGED 直接进业务层写 RMT，没有 Apply 按钮；
//       数量在键盘 ✓（READY）时提交，✗（CANCEL）放弃并回显当前值。
//    5) 卡片顶部色条 + 色相滑块填充色都刷成当前 RGB：
//       两处大面积色块比一个 12x12 小色 swatch 直观，且零额外控件。
//    6) 滑块旋钮 pad 4：轨道只有 12px 高，旋钮做成 20px 才够手指按。
// ============================================================================

#include "ui_led.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "../ui_kit.h"

#include <stdlib.h>      // atoi

lv_obj_t * ui_led = NULL;
lv_obj_t * ui_led_back_btn = NULL;
lv_obj_t * ui_led_status_label = NULL;

static lv_obj_t * led_card = NULL;
static lv_obj_t * led_accent = NULL;      // 卡片顶部色条（= 当前颜色）
static lv_obj_t * led_sl[4];              // HUE / BRIGHT / SPEED 滑块；[COUNT] 恒为 NULL
static lv_obj_t * led_val[4];             // 对应数值标签；[COUNT] 恒为 NULL
static lv_obj_t * led_count_ta = NULL;    // 灯珠数量输入框
static lv_obj_t * led_kb = NULL;          // 数字键盘（隐藏，聚焦输入框时弹出）
static int32_t  led_count_shown = -1;     // 输入框当前回显的数量，避免无谓 set_text

// ---------------------------------------------------------------------------
static void back_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen1_screen_init);
}

// 滑块事件：把"哪个控件 + 新值"原样报给业务层。
// 业务层会回调 ui_led_set_state() 把界面（含数值标签、颜色预览）统一刷一遍，
// 所以这里【不】就地改标签 —— 单一数据源，避免两边各改一半对不上。
static void led_slider_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    int what = (int)(uintptr_t)lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value((lv_obj_t *)lv_event_get_current_target(e));
    ui_led_on_change(what, v);
}

// ---- 数字键盘：弹出 / 收起 / 提交 -----------------------------------------
// 和 WiFi 屏同一套模式：FOCUSED 弹出、READY(✓)/CANCEL(✗) 收起、DEFOCUSED 收起。
// 差别是模式用 LV_KEYBOARD_MODE_NUMBER —— 只有数字 + 退格 + ✓/✗，
// 不会出现"给灯珠数量输入字母"这种无意义状态。
static void led_count_commit(void)
{
    if (led_count_ta == NULL) return;
    const char * txt = lv_textarea_get_text(led_count_ta);
    // 空串不提交（用户全删了又按 ✓）：回显当前生效值即可，
    // 业务层的 led.count 不受影响。
    if (txt != NULL && txt[0] != '\0') {
        int v = atoi(txt);
        ui_led_on_change(UI_LED_COUNT, v);   // 夹紧(1..120)在业务层 led_apply()
        return;                              // led_apply 会走 led_sync_ui 回显
    }
    ui_led_on_change(UI_LED_COUNT, led_count_shown);
}

static void led_kb_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        led_count_commit();
    }
    // CANCEL（✗）：放弃输入，回显当前值
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (led_kb) lv_obj_add_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
        if (code == LV_EVENT_CANCEL && led_count_shown >= 0) {
            char buf[8];
            lv_snprintf(buf, sizeof(buf), "%d", (int)led_count_shown);
            lv_textarea_set_text(led_count_ta, buf);
        }
        // 状态条在键盘底下，收键盘时恢复
        if (ui_led_status_label) lv_obj_clear_flag(ui_led_status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void led_count_focus_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
    if (led_kb == NULL) return;
    lv_keyboard_set_textarea(led_kb, led_count_ta);
    lv_obj_clear_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
    // 状态条会被键盘盖住，先藏起来，免得露出半行
    if (ui_led_status_label) lv_obj_add_flag(ui_led_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void led_count_defocus_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_DEFOCUSED) return;
    // 点输入框以外的地方 = 放弃这次输入，和 ✗ 同语义（不 commit）
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
    // 旋钮向轨道外扩 4px：轨道 12 高，旋钮 20 高，手指才按得准
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

    // ---- 第一行：LED COUNT（数字键盘输入）----
    {
        int y = 10;
        lv_obj_t * t = uik_label(led_card, "LED COUNT", 0, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, y + 8);

        // 提示语宽度算到输入框左沿为止（218 - 90 - 4 = 124），
        // 不留一个压在输入框底下的隐形区域
        lv_obj_t * hint = uik_label(led_card, "1-120, tap to edit", 0, 0,
                                    UIK_C_TEXT_FAINT, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(hint, LED_CW - LED_COUNT_TA_W - 94);
        lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 90, y + 8);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

        led_count_ta = uik_ta(led_card, LED_CW - LED_COUNT_TA_W, y + 1,
                              LED_COUNT_TA_W, LED_COUNT_TA_H, "0", true);
        lv_textarea_set_accepted_chars(led_count_ta, "0123456789");
        lv_textarea_set_max_length(led_count_ta, 3);      // 上限 120，三位足够
        // 输入的数字居中显示，一眼看清当前值
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

    // ---- 数字键盘（隐藏，聚焦数量输入框时弹出）----
    // 放在最后创建：它是屏幕最后一个子对象，弹出来自然盖在滑块/状态条上面。
    led_kb = lv_keyboard_create(ui_led);
    lv_obj_set_size(led_kb, UIK_W, UIK_KB_H);
    lv_obj_align(led_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(led_kb);
    lv_keyboard_set_mode(led_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(led_kb, led_count_ta);
    lv_obj_add_flag(led_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(led_kb, led_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(led_kb, led_kb_event_cb, LV_EVENT_CANCEL, NULL);

    // 建屏完毕，请业务层把真实状态推上来（控件初始值只是占位）
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

    // 数量回显进输入框。★ 先比较再写：
    //   lv_textarea_set_text 会触发 VALUE_CHANGED（进而整屏失效一次）并把光标
    //   打到末尾；值没变就完全不碰它。led_count_shown 同时充当"当前生效值"缓存，
    //   CANCEL/失焦回显和空串提交都用它兜底。
    if (led_count_ta != NULL && count != led_count_shown) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", (int)count);
        lv_textarea_set_text(led_count_ta, buf);
        led_count_shown = count;
    }

    // 当前颜色刷到两处大色块：卡片色条 + 色相滑块填充
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
