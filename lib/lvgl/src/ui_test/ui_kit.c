// ============================================================================
//  ui_kit.c - 统一 UI 组件库的实现（详见 ui_kit.h）
//
//  ★ 关于主题（两层，各管一件事）
//
//  1) LVGL 默认主题：ui.c 里用 lv_theme_default_init() 建，但**配色和字号都换成
//     ui_kit 的值**。它的价值在于给下面这些控件的"子部件"一份基础样式，
//     否则它们会是裸对象（没有鼠标/触摸状态、没有光标、没有占位符颜色）：
//         lv_textarea  → cursor / placeholder / scrollbar
//         lv_list      → item 的内边距与分隔线
//         lv_keyboard  → 键帽 / 按下态 / checked 态
//     本文件再用 uik_* 把这几个控件的配色覆盖成工程规范（见 uik_keyboard /
//     uik_list / uik_ta）。两者是"基础 + 覆盖"的关系，不是二选一。
//
//  2) ui_kit 自己的共享样式（本文件的 st_*）：屏幕底色、滚动条、禁用态、
//     输入框焦点环。这几样在默认主题里没有合适的等价物，或者默认值不适合
//     320x240 的深色界面。
//
//  ★ 样式优先级的两个坑（改这里的代码前务必知道）：
//    · 本地样式（lv_obj_set_style_*）排在数组前面，get_prop_core() 从头找，
//      第一个命中就返回 —— 所以本地样式能盖掉主题样式。
//    · 状态权重：选择器上的状态越具体权重越高（FOCUSED > PRESSED > DEFAULT）。
//      只写 LV_PART_CURSOR 的样式会被主题里 LV_PART_CURSOR|LV_STATE_FOCUSED
//      压过去，必须带上同样的状态才生效。
// ============================================================================

#include "ui_kit.h"

// ---------------------------------------------------------------------------
//  共享样式（static，全工程只有一份）
// ---------------------------------------------------------------------------
static lv_style_t st_screen;          // 屏幕底色
static lv_style_t st_scrollbar;       // 所有可滚动容器共用
static lv_style_t st_disabled;        // 禁用：背景压到 BG 色，文字变灰
static lv_style_t st_ta_focus;        // 输入框获得焦点的描边

static bool kit_theme_ready = false;

static void kit_theme_init(void)
{
    if (kit_theme_ready) return;
    kit_theme_ready = true;

    // 屏幕：纯色、无圆角、无内边距
    lv_style_init(&st_screen);
    lv_style_set_bg_color(&st_screen, lv_color_hex(UIK_C_BG));
    lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);
    lv_style_set_text_color(&st_screen, lv_color_hex(UIK_C_TEXT));
    lv_style_set_text_font(&st_screen, UIK_FONT_BODY);
    lv_style_set_pad_all(&st_screen, 0);
    lv_style_set_border_width(&st_screen, 0);
    lv_style_set_radius(&st_screen, 0);

    // 滚动条：细一点、圆头、半透明。
    // 默认主题那根是 LV_OPA_40 + 5px 宽 + 7px pad，在 320x240 上太抢眼。
    lv_style_init(&st_scrollbar);
    lv_style_set_bg_color(&st_scrollbar, lv_color_hex(UIK_C_SURFACE_HI));
    lv_style_set_bg_opa(&st_scrollbar, LV_OPA_60);
    lv_style_set_width(&st_scrollbar, 3);
    lv_style_set_radius(&st_scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&st_scrollbar, 1);

    // 按下态不用共享样式：LVGL 的样式没有"叠加一层"的语义，
    // 给 PRESSED 挂一个 bg_color+bg_opa 会【替换】掉背景，而不是叠在上面，
    // 所以按下瞬间按钮会变成一块半透明白。正确做法是算好一个提亮色，
    // 用本地样式写进 LV_STATE_PRESSED（见 uik_btn_states）。

    // 禁用态：底色压回 SURFACE（比屏幕底色亮一档，仍看得见"这里有个按钮"），
    // 描边用最暗的 BORDER，文字用 FAINT。
    // ★ 不能用 UIK_C_BG：Send/Edit 这类按钮是直接放在屏幕底色上的，
    //   底色相同就等于把按钮画没了，用户不知道那里本来有个按钮。
    lv_style_init(&st_disabled);
    lv_style_set_bg_color(&st_disabled, lv_color_hex(UIK_C_SURFACE));
    lv_style_set_bg_opa(&st_disabled, LV_OPA_COVER);
    lv_style_set_text_color(&st_disabled, lv_color_hex(UIK_C_TEXT_FAINT));
    lv_style_set_border_color(&st_disabled, lv_color_hex(UIK_C_BORDER));

    // 输入框焦点环
    lv_style_init(&st_ta_focus);
    lv_style_set_border_color(&st_ta_focus, lv_color_hex(UIK_C_PRIMARY));
    lv_style_set_border_width(&st_ta_focus, 2);

    // 列表行的紧凑内边距不用共享样式实现：lv_list_add_btn() 给行设的
    // flex_flow / 百分比宽都是 local style，而 lv_obj_remove_style(row, NULL, 0)
    // 会连 local style 一起 reset 掉，行会塌成默认大小。
    // 所以 uik_list_row() 直接用 lv_obj_set_style_* 写 local style 覆盖主题。
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_screen_init(void)
{
    kit_theme_init();

    lv_obj_t * scr = lv_obj_create(NULL);
    // 去掉 lv_obj_create(NULL) 带出来的默认主题样式（浅色卡片那一套），
    // 换成 ui_kit 自己的：这样"屏幕底色/默认字体/滚动条"全屏统一。
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &st_screen, 0);
    lv_obj_add_style(scr, &st_scrollbar, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_separator(lv_obj_t * parent, int x, int y, int w)
{
    lv_obj_t * line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, w, 1);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

// ---------------------------------------------------------------------------
//  底部状态条：一条 1px 分隔线 + 居中滚动的提示文字。
//  分隔线把"内容区"和"状态区"分开，视觉上不再是一条悬空的灰字。
// ---------------------------------------------------------------------------
lv_obj_t * uik_status_bar(lv_obj_t * parent)
{
    uik_separator(parent, UIK_LEFT, UIK_STATUS_Y, UIK_CONTENT_W);

    lv_obj_t * lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, UIK_W - 2 * UIK_PAD);
    lv_obj_set_style_text_font(lbl, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    // 长文本横向滚动，避免换行把布局顶乱
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl, "");
    // 分隔线占 1px，文字从下面 6px 开始，正好在 212..239 这段里居中
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, UIK_PAD, UIK_STATUS_Y + 6);
    return lbl;
}

