#include "ui_cc.h"
#include "../ui.h"
#include "../ui_kit.h"

#include <string.h>
#include <esp_heap_caps.h>

lv_obj_t * ui_cc = NULL;
lv_obj_t * ui_cc_back_btn = NULL;
lv_obj_t * ui_cc_run_btn = NULL;
lv_obj_t * ui_cc_header = NULL;
lv_obj_t * ui_cc_status_label = NULL;

static lv_obj_t *   cc_card = NULL;
static lv_obj_t *   cc_val[4] = { NULL };
static lv_obj_t *   cc_canvas = NULL;
static lv_color_t * cc_cbuf = NULL;
static uint8_t *    cc_plane = NULL;
static lv_color_t   cc_clut[256];
static bool         cc_filled = false;

static uint32_t cc_shown[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
static bool     cc_run_shown = false;

static lv_obj_t * cc_btn_pkts = NULL;
static lv_obj_t * cc_btn_spec = NULL;
static lv_obj_t * cc_list = NULL;
static lv_obj_t * cc_rows[UI_CC_MAX_ROWS] = { NULL };
static lv_obj_t * cc_row_l[UI_CC_MAX_ROWS] = { NULL };
static lv_obj_t * cc_row_r[UI_CC_MAX_ROWS] = { NULL };
static bool       cc_pkts_mode = false;
static uint32_t   cc_list_ver = 0xFFFFFFFFu;
static int        cc_list_n = -1;

static void cc_label_if_changed(lv_obj_t * l, const char * t)
{
    if (l == NULL || t == NULL) return;
    const char * cur = lv_label_get_text(l);
    if (cur && strcmp(cur, t) == 0) return;
    lv_label_set_text(l, t);
}

static void cc_color_if_changed(lv_obj_t * l, uint32_t rgb)
{
    if (l == NULL) return;
    lv_color_t want = lv_color_hex(rgb);
    if (lv_color_to32(lv_obj_get_style_text_color(l, LV_STATE_DEFAULT)) == lv_color_to32(want)) return;
    lv_obj_set_style_text_color(l, want, LV_STATE_DEFAULT);
}

// 色阶：黑 → 蓝 → 青 → 绿 → 黄 → 红（与 nRF 频谱一致，便于对照）
static void cc_build_clut(void)
{
    static const uint8_t stop[6][3] = {
        {   0,  0, 40 }, {   0, 20, 150 }, {   0, 150, 220 },
        {  60, 210, 90 }, { 250, 215, 40 }, { 200,  20, 10 },
    };
    for (int i = 0; i < 256; i++) {
        float f = i / 255.0f * 5.0f;
        int   s = (int)f; if (s > 4) s = 4;
        float t = f - s;
        uint8_t r = (uint8_t)(stop[s][0] + (stop[s + 1][0] - stop[s][0]) * t);
        uint8_t g = (uint8_t)(stop[s][1] + (stop[s + 1][1] - stop[s][1]) * t);
        uint8_t b = (uint8_t)(stop[s][2] + (stop[s + 1][2] - stop[s][2]) * t);
        cc_clut[i] = lv_color_make(r, g, b);
    }
}

// dBm → 0..255：-105dBm 为 0，-35dBm 为 255（CC1101 RSSI 是绝对值，无需自动增益）
static uint8_t cc_dbm_to_u8(int8_t dbm)
{
    int v = ((int)dbm + 105) * 255 / 70;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static inline bool cc_is_grid(int x, int y)
{
    return (x % 52) == 0 || (y % 20) == 0;
}

// 数据面与画布分开，网格每次合成时重画，避免竖线随滚动变宽
static lv_color_t cc_cell(int x, int y)
{
    uint8_t v = cc_plane[(size_t)y * CC_SPEC_W + x];
    if (v != 0) return cc_clut[v];
    return cc_is_grid(x, y) ? lv_color_make(44, 58, 88) : lv_color_make(8, 10, 16);
}

static void cc_compose_all(void)
{
    for (int y = 0; y < CC_SPEC_H; y++)
        for (int x = 0; x < CC_SPEC_W; x++)
            cc_cbuf[(size_t)y * CC_SPEC_W + x] = cc_cell(x, y);
}

// ---------------------------------------------------------------------------
static void back_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Screen1_screen_init);
}

