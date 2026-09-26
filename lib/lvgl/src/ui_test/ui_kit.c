// ============================================================================
//  ui_kit.c - 统一 UI 组件库的实现（详见 ui_kit.h）
// ============================================================================

#include "ui_kit.h"

// ---------------------------------------------------------------------------
lv_obj_t * uik_screen_init(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(scr, 0, LV_STATE_DEFAULT);
    return scr;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_status_bar(lv_obj_t * parent)
{
    lv_obj_t * lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, UIK_W - 2 * UIK_PAD);
    lv_obj_set_style_text_font(lbl, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    // 长文本横向滚动，避免换行把布局顶乱
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl, "");
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, UIK_PAD, UIK_STATUS_Y);
    return lbl;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_topbar(lv_obj_t * parent, const char * title,
                      const char * right_btn_text,
                      lv_event_cb_t back_cb, lv_event_cb_t right_cb,
                      lv_obj_t ** back_out, lv_obj_t ** right_out)
{
    if (back_out)  *back_out  = NULL;
    if (right_out) *right_out = NULL;

    lv_obj_t * bar = lv_obj_create(parent);
    lv_obj_set_size(bar, UIK_W, UIK_TOPBAR_Y + UIK_TOPBAR_H + 2);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // 左：返回
    lv_obj_t * back = lv_btn_create(bar);
    lv_obj_set_size(back, 60, UIK_TOPBAR_H);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, UIK_PAD, UIK_TOPBAR_Y);
    lv_obj_set_style_bg_color(back, lv_color_hex(UIK_C_NEUTRAL), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_t * back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(back_lbl, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_center(back_lbl);
    if (back_cb) lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    if (back_out) *back_out = back;

    // 中：标题
    if (title != NULL) {
        lv_obj_t * t = lv_label_create(bar);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, UIK_FONT_TITLE, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, UIK_TOPBAR_Y + 7);
        lv_obj_add_flag(t, LV_OBJ_FLAG_USER_1);      // 标记：标题，便于后续改文案
    }

    // 右：可选按钮
    lv_obj_t * rbtn = NULL;
    if (right_btn_text != NULL) {
        rbtn = lv_btn_create(bar);
        lv_obj_set_size(rbtn, 64, UIK_TOPBAR_H);
        lv_obj_align(rbtn, LV_ALIGN_TOP_RIGHT, -UIK_PAD, UIK_TOPBAR_Y);
        lv_obj_set_style_bg_color(rbtn, lv_color_hex(UIK_C_NEUTRAL), LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rbtn, UIK_RADIUS, LV_STATE_DEFAULT);
        lv_obj_t * rl = lv_label_create(rbtn);
        lv_label_set_text(rl, right_btn_text);
        lv_obj_set_style_text_color(rl, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_center(rl);
        if (right_cb) lv_obj_add_event_cb(rbtn, right_cb, LV_EVENT_CLICKED, NULL);
    }
    if (right_out) *right_out = rbtn;
    return rbtn;
}

void uik_topbar_set_title(lv_obj_t * topbar, const char * title)
{
    if (topbar == NULL || title == NULL) return;
    uint32_t n = lv_obj_get_child_cnt(topbar);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t * c = lv_obj_get_child(topbar, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1)) {
            lv_label_set_text(c, title);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_panel(lv_obj_t * parent, int x, int y, int w, int h)
{
    lv_obj_t * p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(p, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(p, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(p, 4, LV_STATE_DEFAULT);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_btn(lv_obj_t * parent, const char * text,
                   int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(b, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(b, 0, LV_STATE_DEFAULT);
    if (text != NULL) {
        lv_obj_t * l = lv_label_create(b);
        lv_label_set_text(l, text);
        // 不显式指定的话会落到全局默认字体(14pt)，和菜单行/顶栏对不上，
        // 这里统一跟 TITLE 档对齐
        lv_obj_set_style_text_font(l, UIK_FONT_TITLE, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_center(l);
    }
    return b;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_ta(lv_obj_t * parent, int x, int y, int w, int h,
                  const char * placeholder, bool one_line)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ta, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ta, 4, LV_STATE_DEFAULT);
    if (one_line) lv_textarea_set_one_line(ta, true);
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    return ta;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_label(lv_obj_t * parent, const char * text,
                     int x, int y, uint32_t color,
                     const lv_font_t * font, lv_text_align_t align)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_STATE_DEFAULT);
    if (font) lv_obj_set_style_text_font(l, font, LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(l, align, LV_STATE_DEFAULT);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
    return l;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_menu_row(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, uint32_t bg,
                        lv_event_cb_t cb, void * user_data)
{
    lv_obj_t * row = lv_btn_create(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(bg), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);

    lv_obj_t * l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);

    // 右侧箭头，提示"可以点进去"
    lv_obj_t * arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);

    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    return row;
}
