// ============================================================================
//  ui_rfmon.c - RF Monitor 屏（802.11 被动监听的可视化）
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]      RF Monitor      [Start]
//      38..88    统计卡片：BEACON / PROBE / DEAUTH / DISAS 四个大数字
//      92..104   动态标题（DEAUTH SOURCES 红色 / BEACONS SEEN 灰色）
//      106..206  列表（4 x 25 行整，可滚动）
//      212..239  状态条
//
//  设计取舍：
//    1) DEAUTH 一列平时是灰的，一旦计数 >0 立刻变红：这一屏存在的理由
//       就是"抓谁在踢我的设备下线"，警报必须是全屏幕跳的第一样东西。
//    2) 列表只在 (version, mode, n) 变化时重建；计数刷新走原地 set_text
//       （先比较再写）。理由和 dht11 屏一样：周期性小块重绘是无 TE 屏的
//       撕裂源，"值没变就不写"是这工程的铁律。
//    3) 行内两列：左列 flex_grow + LONG_DOT（SSID 最长 32 字符，截断比
//       横向滚动安全），右列定宽 60px 右对齐（"CH6 x123"），
//       长 SSID 顶不掉计数。
// ============================================================================

#include "ui_rfmon.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "../ui_kit.h"

#include <string.h>

lv_obj_t * ui_rfmon = NULL;
lv_obj_t * ui_rfmon_back_btn = NULL;
lv_obj_t * ui_rfmon_run_btn = NULL;
lv_obj_t * ui_rfmon_header = NULL;
lv_obj_t * ui_rfmon_list = NULL;
lv_obj_t * ui_rfmon_status_label = NULL;

static lv_obj_t * rfm_card = NULL;
static lv_obj_t * rfm_val[4] = { NULL };          // 四个统计数字标签
static lv_obj_t * rfm_rows[UI_RFMON_MAX_ROWS] = { NULL };
static lv_obj_t * rfm_row_left[UI_RFMON_MAX_ROWS] = { NULL };
static lv_obj_t * rfm_row_right[UI_RFMON_MAX_ROWS] = { NULL };

// 列表结构指纹：任一变化 -> 整表重建
static uint32_t rfm_list_version = 0xFFFFFFFFu;
static bool     rfm_list_deauth  = false;
static int      rfm_list_n       = -1;

// 统计数字的缓存：先比后写
static uint32_t rfm_cnt_shown[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };

// ---------------------------------------------------------------------------
static void rfm_fmt_count(uint32_t v, char * out, size_t out_sz)
{
    if (v >= 100000u) lv_snprintf(out, out_sz, "%uK", (unsigned)(v / 1000u));
    else              lv_snprintf(out, out_sz, "%u", (unsigned)v);
}

static void rfm_set_label_if_changed(lv_obj_t * lbl, const char * text)
{
    if (lbl == NULL || text == NULL) return;
    const char * cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, text) == 0) return;   // 没变不写：防无效失效
    lv_label_set_text(lbl, text);
}

static void rfm_set_color_if_changed(lv_obj_t * lbl, uint32_t rgb)
{
    if (lbl == NULL) return;
    lv_color_t want = lv_color_hex(rgb);
    lv_color_t cur  = lv_obj_get_style_text_color(lbl, LV_STATE_DEFAULT);
    if (lv_color_to32(cur) == lv_color_to32(want)) return;   // 两侧都过 to32：16 位色深量化
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
    ui_rfmon_on_toggle();
}