static void run_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_cc_on_toggle();
}

static void mode_pkts_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_cc_on_mode(true);
}

static void mode_spec_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_cc_on_mode(false);
}

// ---------------------------------------------------------------------------
void ui_cc_spectrum_push(const int8_t * dbm, float min_mhz, float max_mhz, bool commit)
{
    if (cc_cbuf == NULL || cc_plane == NULL || dbm == NULL) return;

    uint8_t col[CC_SPEC_H];
    for (int y = 0; y < CC_SPEC_H; y++) {
        // y=0 是最低行 → 最高频率
        int idx = (CC_SPEC_H - 1 - y) * (UI_CC_STEPS - 1) / (CC_SPEC_H - 1);
        col[y] = cc_dbm_to_u8(dbm[idx]);
    }
    (void)min_mhz; (void)max_mhz;

    if (commit && !cc_filled) {
        for (int y = 0; y < CC_SPEC_H; y++)
            for (int x = 0; x < CC_SPEC_W; x++)
                cc_plane[(size_t)y * CC_SPEC_W + x] = col[y];
        cc_filled = true;
        cc_compose_all();
    } else if (commit) {
        for (int y = 0; y < CC_SPEC_H; y++)
            memmove(cc_plane + (size_t)y * CC_SPEC_W,
                    cc_plane + (size_t)y * CC_SPEC_W + 1, CC_SPEC_W - 1);
        for (int y = 0; y < CC_SPEC_H; y++)
            cc_plane[(size_t)y * CC_SPEC_W + (CC_SPEC_W - 1)] = col[y];
        cc_compose_all();
    } else {
        for (int y = 0; y < CC_SPEC_H; y++)
            cc_plane[(size_t)y * CC_SPEC_W + (CC_SPEC_W - 1)] = col[y];
        for (int y = 0; y < CC_SPEC_H; y++)
            cc_cbuf[(size_t)y * CC_SPEC_W + (CC_SPEC_W - 1)] = cc_cell(CC_SPEC_W - 1, y);
    }
    lv_obj_invalidate(cc_canvas);
}

bool ui_cc_spectrum_ready(void) { return cc_canvas != NULL && cc_cbuf != NULL; }
bool ui_cc_pkts_visible(void)   { return cc_pkts_mode && cc_list != NULL; }