// ---------------------------------------------------------------------------
//  顶栏：左"返回" / 中"标题" / 右"可选按钮"，底下一条分隔线。
//  返回按钮用 NEUTRAL 灰（次要操作），右侧按钮默认 PRIMARY 蓝（主操作）。
// ---------------------------------------------------------------------------
lv_obj_t * uik_topbar(lv_obj_t * parent, const char * title,
                      const char * right_btn_text,
                      lv_event_cb_t back_cb, lv_event_cb_t right_cb,
                      lv_obj_t ** back_out, lv_obj_t ** right_out)
{
    if (back_out)  *back_out  = NULL;
    if (right_out) *right_out = NULL;

    lv_obj_t * bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    // 背景条正好铺到内容起点
    lv_obj_set_size(bar, UIK_W, UIK_CONTENT_Y);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    // 左：返回
    lv_obj_t * back = lv_btn_create(bar);
    lv_obj_set_size(back, 62, UIK_TOPBAR_H);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, UIK_PAD, UIK_TOPBAR_Y);
    lv_obj_set_style_radius(back, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(back, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(back, 0, LV_STATE_DEFAULT);
    uik_btn_states(back, UIK_C_NEUTRAL);
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
        // 限宽 + 截断，标题太长时不会压到两侧按钮上。
        // 标题是居中的，所以左右可用空间对称：
        //   左侧：「返回」按钮结束于 x=66，留 4px → 标题左沿 ≥ 70 → 宽 ≤ 180
        //   右侧：有按钮时按钮始于 x=244，留 4px → 右沿 ≤ 240 → 宽 ≤ 160
        //         没按钮时右边界 316，留 4px   → 右沿 ≤ 312 → 宽 ≤ 304
        // 取两者较小值。180 和 160 都放得下 "Temperature / Humidity"(约 150px)。
        int title_w = (right_btn_text != NULL) ? 160 : 180;
        lv_obj_set_width(t, title_w);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, UIK_TOPBAR_Y + 7);
        lv_obj_add_flag(t, LV_OBJ_FLAG_USER_1);      // 标记：标题，便于后续改文案
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    }

    // 右：可选按钮
    lv_obj_t * rbtn = NULL;
    if (right_btn_text != NULL) {
        rbtn = lv_btn_create(bar);
        lv_obj_set_size(rbtn, 72, UIK_TOPBAR_H);
        lv_obj_align(rbtn, LV_ALIGN_TOP_RIGHT, -UIK_PAD, UIK_TOPBAR_Y);
        lv_obj_set_style_radius(rbtn, UIK_RADIUS, LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(rbtn, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(rbtn, 0, LV_STATE_DEFAULT);
        uik_btn_states(rbtn, UIK_C_PRIMARY);
        lv_obj_t * rl = lv_label_create(rbtn);
        lv_label_set_text(rl, right_btn_text);
        lv_obj_set_style_text_font(rl, UIK_FONT_TITLE, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(rl, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_center(rl);
        if (right_cb) lv_obj_add_event_cb(rbtn, right_cb, LV_EVENT_CLICKED, NULL);
    }
    if (right_out) *right_out = rbtn;

    // 顶栏与内容之间的分隔线（画在 bar 的下沿上）
    uik_separator(bar, 0, UIK_CONTENT_Y - 1, UIK_W);

    return bar;      // ★ 返回顶栏容器，旧代码错写成返回 rbtn
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
//  切屏统一入口。
//
//  ★ 为什么不能直接 lv_scr_load()：
//   LVGL 的失效会被**静默丢弃** —— lv_obj_invalidate_area() 内部先调
//   lv_obj_area_is_visible()，对象所属屏幕既不是当前屏、也不是上一个屏、
//   也不是 layer 时直接 return；lv_refr.c 里若正在渲染（rendering_in_progress）
//   也会直接 return 丢掉。
//   本工程的屏幕全是 ui_init() 里一次性建好、之后不再重建（_ui_screen_change
//   只在 *target == NULL 时才建），所以建屏阶段的失效全被丢掉，进屏那一帧
//   就只靠 scr_load_internal() 末尾那一次全屏失效兜底。
//   只要那一次没覆盖完整 / 被丢弃，就会残留一小块上一次的画面，
//   表现就是"某块区域显示错位，切走再切回来就好了"。
//
//   这里在加载之后显式再失效一次整屏，让每次进屏都等价于"切走再回来"。
//   若 scr_load_internal 那次已经成功，这句会被 _lv_inv_area 判为已覆盖、
//   零开销；只有当那次被丢弃时它才真正补上。整屏重画约 15~20ms，仅切屏时发生。
// ---------------------------------------------------------------------------
void uik_screen_load(lv_obj_t * scr)
{
    if (scr == NULL) return;
    lv_scr_load(scr);
    lv_obj_invalidate(scr);
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_panel(lv_obj_t * parent, int x, int y, int w, int h)
{
    lv_obj_t * p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_STATE_DEFAULT);
    // 1px 暗边框：只用来界定范围，不承担层级（层级由明度台阶给）
    lv_obj_set_style_border_width(p, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(p, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(p, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(p, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// ---------------------------------------------------------------------------
//  按下反馈 + 禁用态。
//
//  按下【提亮】而不是压暗：默认主题给按钮挂的是 color_filter 压暗 35，
//  在 0x1A1A1A 这种底色上，一个本来就暗的按钮再压暗 35 几乎看不出变化，
//  手指按下去像没反应。提亮一档则一眼可见。
//
//  ★ 不能用"给 PRESSED 挂一个 bg_color + 半透明 bg_opa 的共享样式"来做：
//    LVGL 的样式属性是【替换】语义，bg_opa=10 会让整块背景变成近乎透明，
//    按钮会闪一下消失。正确做法是预先把提亮色算好，写进 PRESSED 的本地样式。
// ---------------------------------------------------------------------------
static uint32_t uik_lighten(uint32_t rgb, lv_opa_t lvl)
{
    return lv_color_to32(lv_color_lighten(lv_color_hex(rgb), lvl));
}

// 标记位：共享样式只挂一次。
// lv_obj_add_style() 不做去重，每调一次就往 styles 数组里插一项（还会 realloc），
// 而模式按钮/启用按钮这类"状态会变"的控件每次改色都会走这里，
// 不去重的话数组会一直涨 —— 那是内存泄漏，不是样式问题。
#define UIK_FLAG_STATES_INSTALLED  LV_OBJ_FLAG_USER_4

void uik_btn_states(lv_obj_t * btn, uint32_t base)
{
    kit_theme_init();
    if (btn == NULL) return;

    if (!lv_obj_has_flag(btn, UIK_FLAG_STATES_INSTALLED)) {
        lv_obj_add_style(btn, &st_disabled, LV_STATE_DISABLED);
        // 默认主题给按钮挂的 "pressed=压暗35" 滤镜，在深色底上和背景几乎同色，
        // 反而让按下瞬间"消失"。这里显式关掉，只留我们自己的提亮色。
        lv_obj_set_style_color_filter_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_color_filter_opa(btn, LV_OPA_TRANSP, LV_STATE_DISABLED);
        // 默认主题在按下时还会把控件放大 3px（LV_THEME_DEFAULT_GROW）。
        // 在这块 320x240 的屏上，放大意味着文字要重新栅格化，会糊一下；
        // 而且相邻按钮间距只有 4px，放大会挤在一起。关掉。
        lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
        lv_obj_add_flag(btn, UIK_FLAG_STATES_INSTALLED);
    }

    lv_obj_set_style_bg_color(btn, lv_color_hex(base), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);

    // 按下：把基色往白混 20%（lv_color_lighten 的 lvl 就是混合比例）
    lv_obj_set_style_bg_color(btn, lv_color_hex(uik_lighten(base, LV_OPA_20)), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_btn(lv_obj_t * parent, const char * text,
                   int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(b, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(b, 0, LV_STATE_DEFAULT);
    uik_btn_states(b, bg);

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
//  焦点环回调：触摸模式下 LVGL 不会自动置 LV_STATE_FOCUSED（那要有 indev group），
//  所以这里自己维护 LV_STATE_USER_1 当作"聚焦"标记。
// ---------------------------------------------------------------------------
static void ta_focus_ring_cb(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_add_state(ta, LV_STATE_USER_1);
    else                                          lv_obj_clear_state(ta, LV_STATE_USER_1);
}

void uik_ta_focus_ring(lv_obj_t * ta)
{
    kit_theme_init();
    if (ta == NULL) return;
    lv_obj_add_style(ta, &st_ta_focus, LV_STATE_USER_1);
    lv_obj_add_event_cb(ta, ta_focus_ring_cb, LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(ta, ta_focus_ring_cb, LV_EVENT_DEFOCUSED, NULL);
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_ta(lv_obj_t * parent, int x, int y, int w, int h,
                  const char * placeholder, bool one_line)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ta, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ta, 5, LV_STATE_DEFAULT);
    // 光标：LVGL 的 textarea 光标不是"填一块背景"，而是用
    // LV_PART_CURSOR 的**左边框**画一根 2px 竖线（见主题里的 ta_cursor 样式）。
    // 所以只设 bg_color 是看不见的，要设 border_color。
    // 另外主题把这条样式挂在 LV_PART_CURSOR | LV_STATE_FOCUSED 上，
    // 按 get_prop_core() 的权重规则（状态越具体权重越高），
    // 只写 LV_PART_CURSOR 会被主题的 FOCUSED 样式压过 —— 必须带上同样的状态。
    lv_obj_set_style_border_color(ta, lv_color_hex(UIK_C_PRIMARY),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    // 占位文字比 DIM 再弱一档，免得和真输入的内容混淆
    // （部件名是 LV_PART_TEXTAREA_PLACEHOLDER，不是 LV_PART_PLACEHOLDER）
    lv_obj_set_style_text_color(ta, lv_color_hex(UIK_C_TEXT_FAINT),
                                LV_PART_TEXTAREA_PLACEHOLDER);

    // ★ 顺序有讲究：lv_textarea_set_one_line(true) 内部会
    //   lv_obj_set_height(obj, LV_SIZE_CONTENT)，把刚才设的 h 覆盖掉。
    //   证据是实测：传 30 进去，最后量到的是 27（= 12pt 行高 + 上下 pad + 边框）。
    //   所以必须先开 one_line，再把高度按调用方的意图写回去。
    if (one_line) {
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_height(ta, h);
    }
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    uik_ta_focus_ring(ta);
    return ta;
}

// ---------------------------------------------------------------------------
//  只读文本区：终端回显 / 接收缓冲。
//  和 uik_ta 的区别：凹陷底色（比面板更暗，像一块"屏幕"）、绿字、
//  不挂焦点环。仍保留 CLICKABLE 以便拖动滚动（LVGL 的 textarea 只能通过
//  "按住拖动"滚动，不可点时滚不动）。
// ---------------------------------------------------------------------------
lv_obj_t * uik_ta_readonly(lv_obj_t * parent, int x, int y, int w, int h,
                           const char * placeholder)
{
    lv_obj_t * ta = uik_ta(parent, x, y, w, h, placeholder, false);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UIK_C_SUNKEN), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta, lv_color_hex(UIK_C_OK_TEXT), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta, 1, LV_STATE_DEFAULT);
    // 盖掉 uik_ta 挂上的焦点环：USER_1 状态下强制回到普通描边
    lv_obj_set_style_border_color(ta, lv_color_hex(UIK_C_BORDER),
                                  LV_STATE_USER_1);
    lv_obj_set_style_border_width(ta, 1, LV_STATE_USER_1);
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
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);   // 标签不该吃掉点击
    return l;
}

// ---------------------------------------------------------------------------
//  图标徽章：accent 色圆角方块 + 居中符号。
//  符号颜色取深色（不是白色）：徽章本身已经是彩色，白色符号会把彩色"洗掉"，
//  深色符号反而让色块更实、对比更强。
// ---------------------------------------------------------------------------
lv_obj_t * uik_badge(lv_obj_t * parent, int x, int y, int size,
                     const char * symbol, uint32_t accent)
{
    lv_obj_t * b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, size, size);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(b, UIK_RADIUS_SM, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(b, 0, LV_STATE_DEFAULT);
    // ★ 裁掉越界内容：符号字形宽度不一（水滴 11px ~ WiFi 20px），
    //   万一以后换了更宽的符号或改了字号，宁可裁掉，也绝不画到徽章外面
    //   —— 画出去就是"图标出框 / 错位"的观感。
    lv_obj_set_style_clip_corner(b, true, LV_STATE_DEFAULT);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);   // 点击交给整行按钮

    lv_obj_t * s = lv_label_create(b);
    lv_label_set_text(s, symbol ? symbol : "");
    lv_obj_set_style_text_font(s, UIK_FONT_ICON, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s, lv_color_hex(0x101010), LV_STATE_DEFAULT);
    // 居中用 lv_obj_center() 就够：这些符号的 adv_w 等于 box_w（ofs_x 0 或 1），
    // 所以按"字宽"居中和按"墨迹"居中是同一个结果（已逐个量过）。
    lv_obj_center(s);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_accent_bar(lv_obj_t * parent, int x, int y, int w, uint32_t accent)
{
    lv_obj_t * bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, w, 3);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 2, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_menu_row(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, uint32_t bg,
                        lv_event_cb_t cb, void * user_data)
{
    lv_obj_t * row = lv_btn_create(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(row, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    uik_btn_states(row, bg);

    lv_obj_t * l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);

    // 右侧箭头，提示"可以点进去"
    lv_obj_t * arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UIK_C_TEXT_FAINT), LV_STATE_DEFAULT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);

    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    return row;
}

// ---------------------------------------------------------------------------
lv_obj_t * uik_menu_row_icon(lv_obj_t * parent, int x, int y, int w, int h,
                             const char * symbol, const char * title,
                             uint32_t accent, uint32_t bg,
                             lv_event_cb_t cb, void * user_data,
                             lv_obj_t ** title_out)
{
    if (title_out) *title_out = NULL;

    lv_obj_t * row = lv_btn_create(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(row, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    uik_btn_states(row, bg);

    // 徽章在行内垂直居中：(h - BADGE) / 2
    uik_badge(row, 5, (h - UIK_BADGE) / 2, UIK_BADGE, symbol, accent);

    lv_obj_t * l = lv_label_create(row);
    lv_label_set_text(l, title ? title : "");
    lv_obj_set_style_text_font(l, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 5 + UIK_BADGE + 9, 0);

    lv_obj_t * arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    // 实测（真机照片）：用 TEXT_FAINT(0x6A6A6A) 时箭头几乎看不见 ——
    // 这块 TN 屏的可视角度和对比度都比显示器差，深灰在暗底上会糊成一片。
    // 箭头是"这行可以点"的唯一提示，必须看得清，所以提到 DIM 档。
    lv_obj_set_style_text_color(arrow, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);

    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
    if (title_out) *title_out = l;
    return row;
}

// ---------------------------------------------------------------------------
//  软键盘：默认主题那套底色和键帽色跟本工程配色不是一路，
//  弹起来半屏突兀。这里统一成 BG 底 + SURFACE 键帽 + SURFACE_HI 按下。
// ---------------------------------------------------------------------------
void uik_keyboard(lv_obj_t * kb)
{
    kit_theme_init();
    if (kb == NULL) return;

    lv_obj_set_style_bg_color(kb, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(kb, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(kb, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(kb, 2, LV_STATE_DEFAULT);
    lv_obj_add_style(kb, &st_scrollbar, LV_PART_SCROLLBAR);

    lv_obj_set_style_bg_color(kb, lv_color_hex(UIK_C_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, UIK_RADIUS_SM, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(UIK_C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, UIK_FONT_BODY, LV_PART_ITEMS);

    // 按下的那个键提亮，checked（如 shift 锁定）用主色
    lv_obj_set_style_bg_color(kb, lv_color_hex(UIK_C_SURFACE_HI), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(UIK_C_PRIMARY), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
}

// ---------------------------------------------------------------------------
void uik_list(lv_obj_t * list)
{
    kit_theme_init();
    if (list == NULL) return;
    lv_obj_set_style_bg_color(list, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(list, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(list, UIK_RADIUS_SM, LV_STATE_DEFAULT);
    lv_obj_add_style(list, &st_scrollbar, LV_PART_SCROLLBAR);
}

void uik_list_row(lv_obj_t * row)
{
    kit_theme_init();
    if (row == NULL) return;

    // ★ 这里【不能】用 lv_obj_remove_style(row, NULL, 0) 去摘默认主题的肥内边距：
    //   lv_list_add_btn() 是用 local style 给行设置 flex_flow=ROW 和
    //   width=LV_PCT(100) / height=LV_SIZE_CONTENT 的，remove_style 会把
    //   local style 一起 lv_style_reset + free 掉，行就塌成一个默认大小的方块。
    //   正确做法是用 local style 覆盖 —— local 在 styles 数组里排在主题样式前面，
    //   get_prop_core() 从前往后找，第一个命中就返回，所以 local 必然赢。
    // ★ 行高要能被列表可视高度整除，否则最后一行会被容器裁掉半行，
    //   看起来像渲染坏了（实测 pad5 → 行高 27px，文件列表可视 78px → 只出 2.9 行）。
    //   行高 = 字体行高(15) + 2*pad + 2*border：
    //       pad 4 → 25px：文件列表 78px → 3 行整（75px）
    //                      WiFi 列表 106px → 4 行整（100px）
    //   所以这里用 4。行高仍交给 LV_SIZE_CONTENT 算，别写死数字 ——
    //   写死之后换字体（比如以后要用 14pt）就会把文字切掉。
    lv_obj_set_style_pad_all(row, 4, 0);          // 默认主题是 PAD_SMALL≈10，太肥
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UIK_C_BORDER), 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UIK_C_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_text_color(row, lv_color_hex(UIK_C_TEXT), 0);
    lv_obj_set_style_text_font(row, UIK_FONT_BODY, 0);

    // 默认主题在按下时还会把行向两边各撑开 PAD_DEF(≈13px)（list_item_grow），
    // 在 312 宽的列表里等于行比容器还宽，看着像错位。关掉。
    lv_obj_set_style_transform_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(row, 0, LV_STATE_PRESSED);

    // 按下反馈保留默认主题的"压暗"滤镜：列表行本来就是中性底色，
    // 压暗一档正好，而且不会盖掉调用方设的模式色（复制=暗绿 / 删除=暗红）。
    // （按钮那边才需要改成提亮，因为按钮底色是彩色的，压暗后几乎看不出变化。）

    // 行内的图标和文字统一字体/颜色（lv_list_add_btn 建的 img + label）
    uint32_t n = lv_obj_get_child_cnt(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t * c = lv_obj_get_child(row, i);
        if (lv_obj_check_type(c, &lv_label_class)) {
            lv_obj_set_style_text_font(c, UIK_FONT_BODY, LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(c, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
            // 列表里长文件名横向滚动会一直重绘，改成截断：
            // 滚动动画既费 CPU 又是撕裂源，截断反而一眼扫得过去。
            lv_label_set_long_mode(c, LV_LABEL_LONG_DOT);
        } else if (lv_obj_check_type(c, &lv_img_class)) {
            // ★ 图标是"符号"(LV_SYMBOL_*)时，lv_img 内部把它当**文字**画
            //   （见 lv_img.c 的 LV_IMG_SRC_SYMBOL 分支：lv_obj_init_draw_label_dsc），
            //   所以 img_recolor 对符号无效，真正生效的是 text_color。
            //   真正的位图才用 img_recolor。
            lv_obj_set_style_text_color(c, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor(c, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(c, LV_OPA_COVER, LV_STATE_DEFAULT);
        }
    }
}