// ---------------------------------------------------------------------------
void ui_rfmon_screen_init(void)
{
    ui_rfmon = uik_screen_init();

    // ---- 顶栏：右侧 Start/Stop ----
    uik_topbar(ui_rfmon, "RF Monitor", LV_SYMBOL_PLAY " Start",
               back_btn_ui_cb, run_btn_ui_cb, &ui_rfmon_back_btn, &ui_rfmon_run_btn);

    // ---- 统计卡片：4 列 ----
    rfm_card = uik_panel(ui_rfmon, UIK_LEFT, RFM_CARD_Y, UIK_CONTENT_W, RFM_CARD_H);
    static const char * cap[4] = { "BEACON", "PROBE", "DEAUTH", "DISAS" };
    for (int i = 0; i < 4; i++) {
        int x = i * RFM_COL_W;
        rfm_val[i] = uik_label(rfm_card, "0", x, 0, UIK_C_TEXT,
                               UIK_FONT_BIG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(rfm_val[i], RFM_COL_W);
        lv_obj_align(rfm_val[i], LV_ALIGN_TOP_LEFT, x, 4);
        lv_obj_t * c = uik_label(rfm_card, cap[i], x, 0, UIK_C_TEXT_DIM,
                                 UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(c, RFM_COL_W);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, 28);
        // 列间竖线（第 1..3 列左缘），把四格分开
        if (i > 0) uik_separator(rfm_card, x, 6, 1);
    }

    // ---- 动态标题 ----
    ui_rfmon_header = uik_label(ui_rfmon, "Idle", 0, 0, UIK_C_TEXT_DIM,
                                UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_rfmon_header, UIK_CONTENT_W);
    lv_obj_align(ui_rfmon_header, LV_ALIGN_TOP_LEFT, UIK_LEFT, RFM_HDR_Y);
    lv_label_set_long_mode(ui_rfmon_header, LV_LABEL_LONG_DOT);

    // ---- 列表（4 x 25 行整；不套外壳面板，理由同 ui_wscan）----
    ui_rfmon_list = lv_list_create(ui_rfmon);
    lv_obj_set_size(ui_rfmon_list, UIK_CONTENT_W, RFM_LIST_H);
    lv_obj_align(ui_rfmon_list, LV_ALIGN_TOP_LEFT, UIK_LEFT, RFM_LIST_Y);
    lv_obj_set_scrollbar_mode(ui_rfmon_list, LV_SCROLLBAR_MODE_AUTO);
    uik_list(ui_rfmon_list);

    // ---- 状态条 ----
    ui_rfmon_status_label = uik_status_bar(ui_rfmon);
    lv_label_set_text(ui_rfmon_status_label,
                      "RX only " LV_SYMBOL_BULLET " tap Start (WiFi pauses while monitoring)");

    for (int i = 0; i < UI_RFMON_MAX_ROWS; i++) {
        rfm_rows[i] = NULL;
        rfm_row_left[i] = NULL;
        rfm_row_right[i] = NULL;
    }
    rfm_list_version = 0xFFFFFFFFu;
    rfm_list_n = -1;
    for (int i = 0; i < 4; i++) rfm_cnt_shown[i] = 0xFFFFFFFFu;

    ui_rfmon_on_screen_ready();
}

// ---------------------------------------------------------------------------
void ui_rfmon_set_running(bool running)
{
    if (ui_rfmon_run_btn == NULL) return;
    lv_obj_t * lbl = lv_obj_get_child(ui_rfmon_run_btn, 0);
    if (lbl == NULL) return;
    if (running) {
        lv_label_set_text(lbl, LV_SYMBOL_STOP " Stop");
        // Stop 是"破坏当前会话"的动作，用红色；Start 用主色蓝
        uik_btn_states(ui_rfmon_run_btn, UIK_C_DANGER);
    } else {
        lv_label_set_text(lbl, LV_SYMBOL_PLAY " Start");
        uik_btn_states(ui_rfmon_run_btn, UIK_C_PRIMARY);
    }
}

void ui_rfmon_set_counts(uint32_t beacons, uint32_t probes,
                         uint32_t deauths, uint32_t disassoc)
{
    if (ui_rfmon == NULL) return;
    uint32_t v[4] = { beacons, probes, deauths, disassoc };
    char buf[12];
    for (int i = 0; i < 4; i++) {
        if (rfm_cnt_shown[i] == v[i]) continue;
        rfm_cnt_shown[i] = v[i];
        rfm_fmt_count(v[i], buf, sizeof(buf));
        rfm_set_label_if_changed(rfm_val[i], buf);
        // DEAUTH / DISAS 两列有货就变红，归零恢复
        if (i >= 2)
            rfm_set_color_if_changed(rfm_val[i], (v[i] > 0) ? UIK_C_ERR_TEXT : UIK_C_TEXT);
    }
}

void ui_rfmon_set_header(const char * text, uint32_t color)
{
    if (ui_rfmon_header == NULL || text == NULL) return;
    rfm_set_label_if_changed(ui_rfmon_header, text);
    rfm_set_color_if_changed(ui_rfmon_header, color);
}

void ui_rfmon_set_status(const char * text, uint32_t color)
{
    if (ui_rfmon_status_label == NULL || text == NULL) return;
    rfm_set_label_if_changed(ui_rfmon_status_label, text);
    rfm_set_color_if_changed(ui_rfmon_status_label, color);
}

// ---------------------------------------------------------------------------
//  重建列表：结构（version/mode/n）变了才走到这里
// ---------------------------------------------------------------------------
static void rfm_rebuild_list(int n, const ui_rfmon_entry_t * entries)
{
    if (ui_rfmon_list == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_RFMON_MAX_ROWS) n = UI_RFMON_MAX_ROWS;

    lv_obj_clean(ui_rfmon_list);
    lv_obj_scroll_to_y(ui_rfmon_list, 0, LV_ANIM_OFF);
    for (int i = 0; i < UI_RFMON_MAX_ROWS; i++) {
        rfm_rows[i] = NULL;
        rfm_row_left[i] = NULL;
        rfm_row_right[i] = NULL;
    }

    for (int i = 0; i < n; i++) {
        lv_obj_t * row = lv_list_add_btn(ui_rfmon_list, NULL, entries[i].name);
        lv_obj_t * right = lv_label_create(row);
        lv_label_set_text(right, entries[i].right);
        lv_obj_set_width(right, 60);
        lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
        uik_list_row(row);   // 会把两个 label 都刷成 BODY/TEXT，右列随后降档
        lv_obj_set_style_text_font(right, UIK_FONT_HINT, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(right, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);   // 只读行，不给按下高亮

        rfm_rows[i] = row;
        rfm_row_left[i] = lv_obj_get_child(row, 0);      // lv_list_add_btn 建的文本 label
        rfm_row_right[i] = right;
    }
}

void ui_rfmon_set_list(uint32_t version, bool deauth_mode,
                       int n, const ui_rfmon_entry_t * entries)
{
    if (ui_rfmon_list == NULL) return;
    if (n < 0) n = 0;
    if (n > UI_RFMON_MAX_ROWS) n = UI_RFMON_MAX_ROWS;

    if (version != rfm_list_version || deauth_mode != rfm_list_deauth || n != rfm_list_n) {
        rfm_list_version = version;
        rfm_list_deauth  = deauth_mode;
        rfm_list_n       = n;
        rfm_rebuild_list(n, entries);
        return;
    }

    // 结构没变：只做原地文本更新（先比后写）
    for (int i = 0; i < n; i++) {
        rfm_set_label_if_changed(rfm_row_left[i], entries[i].name);
        rfm_set_label_if_changed(rfm_row_right[i], entries[i].right);
    }
}

// ---------------------------------------------------------------------------
void ui_rfmon_screen_destroy(void)
{
    if (ui_rfmon) lv_obj_del(ui_rfmon);
    ui_rfmon = NULL;
    ui_rfmon_back_btn = NULL;
    ui_rfmon_run_btn = NULL;
    ui_rfmon_header = NULL;
    ui_rfmon_list = NULL;
    ui_rfmon_status_label = NULL;
    rfm_card = NULL;
    for (int i = 0; i < 4; i++) {
        rfm_val[i] = NULL;
        rfm_cnt_shown[i] = 0xFFFFFFFFu;
    }
    for (int i = 0; i < UI_RFMON_MAX_ROWS; i++) {
        rfm_rows[i] = NULL;
        rfm_row_left[i] = NULL;
        rfm_row_right[i] = NULL;
    }
    rfm_list_version = 0xFFFFFFFFu;
    rfm_list_n = -1;
}
