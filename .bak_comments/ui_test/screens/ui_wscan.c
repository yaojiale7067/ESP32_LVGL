// ============================================================================
//  ui_wscan.c - WiFi 信道扫描屏（2.4GHz 信道占用分析）
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]      WiFi Channels      [⟳ Scan]
//      40..52    汇总行："26 AP · best CH 11 · 1/6/11: CH 6"
//      56..138   占用图（uik_panel，pad 压到 0 → 内容区 310x80）
//                    2          ← 柱顶数字 = 该信道 AP 数（0 不显示）
//                   ██
//                   ██ ▄▄       ← 柱高按本次最大值归一化；颜色按绝对占用分档
//          ──────────────────    ← 基线 y=67
//           1  2  3 ... 12 13    ← 信道号 y=68..79
//      142..154  详情标题："CH 11 · 3 AP · best -42 dBm"
//      158..208  该信道上的 AP 列表（两行整，可滚动）
//      212..239  状态提示
//
//  设计取舍：
//    1) 可点的是**整根槽位**（23x55 的透明块），不是那根 15px 宽的柱子。
//       空信道的柱子只有 3px 高，拿它当触摸目标基本点不中；槽位永远 55px。
//       柱子自己关掉 CLICKABLE，点击落到父槽位上，按下时整列亮一下作为反馈。
//    2) 柱顶计数是柱子的**兄弟节点**（都挂在图面板上）而不是柱子的子节点：
//       最高那根柱子的计数要落在内容区 y=0，已经跑到柱子外面了，
//       当子对象会被父坐标系带偏甚至裁掉。
//    3) 颜色按**绝对**占用分档（0 / 1-2 / 3-5 / >=6），不按相对值：
//       "比旁边那根高"没有意义，"有 6 个 AP 挤在这"才有意义。
//       柱高才按相对值归一化，那是为了在 55px 里把差异拉开。
//    4) 推荐信道 = 亮蓝 1px 描边 + 信道号变蓝；选中信道 = 白色 2px 描边 + 信道号变白。
//       两者同时成立时描边取选中态（白色更醒目），推荐态由信道号颜色继续保留。
// ============================================================================

#include "ui_wscan.h"
#include "../ui.h"       // ui_wifi / ui_wifi_screen_init / _ui_screen_change
#include "../ui_kit.h"

lv_obj_t * ui_wscan = NULL;
lv_obj_t * ui_wscan_back_btn = NULL;
lv_obj_t * ui_wscan_scan_btn = NULL;
lv_obj_t * ui_wscan_summary = NULL;
lv_obj_t * ui_wscan_detail_hdr = NULL;
lv_obj_t * ui_wscan_detail = NULL;
lv_obj_t * ui_wscan_status_label = NULL;

// 每根柱子四个对象：可点槽位 / 柱体 / 柱顶计数 / 信道号。下标 0..12 对应信道 1..13。
static lv_obj_t * wsc_slot[WSC_CH_NUM];
static lv_obj_t * wsc_bar[WSC_CH_NUM];
static lv_obj_t * wsc_cnt[WSC_CH_NUM];
static lv_obj_t * wsc_chlbl[WSC_CH_NUM];
static lv_obj_t * wsc_chart = NULL;

static uint8_t wsc_best_ch = 0;      // 0 = 不标记推荐信道
static uint8_t wsc_sel_ch  = 0;      // 0 = 未选中

// ---------------------------------------------------------------------------
//  占用分档配色。阈值是绝对的，理由见文件头第 3 条。
// ---------------------------------------------------------------------------
static uint32_t wsc_load_color(uint8_t cnt)
{
    if (cnt == 0) return UIK_C_SURFACE_HI;   // 空信道：暗柱，只表示"这里没东西"
    if (cnt <= 2) return UIK_C_SUCCESS;      // 宽松
    if (cnt <= 5) return UIK_ACCENT_TEMP;    // 拥挤（琥珀）
    return UIK_C_DANGER;                     // 严重（暗红）
}

