#include "ui_nrf.h"
#include "../ui.h"
#include "../ui_kit.h"

#include <string.h>
#include <math.h>
#include <esp_heap_caps.h>

lv_obj_t * ui_nrf = NULL;
lv_obj_t * ui_nrf_back_btn = NULL;
lv_obj_t * ui_nrf_run_btn = NULL;
lv_obj_t * ui_nrf_header = NULL;
lv_obj_t * ui_nrf_list = NULL;
lv_obj_t * ui_nrf_status_label = NULL;

static lv_obj_t * nrf_card = NULL;
static lv_obj_t * nrf_val[4] = { NULL };
static lv_obj_t * nrf_rows[UI_NRF_MAX_ROWS] = { NULL };
static lv_obj_t * nrf_row_left[UI_NRF_MAX_ROWS] = { NULL };
static lv_obj_t * nrf_row_right[UI_NRF_MAX_ROWS] = { NULL };

static lv_obj_t *   nrf_btn_pkts = NULL;
static lv_obj_t *   nrf_btn_spec = NULL;
static lv_obj_t *   nrf_canvas   = NULL;
static lv_color_t * nrf_cbuf     = NULL;
static bool         nrf_spectrum = false;
static bool         nrf_spec_filled = false;   // 首屏是否已用整屏填充

static lv_color_t   nrf_clut[256];        // 活动度 → 颜色（黑蓝青绿黄红）
static uint8_t      nrf_gain[256];        // 低活动度的显示增益（sqrt 曲线）

static uint32_t nrf_list_version = 0xFFFFFFFFu;
static int      nrf_list_n       = -1;
static uint32_t nrf_stat_shown[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
static bool     nrf_run_shown    = false;

static const char * kRate[3] = { "1M", "2M", "250K" };

static void nrf_set_label_if_changed(lv_obj_t * lbl, const char * text)
{
    if (lbl == NULL || text == NULL) return;
    const char * cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, text) == 0) return;
    lv_label_set_text(lbl, text);
}

static void nrf_set_color_if_changed(lv_obj_t * lbl, uint32_t rgb)
{
    if (lbl == NULL) return;
    lv_color_t want = lv_color_hex(rgb);
    lv_color_t cur  = lv_obj_get_style_text_color(lbl, LV_STATE_DEFAULT);
    if (lv_color_to32(cur) == lv_color_to32(want)) return;
    lv_obj_set_style_text_color(lbl, want, LV_STATE_DEFAULT);
}

// 256 级色阶：0 = 无信号（近黑），255 = 强信号（红）
static void nrf_build_clut(void)
{
    static const uint8_t stop[6][3] = {
        {   0,  0, 40 }, {   0, 20, 150 }, {   0, 150, 220 },
        {  60, 210, 90 }, { 250, 215, 40 }, { 200,  20, 10 },
    };
    for (int i = 0; i < 256; i++) {
        float f = i / 255.0f * 5.0f;
        int   s = (int)f;
        if (s > 4) s = 4;
        float t = f - s;
        uint8_t r = (uint8_t)(stop[s][0] + (stop[s + 1][0] - stop[s][0]) * t);
        uint8_t g = (uint8_t)(stop[s][1] + (stop[s + 1][1] - stop[s][1]) * t);
        uint8_t b = (uint8_t)(stop[s][2] + (stop[s + 1][2] - stop[s][2]) * t);
        nrf_clut[i] = lv_color_make(r, g, b);

        // 实测 RPD 命中率多为 4~25%，线性映射会全挤在近黑的一端；
        // 用 sqrt 提亮低端，并给"只要检测到能量"一个可见下限，
        // 否则安静环境里 4% 的信道在屏幕上看不出来。
        float x = i / 255.0f;
        uint8_t gv = (uint8_t)(sqrtf(x) * 255.0f);
        if (i > 0 && gv < 72) gv = 72;
        nrf_gain[i] = gv;
    }
}

// ---------------------------------------------------------------------------
static void back_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen1_screen_init);
}

static void run_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_nrf_on_toggle();
}