void ui_cc_set_mode(bool pkts)
{
    cc_pkts_mode = pkts;
    if (cc_list) {
        if (pkts) lv_obj_clear_flag(cc_list, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(cc_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (cc_canvas) {
        if (pkts) lv_obj_add_flag(cc_canvas, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_clear_flag(cc_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    if (cc_btn_pkts) uik_btn_states(cc_btn_pkts, pkts ? UIK_C_PRIMARY : UIK_C_NEUTRAL);
    if (cc_btn_spec) uik_btn_states(cc_btn_spec, pkts ? UIK_C_NEUTRAL : UIK_C_PRIMARY);
    cc_list_ver = 0xFFFFFFFFu;
    cc_list_n = -1;
}

static void cc_rebuild_list(int n, const ui_cc_entry_t * e)
{
    if (cc_list == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_CC_MAX_ROWS) n = UI_CC_MAX_ROWS;
    lv_obj_clean(cc_list);
    lv_obj_scroll_to_y(cc_list, 0, LV_ANIM_OFF);
    for (int i = 0; i < UI_CC_MAX_ROWS; i++) {
        cc_rows[i] = NULL; cc_row_l[i] = NULL; cc_row_r[i] = NULL;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t * row = lv_list_add_btn(cc_list, NULL, e[i].name);
        lv_obj_t * right = lv_label_create(row);
        lv_label_set_text(right, e[i].right);
        lv_obj_set_width(right, 66);
        lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
        uik_list_row(row);
        lv_obj_set_style_text_font(right, UIK_FONT_HINT, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(right, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        cc_rows[i] = row;
        cc_row_l[i] = lv_obj_get_child(row, 0);
        cc_row_r[i] = right;
    }
}

void ui_cc_set_list(uint32_t version, int n, const ui_cc_entry_t * entries)
{
    if (cc_list == NULL || entries == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_CC_MAX_ROWS) n = UI_CC_MAX_ROWS;
    if (version != cc_list_ver || n != cc_list_n) {
        cc_list_ver = version;
        cc_list_n = n;
        cc_rebuild_list(n, entries);
        return;
    }
    for (int i = 0; i < n; i++) {
        cc_label_if_changed(cc_row_l[i], entries[i].name);
        cc_label_if_changed(cc_row_r[i], entries[i].right);
    }
}

// ---------------------------------------------------------------------------
void ui_cc_screen_init(void)
{
    ui_cc = uik_screen_init();
    cc_build_clut();

    uik_topbar(ui_cc, "CC1101", LV_SYMBOL_PLAY " Scan",
               back_cb, run_cb, &ui_cc_back_btn, &ui_cc_run_btn);

    cc_card = uik_panel(ui_cc, UIK_LEFT, CC_CARD_Y, UIK_CONTENT_W, CC_CARD_H);
    static const char * cap[4] = { "PEAK", "FREQ", "CUR", "HW" };
    for (int i = 0; i < 4; i++) {
        int x = i * CC_COL_W;
        cc_val[i] = uik_label(cc_card, "-", x, 0, UIK_C_TEXT, UIK_FONT_BIG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(cc_val[i], CC_COL_W);
        lv_obj_align(cc_val[i], LV_ALIGN_TOP_LEFT, x, 1);
        lv_obj_t * c = uik_label(cc_card, cap[i], x, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(c, CC_COL_W);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, 25);
        if (i > 0) uik_separator(cc_card, x, 6, 1);
    }

    ui_cc_header = uik_label(ui_cc, "CC1101 Sub-GHz sweep", 0, 0, UIK_C_TEXT_DIM,
                             UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_cc_header, 140);
    lv_obj_align(ui_cc_header, LV_ALIGN_TOP_LEFT, UIK_LEFT, CC_HDR_Y + 4);
    lv_label_set_long_mode(ui_cc_header, LV_LABEL_LONG_DOT);

    cc_btn_pkts = uik_btn(ui_cc, "Pkts", UIK_LEFT + 144, CC_HDR_Y, 74, CC_HDR_H, UIK_C_NEUTRAL);
    cc_btn_spec = uik_btn(ui_cc, "Spec", UIK_LEFT + 222, CC_HDR_Y, 82, CC_HDR_H, UIK_C_NEUTRAL);
    lv_obj_add_event_cb(cc_btn_pkts, mode_pkts_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(cc_btn_spec, mode_spec_cb, LV_EVENT_CLICKED, NULL);

    // 抓包列表（与频谱画布同一块区域，互斥显示）
    cc_list = lv_list_create(ui_cc);
    lv_obj_set_size(cc_list, UIK_CONTENT_W, CC_SPEC_H);
    lv_obj_align(cc_list, LV_ALIGN_TOP_LEFT, UIK_LEFT, CC_SPEC_Y);
    lv_obj_set_scrollbar_mode(cc_list, LV_SCROLLBAR_MODE_AUTO);
    uik_list(cc_list);
    lv_obj_add_flag(cc_list, LV_OBJ_FLAG_HIDDEN);

    cc_cbuf = (lv_color_t *)heap_caps_malloc(
        (size_t)CC_SPEC_W * CC_SPEC_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cc_plane = (uint8_t *)heap_caps_malloc((size_t)CC_SPEC_W * CC_SPEC_H,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (cc_cbuf && cc_plane) {
        memset(cc_plane, 0, (size_t)CC_SPEC_W * CC_SPEC_H);
        cc_canvas = lv_canvas_create(ui_cc);
        lv_canvas_set_buffer(cc_canvas, cc_cbuf, CC_SPEC_W, CC_SPEC_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(cc_canvas, LV_ALIGN_TOP_LEFT, UIK_LEFT, CC_SPEC_Y);
        lv_obj_clear_flag(cc_canvas, LV_OBJ_FLAG_CLICKABLE);
        cc_compose_all();
    }

    ui_cc_status_label = uik_status_bar(ui_cc);
    lv_label_set_text(ui_cc_status_label, "RX only " LV_SYMBOL_BULLET " tap Scan");

    for (int i = 0; i < 4; i++) cc_shown[i] = 0xFFFFFFFFu;
    cc_run_shown = false;
    ui_cc_set_mode(false);
    ui_cc_on_screen_ready();
}

void ui_cc_set_running(bool running, bool available)
{
    if (ui_cc_run_btn == NULL) return;
    lv_obj_t * lbl = lv_obj_get_child(ui_cc_run_btn, 0);
    if (lbl == NULL) return;
    if (!available) {
        lv_label_set_text(lbl, LV_SYMBOL_PLAY " Scan");
        uik_btn_states(ui_cc_run_btn, UIK_C_NEUTRAL);
        cc_run_shown = running;
        return;
    }
    if (running == cc_run_shown) return;
    cc_run_shown = running;
    if (running) { lv_label_set_text(lbl, LV_SYMBOL_STOP " Stop"); uik_btn_states(ui_cc_run_btn, UIK_C_DANGER); }
    else         { lv_label_set_text(lbl, LV_SYMBOL_PLAY " Scan"); uik_btn_states(ui_cc_run_btn, UIK_C_PRIMARY); }
}

void ui_cc_set_stats(int8_t peak_dbm, float cur_mhz, int8_t cur_dbm, bool available)
{
    char buf[14];
    uint32_t k;

    k = available ? (uint32_t)(peak_dbm + 200) : 0xFFFFFFu;
    if (cc_shown[0] != k) {
        cc_shown[0] = k;
        if (available) lv_snprintf(buf, sizeof(buf), "%d", (int)peak_dbm);
        else           lv_snprintf(buf, sizeof(buf), "--");
        cc_label_if_changed(cc_val[0], buf);
    }
    k = available ? (uint32_t)(cur_mhz * 10.0f) : 0xFFFFFFu;
    if (cc_shown[1] != k) {
        cc_shown[1] = k;
        if (available) lv_snprintf(buf, sizeof(buf), "%.0f", (double)cur_mhz);
        else           lv_snprintf(buf, sizeof(buf), "--");
        cc_label_if_changed(cc_val[1], buf);
    }
    k = available ? (uint32_t)(cur_dbm + 200) : 0xFFFFFFu;
    if (cc_shown[2] != k) {
        cc_shown[2] = k;
        if (available) lv_snprintf(buf, sizeof(buf), "%d", (int)cur_dbm);
        else           lv_snprintf(buf, sizeof(buf), "--");
        cc_label_if_changed(cc_val[2], buf);
    }
    k = available ? 1u : 0u;
    if (cc_shown[3] != k) {
        cc_shown[3] = k;
        cc_label_if_changed(cc_val[3], available ? "OK" : "NO");
        cc_color_if_changed(cc_val[3], available ? UIK_C_OK_TEXT : UIK_C_ERR_TEXT);
    }
}

void ui_cc_set_header(const char * text, uint32_t color)
{
    if (ui_cc_header == NULL || text == NULL) return;
    cc_label_if_changed(ui_cc_header, text);
    cc_color_if_changed(ui_cc_header, color);
}

void ui_cc_set_status(const char * text, uint32_t color)
{
    if (ui_cc_status_label == NULL || text == NULL) return;
    cc_label_if_changed(ui_cc_status_label, text);
    cc_color_if_changed(ui_cc_status_label, color);
}

void ui_cc_screen_destroy(void)
{
    if (ui_cc) lv_obj_del(ui_cc);
    ui_cc = NULL;
    ui_cc_back_btn = NULL;
    ui_cc_run_btn = NULL;
    ui_cc_header = NULL;
    ui_cc_status_label = NULL;
    cc_card = NULL;
    cc_canvas = NULL;
    cc_list = NULL;
    cc_btn_pkts = NULL;
    cc_btn_spec = NULL;
    for (int i = 0; i < UI_CC_MAX_ROWS; i++) { cc_rows[i] = NULL; cc_row_l[i] = NULL; cc_row_r[i] = NULL; }
    cc_pkts_mode = false;
    cc_list_ver = 0xFFFFFFFFu;
    cc_list_n = -1;
    if (cc_cbuf)  { heap_caps_free(cc_cbuf);  cc_cbuf = NULL; }
    if (cc_plane) { heap_caps_free(cc_plane); cc_plane = NULL; }
    cc_filled = false;
    cc_run_shown = false;
    for (int i = 0; i < 4; i++) { cc_val[i] = NULL; cc_shown[i] = 0xFFFFFFFFu; }
}