// ---------------------------------------------------------------------------
//  描边 / 信道号颜色：由 best_ch 与 sel_ch 共同决定，任一变化就整体重刷。
//  ★ 这里写本地样式而不是挂共享样式：LVGL 的样式属性是替换语义，
//    而且 best/sel 每次扫描都会变，反复 lv_obj_add_style 会一直往 styles
//    数组里塞（lv_obj_add_style 不去重，见 ui_kit.c 里的说明）。
// ---------------------------------------------------------------------------
static void wsc_apply_decor(void)
{
    for (int i = 0; i < WSC_CH_NUM; i++) {
        uint8_t ch = (uint8_t)(i + 1);
        bool sel  = (ch == wsc_sel_ch);
        bool best = (ch == wsc_best_ch);

        if (wsc_bar[i] != NULL) {
            if (sel) {
                lv_obj_set_style_border_width(wsc_bar[i], 2, LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(wsc_bar[i], lv_color_hex(UIK_C_TEXT),
                                              LV_STATE_DEFAULT);
            } else if (best) {
                lv_obj_set_style_border_width(wsc_bar[i], 1, LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(wsc_bar[i], lv_color_hex(UIK_C_PRIMARY_HI),
                                              LV_STATE_DEFAULT);
            } else {
                lv_obj_set_style_border_width(wsc_bar[i], 0, LV_STATE_DEFAULT);
            }
        }
        if (wsc_chlbl[i] != NULL) {
            uint32_t c = sel ? UIK_C_TEXT : (best ? UIK_C_PRIMARY_HI : UIK_C_TEXT_DIM);
            lv_obj_set_style_text_color(wsc_chlbl[i], lv_color_hex(c), LV_STATE_DEFAULT);
        }
    }
}

// ---------------------------------------------------------------------------
//  事件
// ---------------------------------------------------------------------------
static void bar_slot_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // user_data 里直接存信道号（1..13），比"用 target 反查数组下标"稳
    uint8_t ch = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (ch >= 1 && ch <= WSC_CH_NUM) ui_wscan_on_channel_selected(ch);
}

// 返回 WiFi 屏（这是它的上一级），不是主菜单
static void back_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_wifi_screen_init);
}

// 顶栏"重新扫描"：直接回调业务层（和 ui_file.c 调 file_send_selected() 一个路子）。
// 这屏是懒创建的（不在 ui_init 里），所以不能靠 original_setup() 事后补绑事件。
static void scan_btn_ui_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_wscan_on_scan_requested();
}