static void mode_pkts_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_nrf_set_mode(false);
}

static void mode_spec_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_nrf_set_mode(true);
}

// ---------------------------------------------------------------------------
void ui_nrf_set_mode(bool spectrum)
{
    if (spectrum && !nrf_spectrum) nrf_spec_filled = false;   // 进频谱页重新铺一次
    nrf_spectrum = spectrum;

    // ★ 不能用 add_flag(x, 0) 这种写法：LVGL 里 0 是"没有标志"，
    //   加/清 0 都是空操作，结果就是两个对象的状态从来没被切换过。
    if (ui_nrf_list) {
        if (spectrum) lv_obj_add_flag(ui_nrf_list, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_clear_flag(ui_nrf_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (nrf_canvas) {
        if (spectrum) lv_obj_clear_flag(nrf_canvas, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(nrf_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    if (nrf_btn_pkts) uik_btn_states(nrf_btn_pkts, spectrum ? UIK_C_NEUTRAL : UIK_C_PRIMARY);
    if (nrf_btn_spec) uik_btn_states(nrf_btn_spec, spectrum ? UIK_C_PRIMARY : UIK_C_NEUTRAL);
}

bool ui_nrf_spectrum_visible(void) { return nrf_spectrum && nrf_canvas != NULL; }
bool ui_nrf_spectrum_ready(void)   { return nrf_canvas != NULL && nrf_cbuf != NULL; }

bool ui_nrf_canvas_hidden(void)
{
    return nrf_canvas == NULL || lv_obj_has_flag(nrf_canvas, LV_OBJ_FLAG_HIDDEN);
}

bool ui_nrf_list_hidden(void)
{
    return ui_nrf_list == NULL || lv_obj_has_flag(ui_nrf_list, LV_OBJ_FLAG_HIDDEN);
}

// 网格：作为"标尺"画在数据之上（1px 细线，暗蓝灰），
// 竖线每 52px 一格 = 时间刻度，横线每 20px 一格 ≈ 25 个信道。
// 因为画布是按列滚动（memmove 左移 1px），网格必须每次重画，
// 否则会跟着数据一起被拖走。11 条线 ≈ 2500 次写，开销可以忽略。
static void nrf_draw_grid(void)
{
    if (nrf_cbuf == NULL) return;
    lv_color_t gc = lv_color_make(44, 58, 88);
    for (int x = 0; x < NRF_SPEC_W; x += 52)
        for (int y = 0; y < NRF_SPEC_H; y++)
            nrf_cbuf[(size_t)y * NRF_SPEC_W + x] = gc;
    for (int y = 0; y < NRF_SPEC_H; y += 20)
        for (int x = 0; x < NRF_SPEC_W; x++)
            nrf_cbuf[(size_t)y * NRF_SPEC_W + x] = gc;
}

void ui_nrf_spectrum_push(const uint8_t * vals, uint8_t lo, uint8_t hi, bool commit)
{
    if (nrf_cbuf == NULL || vals == NULL || !nrf_spectrum) return;
    if (hi < lo) return;

    // 自动增益：RPD 命中率绝对值都很小（几 % 到几十 %），固定映射会全挤在
    // 蓝色区段。按本列峰值归一化，最强的信道给到红端，其余按比例分级——
    // 和 SDR 频谱图的自动对比度是一个思路。
    int peak = 0;
    for (int c = lo; c <= hi && c < UI_NRF_CH_NUM; c++)
        if (vals[c] > peak) peak = vals[c];
    if (peak < 24) peak = 24;              // 环境安静时不把噪声放大成满屏红

    // 频率向上递增：y=0 是最低行 → 最高频
    int span = (int)hi - (int)lo + 1;
    lv_color_t col[NRF_SPEC_H];
    for (int y = 0; y < NRF_SPEC_H; y++) {
        int ch = hi - (y * span) / NRF_SPEC_H;
        uint8_t v = (ch >= 0 && ch < UI_NRF_CH_NUM) ? vals[ch] : 0;
        int scaled = (int)v * 255 / peak;
        if (scaled > 255) scaled = 255;
        col[y] = nrf_clut[nrf_gain[scaled]];
    }

    if (commit && !nrf_spec_filled) {
        // 第一轮扫完：把整屏都铺成这一列。
        // 否则要等 312 轮（dwell 20ms 时 13 分钟）画面才长满，看起来像"没有频谱"。
        for (int y = 0; y < NRF_SPEC_H; y++) {
            lv_color_t * row = nrf_cbuf + (size_t)y * NRF_SPEC_W;
            for (int x = 0; x < NRF_SPEC_W; x++) row[x] = col[y];
        }
        nrf_spec_filled = true;
    } else {
        if (commit) {
            // 完成一轮：整屏左移一列，空出最右边一列
            for (int y = 0; y < NRF_SPEC_H; y++) {
                lv_color_t * row = nrf_cbuf + (size_t)y * NRF_SPEC_W;
                memmove(row, row + 1, (NRF_SPEC_W - 1) * sizeof(lv_color_t));
                row[NRF_SPEC_W - 1] = nrf_clut[0];
            }
        }
        for (int y = 0; y < NRF_SPEC_H; y++)
            nrf_cbuf[(size_t)y * NRF_SPEC_W + (NRF_SPEC_W - 1)] = col[y];
    }

    nrf_draw_grid();
    lv_obj_invalidate(nrf_canvas);
}

// ---------------------------------------------------------------------------
void ui_nrf_screen_init(void)
{
    ui_nrf = uik_screen_init();
    nrf_build_clut();

    uik_topbar(ui_nrf, "nRF24", LV_SYMBOL_PLAY " Scan",
               back_btn_ui_cb, run_btn_ui_cb, &ui_nrf_back_btn, &ui_nrf_run_btn);

    // ---- 统计卡片 ----
    nrf_card = uik_panel(ui_nrf, UIK_LEFT, NRF_CARD_Y, UIK_CONTENT_W, NRF_CARD_H);
    static const char * cap[4] = { "PKTS", "CH", "RATE", "HW" };
    for (int i = 0; i < 4; i++) {
        int x = i * NRF_COL_W;
        nrf_val[i] = uik_label(nrf_card, "-", x, 0, UIK_C_TEXT,
                               UIK_FONT_BIG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(nrf_val[i], NRF_COL_W);
        lv_obj_align(nrf_val[i], LV_ALIGN_TOP_LEFT, x, 1);
        lv_obj_t * c = uik_label(nrf_card, cap[i], x, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(c, NRF_COL_W);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, 25);
        if (i > 0) uik_separator(nrf_card, x, 6, 1);
    }

    // ---- 标题 + 模式开关 ----
    ui_nrf_header = uik_label(ui_nrf, "nRF24L01+ sniffer", 0, 0, UIK_C_TEXT_DIM,
                              UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_nrf_header, 140);
    lv_obj_align(ui_nrf_header, LV_ALIGN_TOP_LEFT, UIK_LEFT, NRF_HDR_Y + 4);
    lv_label_set_long_mode(ui_nrf_header, LV_LABEL_LONG_DOT);

    nrf_btn_pkts = uik_btn(ui_nrf, "Pkts", UIK_LEFT + 144, NRF_HDR_Y, 74, NRF_HDR_H,
                           UIK_C_NEUTRAL);
    nrf_btn_spec = uik_btn(ui_nrf, "Spec", UIK_LEFT + 222, NRF_HDR_Y, 82, NRF_HDR_H,
                           UIK_C_NEUTRAL);
    lv_obj_add_event_cb(nrf_btn_pkts, mode_pkts_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(nrf_btn_spec, mode_spec_cb, LV_EVENT_CLICKED, NULL);

    // ---- 内容区 A：抓包列表 ----
    ui_nrf_list = lv_list_create(ui_nrf);
    lv_obj_set_size(ui_nrf_list, UIK_CONTENT_W, NRF_LIST_H);
    lv_obj_align(ui_nrf_list, LV_ALIGN_TOP_LEFT, UIK_LEFT, NRF_LIST_Y);
    lv_obj_set_scrollbar_mode(ui_nrf_list, LV_SCROLLBAR_MODE_AUTO);
    uik_list(ui_nrf_list);

    // ---- 内容区 B：频谱画布（PSRAM 上 312x100 RGB565 ≈ 62KB）----
    nrf_cbuf = (lv_color_t *)heap_caps_malloc(
        (size_t)NRF_SPEC_W * NRF_SPEC_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (nrf_cbuf != NULL) {
        memset(nrf_cbuf, 0, (size_t)NRF_SPEC_W * NRF_SPEC_H * sizeof(lv_color_t));
        nrf_canvas = lv_canvas_create(ui_nrf);
        lv_canvas_set_buffer(nrf_canvas, nrf_cbuf, NRF_SPEC_W, NRF_SPEC_H,
                             LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(nrf_canvas, LV_ALIGN_TOP_LEFT, UIK_LEFT, NRF_LIST_Y);
        lv_obj_clear_flag(nrf_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(nrf_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    ui_nrf_status_label = uik_status_bar(ui_nrf);
    lv_label_set_text(ui_nrf_status_label, "RX only " LV_SYMBOL_BULLET " tap Scan");

    for (int i = 0; i < UI_NRF_MAX_ROWS; i++) {
        nrf_rows[i] = NULL;
        nrf_row_left[i] = NULL;
        nrf_row_right[i] = NULL;
    }
    nrf_list_version = 0xFFFFFFFFu;
    nrf_list_n = -1;
    for (int i = 0; i < 4; i++) nrf_stat_shown[i] = 0xFFFFFFFFu;
    nrf_run_shown = false;

    ui_nrf_set_mode(false);
    ui_nrf_on_screen_ready();
}

// ---------------------------------------------------------------------------
void ui_nrf_set_running(bool running, bool available)
{
    if (ui_nrf_run_btn == NULL) return;
    lv_obj_t * lbl = lv_obj_get_child(ui_nrf_run_btn, 0);
    if (lbl == NULL) return;

    if (!available) {
        lv_label_set_text(lbl, LV_SYMBOL_PLAY " Scan");
        uik_btn_states(ui_nrf_run_btn, UIK_C_NEUTRAL);
        nrf_run_shown = running;
        return;
    }
    if (running == nrf_run_shown) return;
    nrf_run_shown = running;
    if (running) {
        lv_label_set_text(lbl, LV_SYMBOL_STOP " Stop");
        uik_btn_states(ui_nrf_run_btn, UIK_C_DANGER);
    } else {
        lv_label_set_text(lbl, LV_SYMBOL_PLAY " Scan");
        uik_btn_states(ui_nrf_run_btn, UIK_C_PRIMARY);
    }
}

void ui_nrf_set_stats(uint32_t packets, uint8_t ch, uint8_t rate_idx,
                      bool hopping, bool available)
{
    if (ui_nrf == NULL) return;
    char buf[12];

    if (nrf_stat_shown[0] != packets) {
        nrf_stat_shown[0] = packets;
        if (packets >= 100000u) lv_snprintf(buf, sizeof(buf), "%uK", (unsigned)(packets / 1000u));
        else                    lv_snprintf(buf, sizeof(buf), "%u", (unsigned)packets);
        nrf_set_label_if_changed(nrf_val[0], buf);
    }
    uint32_t chkey = (uint32_t)ch | (hopping ? 0x100u : 0u) | (available ? 0x200u : 0u);
    if (nrf_stat_shown[1] != chkey) {
        nrf_stat_shown[1] = chkey;
        if (!available)      lv_snprintf(buf, sizeof(buf), "--");
        else if (hopping)    lv_snprintf(buf, sizeof(buf), "%u*", (unsigned)ch);
        else                 lv_snprintf(buf, sizeof(buf), "%u", (unsigned)ch);
        nrf_set_label_if_changed(nrf_val[1], buf);
    }
    uint32_t rkey = available ? (uint32_t)(rate_idx & 3) : 9u;
    if (nrf_stat_shown[2] != rkey) {
        nrf_stat_shown[2] = rkey;
        nrf_set_label_if_changed(nrf_val[2], available ? kRate[rate_idx & 3] : "--");
    }
    uint32_t hkey = available ? 1u : 0u;
    if (nrf_stat_shown[3] != hkey) {
        nrf_stat_shown[3] = hkey;
        nrf_set_label_if_changed(nrf_val[3], available ? "OK" : "NO");
        nrf_set_color_if_changed(nrf_val[3], available ? UIK_C_OK_TEXT : UIK_C_ERR_TEXT);
    }
}

void ui_nrf_set_header(const char * text, uint32_t color)
{
    if (ui_nrf_header == NULL || text == NULL) return;
    nrf_set_label_if_changed(ui_nrf_header, text);
    nrf_set_color_if_changed(ui_nrf_header, color);
}

void ui_nrf_set_status(const char * text, uint32_t color)
{
    if (ui_nrf_status_label == NULL || text == NULL) return;
    nrf_set_label_if_changed(ui_nrf_status_label, text);
    nrf_set_color_if_changed(ui_nrf_status_label, color);
}

// ---------------------------------------------------------------------------
static void nrf_rebuild_list(int n, const ui_nrf_entry_t * entries)
{
    if (ui_nrf_list == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_NRF_MAX_ROWS) n = UI_NRF_MAX_ROWS;

    lv_obj_clean(ui_nrf_list);
    lv_obj_scroll_to_y(ui_nrf_list, 0, LV_ANIM_OFF);
    for (int i = 0; i < UI_NRF_MAX_ROWS; i++) {
        nrf_rows[i] = NULL;
        nrf_row_left[i] = NULL;
        nrf_row_right[i] = NULL;
    }

    for (int i = 0; i < n; i++) {
        lv_obj_t * row = lv_list_add_btn(ui_nrf_list, NULL, entries[i].name);
        lv_obj_t * right = lv_label_create(row);
        lv_label_set_text(right, entries[i].right);
        lv_obj_set_width(right, 60);
        lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
        uik_list_row(row);
        lv_obj_set_style_text_font(right, UIK_FONT_HINT, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(right, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        nrf_rows[i] = row;
        nrf_row_left[i] = lv_obj_get_child(row, 0);
        nrf_row_right[i] = right;
    }
}

void ui_nrf_set_list(uint32_t version, int n, const ui_nrf_entry_t * entries)
{
    if (ui_nrf_list == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_NRF_MAX_ROWS) n = UI_NRF_MAX_ROWS;

    if (version != nrf_list_version || n != nrf_list_n) {
        nrf_list_version = version;
        nrf_list_n = n;
        nrf_rebuild_list(n, entries);
        return;
    }
    for (int i = 0; i < n; i++) {
        nrf_set_label_if_changed(nrf_row_left[i], entries[i].name);
        nrf_set_label_if_changed(nrf_row_right[i], entries[i].right);
    }
}

// ---------------------------------------------------------------------------
void ui_nrf_screen_destroy(void)
{
    if (ui_nrf) lv_obj_del(ui_nrf);
    ui_nrf = NULL;
    ui_nrf_back_btn = NULL;
    ui_nrf_run_btn = NULL;
    ui_nrf_header = NULL;
    ui_nrf_list = NULL;
    ui_nrf_status_label = NULL;
    nrf_card = NULL;
    nrf_btn_pkts = NULL;
    nrf_btn_spec = NULL;
    nrf_canvas = NULL;
    if (nrf_cbuf) { heap_caps_free(nrf_cbuf); nrf_cbuf = NULL; }
    nrf_spectrum = false;
    for (int i = 0; i < 4; i++) {
        nrf_val[i] = NULL;
        nrf_stat_shown[i] = 0xFFFFFFFFu;
    }
    for (int i = 0; i < UI_NRF_MAX_ROWS; i++) {
        nrf_rows[i] = NULL;
        nrf_row_left[i] = NULL;
        nrf_row_right[i] = NULL;
    }
    nrf_list_version = 0xFFFFFFFFu;
    nrf_list_n = -1;
    nrf_run_shown = false;
}