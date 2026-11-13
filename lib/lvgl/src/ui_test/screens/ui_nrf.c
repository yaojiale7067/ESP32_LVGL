// ============================================================================
//  ui_nrf.c - nRF24L01+ 被动嗅探屏（实现同构于 ui_rfmon.c，铁律一致：
//  周期性小重绘是撕裂源 —— 所有标签"先比后写"，列表结构不变就不重建）
// ============================================================================

#include "ui_nrf.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "../ui_kit.h"

#include <string.h>

lv_obj_t * ui_nrf = NULL;
lv_obj_t * ui_nrf_back_btn = NULL;
lv_obj_t * ui_nrf_run_btn = NULL;
lv_obj_t * ui_nrf_header = NULL;
lv_obj_t * ui_nrf_list = NULL;
lv_obj_t * ui_nrf_status_label = NULL;

static lv_obj_t * nrf_card = NULL;
static lv_obj_t * nrf_val[4] = { NULL };          // 四格统计数字标签
static lv_obj_t * nrf_rows[UI_NRF_MAX_ROWS] = { NULL };
static lv_obj_t * nrf_row_left[UI_NRF_MAX_ROWS] = { NULL };
static lv_obj_t * nrf_row_right[UI_NRF_MAX_ROWS] = { NULL };

static uint32_t nrf_list_version = 0xFFFFFFFFu;
static int      nrf_list_n       = -1;
static uint32_t nrf_stat_shown[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
static bool     nrf_run_shown    = false;

static const char * kRate[3] = { "1M", "2M", "250K" };

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
void ui_nrf_screen_init(void)
{
    ui_nrf = uik_screen_init();

    uik_topbar(ui_nrf, "nRF24", LV_SYMBOL_PLAY " Scan",
               back_btn_ui_cb, run_btn_ui_cb, &ui_nrf_back_btn, &ui_nrf_run_btn);

    // ---- 统计卡片：4 列 ----
    nrf_card = uik_panel(ui_nrf, UIK_LEFT, NRF_CARD_Y, UIK_CONTENT_W, NRF_CARD_H);
    static const char * cap[4] = { "PKTS", "CH", "RATE", "HW" };
    for (int i = 0; i < 4; i++) {
        int x = i * NRF_COL_W;
        nrf_val[i] = uik_label(nrf_card, "-", x, 0, UIK_C_TEXT,
                               UIK_FONT_BIG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(nrf_val[i], NRF_COL_W);
        lv_obj_align(nrf_val[i], LV_ALIGN_TOP_LEFT, x, 4);
        lv_obj_t * c = uik_label(nrf_card, cap[i], x, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(c, NRF_COL_W);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, 28);
        if (i > 0) uik_separator(nrf_card, x, 6, 1);
    }

    // ---- 动态标题 ----
    ui_nrf_header = uik_label(ui_nrf, "nRF24L01+ sniffer", 0, 0, UIK_C_TEXT_DIM,
                              UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_nrf_header, UIK_CONTENT_W);
    lv_obj_align(ui_nrf_header, LV_ALIGN_TOP_LEFT, UIK_LEFT, NRF_HDR_Y);
    lv_label_set_long_mode(ui_nrf_header, LV_LABEL_LONG_DOT);

    // ---- 列表（4 x 25 可见行，可滚动）----
    ui_nrf_list = lv_list_create(ui_nrf);
    lv_obj_set_size(ui_nrf_list, UIK_CONTENT_W, NRF_LIST_H);
    lv_obj_align(ui_nrf_list, LV_ALIGN_TOP_LEFT, UIK_LEFT, NRF_LIST_Y);
    lv_obj_set_scrollbar_mode(ui_nrf_list, LV_SCROLLBAR_MODE_AUTO);
    uik_list(ui_nrf_list);

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

    ui_nrf_on_screen_ready();
}

// ---------------------------------------------------------------------------
void ui_nrf_set_running(bool running, bool available)
{
    if (ui_nrf_run_btn == NULL) return;
    lv_obj_t * lbl = lv_obj_get_child(ui_nrf_run_btn, 0);
    if (lbl == NULL) return;

    if (!available) {
        // 未接线/未握手：按钮灰掉。CLICKABLE 保留 ——
        // 点击仍走 on_toggle，由业务层在串口/状态条提示排障，比"死按钮"可诊断。
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

    // 列 0：包数（>=100000 显示 K）
    if (nrf_stat_shown[0] != packets) {
        nrf_stat_shown[0] = packets;
        if (packets >= 100000u) lv_snprintf(buf, sizeof(buf), "%uK", (unsigned)(packets / 1000u));
        else                    lv_snprintf(buf, sizeof(buf), "%u", (unsigned)packets);
        nrf_set_label_if_changed(nrf_val[0], buf);
    }
    // 列 1：信道（跳频时后面跟 *，固定信道纯数字）
    uint32_t chkey = (uint32_t)ch | (hopping ? 0x100u : 0u) | (available ? 0x200u : 0u);
    if (nrf_stat_shown[1] != chkey) {
        nrf_stat_shown[1] = chkey;
        if (!available)      lv_snprintf(buf, sizeof(buf), "--");
        else if (hopping)    lv_snprintf(buf, sizeof(buf), "%u*", (unsigned)ch);
        else                 lv_snprintf(buf, sizeof(buf), "%u", (unsigned)ch);
        nrf_set_label_if_changed(nrf_val[1], buf);
    }
    // 列 2：速率档
    uint32_t rkey = available ? (uint32_t)(rate_idx & 3) : 9u;
    if (nrf_stat_shown[2] != rkey) {
        nrf_stat_shown[2] = rkey;
        nrf_set_label_if_changed(nrf_val[2], available ? kRate[rate_idx & 3] : "--");
    }
    // 列 3：硬件状态（OK 绿 / NO 红）—— 一眼看清"是没接好还是没抓到"
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