// ---------------------------------------------------------------------------
//  一根柱子的槽位（可点透明块）+ 柱体
// ---------------------------------------------------------------------------
static void wsc_make_slot(lv_obj_t * chart, int i)
{
    lv_obj_t * slot = lv_obj_create(chart);
    lv_obj_remove_style_all(slot);
    // 槽位盖住"基线以上整列"（含柱顶计数那 12px），所以 y 从 0 起、高 67
    lv_obj_set_size(slot, WSC_SLOT, WSC_SLOT_H);
    lv_obj_align(slot, LV_ALIGN_TOP_LEFT, WSC_SLOT_X(i), 0);
    lv_obj_set_style_radius(slot, UIK_RADIUS_SM, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(slot, 0, LV_STATE_DEFAULT);
    // 平时全透明；按下时整列亮一下 —— 这是"这一列可以点"的唯一反馈，
    // 只给 15px 宽的柱子做按下效果，手指盖上去根本看不出来。
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slot, lv_color_hex(UIK_C_SURFACE_HI), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(slot, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(slot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(slot, bar_slot_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(i + 1));
    wsc_slot[i] = slot;

    lv_obj_t * bar = lv_obj_create(slot);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, WSC_BAR_W, WSC_BAR_MINH);
    // 底部对齐槽位下沿（= 基线上方一行），所以柱高变化时只会往上长
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(bar, UIK_RADIUS_SM, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UIK_C_SURFACE_HI), LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);   // 点击交给槽位
    wsc_bar[i] = bar;
}

// ---------------------------------------------------------------------------
void ui_wscan_screen_init(void)
{
    ui_wscan = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_wscan, "WiFi Channels", LV_SYMBOL_REFRESH " Scan",
               back_btn_ui_cb, scan_btn_ui_cb, &ui_wscan_back_btn, &ui_wscan_scan_btn);

    // ---- 汇总行 ----
    // ★ 先 set_width 再 align：uik_label() 内部已经 align 过一次，但那时宽度还是
    //   LV_SIZE_CONTENT，改完宽度必须重排一次（和 ui_dht11.c 里的做法一致）。
    ui_wscan_summary = uik_label(ui_wscan, "Tap Scan to survey 2.4 GHz",
                                 0, 0, UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_wscan_summary, UIK_CONTENT_W);
    lv_obj_align(ui_wscan_summary, LV_ALIGN_TOP_LEFT, UIK_LEFT, WSC_SUM_Y);
    lv_label_set_long_mode(ui_wscan_summary, LV_LABEL_LONG_DOT);

    // ---- 占用图 ----
    wsc_chart = uik_panel(ui_wscan, UIK_LEFT, WSC_CHART_Y, UIK_CONTENT_W, WSC_CHART_H);
    // pad 压到 0，只留 1px 边框，内容区正好 310x80，子对象坐标全按这个算
    lv_obj_set_style_pad_all(wsc_chart, 0, LV_STATE_DEFAULT);
    // ★ 面板自己不能是可点的：lv_indev_search_obj() 在"没有子对象命中"时会把
    //   面板自己返回出去（只要它有 CLICKABLE），于是点在信道号那一排
    //   （y=68..79，槽位只到 66）会让整块图闪一下按下态，却什么也不发生。
    //   去掉之后子对象（槽位）照样各自可点 —— 命中测试只看"点在父对象范围内"，
    //   不看父对象自己可不可点。
    lv_obj_clear_flag(wsc_chart, LV_OBJ_FLAG_CLICKABLE);

    uik_separator(wsc_chart, 0, WSC_BASE_Y, WSC_CONTENT_W);

    for (int i = 0; i < WSC_CH_NUM; i++) {
        wsc_make_slot(wsc_chart, i);

        // 柱顶计数：横向和槽位同宽居中，纵向在 set_bars 里跟着柱顶走
        wsc_cnt[i] = uik_label(wsc_chart, "", WSC_SLOT_X(i),
                               WSC_BAR_TOPLIM - WSC_CHLBL_H,
                               UIK_C_TEXT, UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(wsc_cnt[i], WSC_SLOT);

        // 信道号
        char num[4];
        lv_snprintf(num, sizeof(num), "%d", i + 1);
        wsc_chlbl[i] = uik_label(wsc_chart, num, WSC_SLOT_X(i), WSC_CHLBL_Y,
                                 UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(wsc_chlbl[i], WSC_SLOT);
    }

    // ---- 详情标题 + 列表 ----
    ui_wscan_detail_hdr = uik_label(ui_wscan, "Select a channel",
                                    0, 0, UIK_C_TEXT_DIM, UIK_FONT_HINT,
                                    LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(ui_wscan_detail_hdr, UIK_CONTENT_W);
    lv_obj_align(ui_wscan_detail_hdr, LV_ALIGN_TOP_LEFT, UIK_LEFT, WSC_DETHDR_Y);
    lv_label_set_long_mode(ui_wscan_detail_hdr, LV_LABEL_LONG_DOT);

    // 不套外层面板：uik_list() 已经把底色/圆角/滚动条统一好了，
    // 再套一层会白吃 6px 纵向空间，而 WSC_LIST_H(50) 正好是两整行（2 x 25），
    // 少 6px 就会露出半行，看着像渲染坏了（见 ui_kit.c 的行高注释）。
    ui_wscan_detail = lv_list_create(ui_wscan);
    lv_obj_set_size(ui_wscan_detail, UIK_CONTENT_W, WSC_LIST_H);
    lv_obj_align(ui_wscan_detail, LV_ALIGN_TOP_LEFT, UIK_LEFT, WSC_LIST_Y);
    lv_obj_set_scrollbar_mode(ui_wscan_detail, LV_SCROLLBAR_MODE_AUTO);
    uik_list(ui_wscan_detail);

    // ---- 状态条 ----
    ui_wscan_status_label = uik_status_bar(ui_wscan);
    lv_label_set_text(ui_wscan_status_label,
                      "2.4 GHz only  " LV_SYMBOL_BULLET "  tap a bar for details");

    wsc_best_ch = 0;
    wsc_sel_ch  = 0;
    wsc_apply_decor();

    // 建屏完毕，把已经缓存的扫描结果补画上去（见 ui_wscan.h 的说明）
    ui_wscan_on_screen_ready();
}

// ---------------------------------------------------------------------------
void ui_wscan_set_bars(const uint8_t * per_ch1_13, uint8_t best_ch)
{
    if (ui_wscan == NULL || per_ch1_13 == NULL) return;

    uint8_t mx = 0;
    for (int i = 0; i < WSC_CH_NUM; i++) {
        if (per_ch1_13[i] > mx) mx = per_ch1_13[i];
    }

    for (int i = 0; i < WSC_CH_NUM; i++) {
        uint8_t cnt = per_ch1_13[i];
        // 归一化：0 -> 最小高度（留个可见的小截），mx -> 满高
        int h = (mx == 0)
                    ? WSC_BAR_MINH
                    : WSC_BAR_MINH +
                      (int)((uint32_t)cnt * (WSC_BAR_MAXH - WSC_BAR_MINH) / (uint32_t)mx);
        if (h > WSC_BAR_MAXH) h = WSC_BAR_MAXH;
        if (h < WSC_BAR_MINH) h = WSC_BAR_MINH;

        int bar_top = WSC_BAR_BOT - h + 1;      // 柱顶在图内容区里的 y

        if (wsc_bar[i] != NULL) {
            lv_obj_set_height(wsc_bar[i], (lv_coord_t)h);
            lv_obj_set_style_bg_color(wsc_bar[i], lv_color_hex(wsc_load_color(cnt)),
                                      LV_STATE_DEFAULT);
        }
        if (wsc_cnt[i] != NULL) {
            // 计数贴在柱顶上方 12px：最高那根（h=55）正好落在 y=0，不会跑出内容区
            lv_obj_align(wsc_cnt[i], LV_ALIGN_TOP_LEFT, WSC_SLOT_X(i),
                         bar_top - WSC_CHLBL_H);
            char buf[4];
            // 0 不写数字：十三根柱子全顶着"0"只会变成噪声，矮柱本身就说明是空的
            if (cnt == 0) buf[0] = '\0';
            else lv_snprintf(buf, sizeof(buf), "%u", (unsigned)cnt);
            lv_label_set_text(wsc_cnt[i], buf);
        }
    }

    wsc_best_ch = (best_ch >= 1 && best_ch <= WSC_CH_NUM) ? best_ch : 0;
    wsc_apply_decor();
}

// ---------------------------------------------------------------------------
void ui_wscan_select_channel(uint8_t ch)
{
    wsc_sel_ch = (ch >= 1 && ch <= WSC_CH_NUM) ? ch : 0;
    wsc_apply_decor();
}

uint8_t ui_wscan_selected_channel(void)
{
    return wsc_sel_ch;
}

// ---------------------------------------------------------------------------
void ui_wscan_set_scanning(bool scanning)
{
    if (ui_wscan_scan_btn == NULL) return;
    if (scanning) lv_obj_add_state(ui_wscan_scan_btn, LV_STATE_DISABLED);
    else          lv_obj_clear_state(ui_wscan_scan_btn, LV_STATE_DISABLED);
}

void ui_wscan_set_summary(const char * text)
{
    if (ui_wscan_summary == NULL || text == NULL) return;
    lv_label_set_text(ui_wscan_summary, text);
}

void ui_wscan_set_detail_hdr(const char * text)
{
    if (ui_wscan_detail_hdr == NULL || text == NULL) return;
    lv_label_set_text(ui_wscan_detail_hdr, text);
}

void ui_wscan_set_status(const char * text, uint32_t color)
{
    if (ui_wscan_status_label == NULL || text == NULL) return;
    lv_label_set_text(ui_wscan_status_label, text);
    lv_obj_set_style_text_color(ui_wscan_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
}

// ---------------------------------------------------------------------------
void ui_wscan_screen_destroy(void)
{
    if (ui_wscan) lv_obj_del(ui_wscan);
    ui_wscan = NULL;
    ui_wscan_back_btn = NULL;
    ui_wscan_scan_btn = NULL;
    ui_wscan_summary = NULL;
    ui_wscan_detail_hdr = NULL;
    ui_wscan_detail = NULL;
    ui_wscan_status_label = NULL;
    wsc_chart = NULL;
    for (int i = 0; i < WSC_CH_NUM; i++) {
        wsc_slot[i]  = NULL;
        wsc_bar[i]   = NULL;
        wsc_cnt[i]   = NULL;
        wsc_chlbl[i] = NULL;
    }
    wsc_best_ch = 0;
    wsc_sel_ch  = 0;
}
