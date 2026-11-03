// ============================================================================
//  ui_file.c - SD 卡文件管理器
//
//  布局（320x240，与其它屏同一套 ui_kit 规范）：
//      0..37     顶栏：[← Back]   File Manager   [⟳]
//      40..66    工具栏：[Normal][Copy][Delete]        [＋ New]   四等分
//      70..85    路径行：Path: /xxx
//      88..176   文件列表（业务层填充）
//      180..208  操作行：[⬆ Send]              [✎ Edit]       各 154 宽
//      212..239  状态提示（含上沿分隔线）
//
//  为什么"发送"要单独一个按钮：
//    以前点 .txt 文件 = 直接发串口，于是列表里已有的 txt **只能发不能编辑**，
//    只有通过 New 新建的文件才能进编辑器。现在改成：
//        点 .txt  → 只是"选中"（会显示在底部）
//        点 Send  → 发送选中的文件
//        New      → 新建并进编辑器
//    这样纯 txt 文件也有统一的编辑入口（复用 New 那套编辑器）。
//
//  模式的三态：
//    模式原本是**一个按钮循环切换**（普通→复制→删除→普通），点错要绕两圈。
//    现在是三个互斥按钮，当前模式高亮：Normal 灰 / Copy 绿 / Delete 红。
//
//  ★ 业务层 (LVGL_Demos.cpp) 会直接对 ui_file_list 做 lv_obj_clean()
//    并往里 create scroll_panel / lv_list，所以 ui_file_list 必须保持
//    "普通容器、无 flex 布局"这个语义。
// ============================================================================

#include <stdio.h>      // snprintf（拼 "Path: ..." / "Selected: ..." 用）

#include "../ui.h"
#include "../ui_kit.h"

// ==================== UI控件全局变量 ====================
lv_obj_t * ui_file = NULL;
lv_obj_t * ui_Button4 = NULL;      // 顶栏返回
lv_obj_t * ui_Label6 = NULL;

// 文件管理器专用控件（供外部访问）
lv_obj_t * ui_file_list = NULL;      // 文件列表容器
lv_obj_t * ui_path_label = NULL;     // 路径显示标签
lv_obj_t * ui_status_label = NULL;   // 状态栏标签（由 uik_status_bar 创建）
lv_obj_t * ui_refresh_btn = NULL;    // 刷新按钮（顶栏右侧）
lv_obj_t * ui_title_label = NULL;    // 预留（标题现在由顶栏持有）

lv_obj_t * ui_mode_normal_btn = NULL;   // 三个互斥的模式按钮
lv_obj_t * ui_mode_copy_btn = NULL;
lv_obj_t * ui_mode_delete_btn = NULL;
lv_obj_t * ui_newfile_btn = NULL;
lv_obj_t * ui_send_btn = NULL;          // 发送选中的 .txt
lv_obj_t * ui_edit_btn = NULL;          // 编辑选中的 .txt
lv_obj_t * ui_sel_label = NULL;         // 保留占位：固定为 NULL（选中项显示在状态行）

// ==================== 几何常量 ====================
//  全部由 ui_kit 推导，纵横缝隙都是 4。
//  横向：4 个等宽按钮 (UIK_CONTENT_W - 3*GAP) / 4 = (312-12)/4 = 75
//        4 + 75 + 4 + 75 + 4 + 75 + 4 + 75 + 4 = 320 ✓（两端各留 4）
//  纵向：每个 y + h 都 ≤ 208（状态条从 212 起，另有分隔线）
#define GAP          UIK_PANEL_GAP
#define MARGIN       UIK_LEFT                    // 4
#define CW           UIK_CONTENT_W               // 312：通栏元素宽度

#define F_BAR_Y    (UIK_CONTENT_Y + 2)           // 40 工具栏 → 40..66
#define F_BAR_H    26
#define F_MODE_W   ((CW - 3 * GAP) / 4)          // 75
#define F_PATH_Y   70                            // 路径行  → 70..85
#define F_LIST_Y   88                            // 列表    → 88..176
#define F_LIST_H   88
#define F_ACT_H    28
#define F_ACT_Y    (UIK_STATUS_Y - GAP - F_ACT_H)  // 180 → 180..208
#define F_BTN_W    ((CW - GAP) / 2)              // 154：Send / Edit 平分

// ==================== 事件函数 ====================
void ui_event_Button4(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Screen1_screen_init);
    }
}

static void back_cb(lv_event_t * e) { ui_event_Button4(e); }

static void mode_normal_cb(lv_event_t * e) { (void)e; file_set_mode_normal(); }
static void mode_copy_cb(lv_event_t * e)   { (void)e; file_set_mode_copy();   }
static void mode_delete_cb(lv_event_t * e) { (void)e; file_set_mode_delete(); }
static void newfile_cb(lv_event_t * e)     { (void)e; create_new_text_file(); }
static void send_cb(lv_event_t * e)        { (void)e; file_send_selected();   }
static void edit_cb(lv_event_t * e)        { (void)e; file_edit_selected();   }

// ==================== 小工具 ====================
// 模式按钮：三态（未选中/选中）用背景色区分，文字在未选中时压暗。
// 不用 uik_btn()，因为它会把标签设成白字；这里需要标签可控。
static lv_obj_t * make_mode_btn(lv_obj_t * parent, int x, const char * text)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, F_MODE_W, F_BAR_H);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, F_BAR_Y);
    lv_obj_set_style_radius(b, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(b, 0, LV_STATE_DEFAULT);
    uik_btn_states(b, UIK_C_SURFACE);          // 未选中 = 和背景一档的暗色

    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_center(l);
    return b;
}

// 底部操作按钮（Send / Edit）：标签需要跟着禁用态变灰，所以也自己建。
static lv_obj_t * make_action_btn(lv_obj_t * parent, int x, const char * text,
                                  lv_event_cb_t cb)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, F_BTN_W, F_ACT_H);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, F_ACT_Y);
    lv_obj_set_style_radius(b, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(b, 0, LV_STATE_DEFAULT);
    uik_btn_states(b, UIK_C_NEUTRAL);

    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

// 按钮的子标签（Send/Edit 都是 child 0）
static lv_obj_t * btn_label(lv_obj_t * b)
{
    return b ? lv_obj_get_child(b, 0) : NULL;
}

// ==================== UI构建函数 ====================
void ui_file_screen_init(void)
{
    ui_file = uik_screen_init();

    // ---- 顶栏：返回 + 标题 + 刷新 ----
    // 刷新按钮保持主色（它是这一屏最常用的动作），但补上按下反馈。
    uik_topbar(ui_file, "File Manager", LV_SYMBOL_REFRESH, back_cb, NULL,
               &ui_Button4, &ui_refresh_btn);
    if (ui_refresh_btn != NULL) uik_btn_states(ui_refresh_btn, UIK_C_PRIMARY);
    if (ui_Button4 != NULL) ui_Label6 = lv_obj_get_child(ui_Button4, 0);
    ui_title_label = NULL;

    // ---- 工具栏：3 个互斥模式按钮 + 新建，四等分整宽 ----
    int x = MARGIN;
    ui_mode_normal_btn = make_mode_btn(ui_file, x, "Normal");
    lv_obj_add_event_cb(ui_mode_normal_btn, mode_normal_cb, LV_EVENT_CLICKED, NULL);

    x += F_MODE_W + GAP;
    ui_mode_copy_btn = make_mode_btn(ui_file, x, "Copy");
    lv_obj_add_event_cb(ui_mode_copy_btn, mode_copy_cb, LV_EVENT_CLICKED, NULL);

    x += F_MODE_W + GAP;
    ui_mode_delete_btn = make_mode_btn(ui_file, x, "Delete");
    lv_obj_add_event_cb(ui_mode_delete_btn, mode_delete_cb, LV_EVENT_CLICKED, NULL);

    // 第四个位置："新建"：绿色，和模式按钮区分开
    x += F_MODE_W + GAP;
    ui_newfile_btn = uik_btn(ui_file, LV_SYMBOL_PLUS " New",
                             x, F_BAR_Y, F_MODE_W, F_BAR_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(ui_newfile_btn, newfile_cb, LV_EVENT_CLICKED, NULL);

    // ---- 路径行 ----
    ui_path_label = uik_label(ui_file, "Path: /", MARGIN, F_PATH_Y,
                              UIK_C_TEXT_DIM, UIK_FONT_BODY, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(ui_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui_path_label, CW);

    // ---- 文件列表容器 ----
    // 业务层会 lv_obj_clean() 它并塞入自己的 scroll_panel（用 LV_PCT(100) 自适应）。
    // ★ 必须保持普通容器、不加 flex 布局。
    ui_file_list = lv_obj_create(ui_file);
    lv_obj_set_size(ui_file_list, CW, F_LIST_H);
    lv_obj_align(ui_file_list, LV_ALIGN_TOP_LEFT, MARGIN, F_LIST_Y);
    lv_obj_set_style_bg_color(ui_file_list, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_file_list, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_file_list, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_file_list, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_file_list, 2, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(ui_file_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui_file_list, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 状态行 ----
    // 标准位置（y=212）由 uik_status_bar 决定，不再单独挪位置 ——
    // 以前把它 align 到 190，结果和分隔线的位置对不上。
    ui_sel_label = NULL;
    ui_status_label = uik_status_bar(ui_file);
    lv_label_set_text(ui_status_label, "Tap a .txt to select it");
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);

    // ---- 底部操作行：Send / Edit，两个按钮平分整宽 ----
    int bx = MARGIN;
    ui_send_btn = make_action_btn(ui_file, bx, LV_SYMBOL_UPLOAD " Send", send_cb);
    bx += F_BTN_W + GAP;
    ui_edit_btn = make_action_btn(ui_file, bx, LV_SYMBOL_EDIT " Edit", edit_cb);

    // 初始状态：没有选中任何文件 → 两个按钮都禁用
    ui_file_set_send_enabled(false, NULL);
}

// ============================================================================
//  供业务层调用
// ============================================================================
// 刷新三个模式按钮的高亮。业务层每次重建列表时调用。
//   Normal=灰(默认) / Copy=绿 / Delete=红
// 另外：未选中的按钮文字压暗，避免四个同亮度文字抢注意力。
void ui_file_update_mode_buttons(bool copy_mode, bool delete_mode)
{
    lv_obj_t * b[3]  = { ui_mode_normal_btn, ui_mode_copy_btn, ui_mode_delete_btn };
    uint32_t  on[3]  = { UIK_C_NEUTRAL,      UIK_C_SUCCESS,     UIK_C_DANGER };
    bool      sel[3] = { !copy_mode && !delete_mode, copy_mode, delete_mode };

    for (int i = 0; i < 3; i++) {
        if (b[i] == NULL) continue;
        // ★ 走 uik_btn_states 而不是只写 DEFAULT 的 bg_color：
        //   它还负责算 PRESSED 的提亮色。只改 DEFAULT 的话，按下时用的还是
        //   上一次 base 算出来的提亮色，会出现"按下反而变回灰色"的怪现象。
        uik_btn_states(b[i], sel[i] ? on[i] : UIK_C_SURFACE);
        lv_obj_t * l = btn_label(b[i]);
        if (l != NULL) {
            lv_obj_set_style_text_color(l,
                lv_color_hex(sel[i] ? UIK_C_TEXT : UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
        }
    }
}

// 路径行文本：Path: /xxx，可附带"已复制"提示
void ui_file_set_path_text(const char * path, const char * copied_name)
{
    if (ui_path_label == NULL) return;
    char buf[192];
    if (copied_name != NULL && copied_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "Path: %s   [copied: %s]",
                 path ? path : "/", copied_name);
    } else {
        snprintf(buf, sizeof(buf), "Path: %s", path ? path : "/");
    }
    lv_label_set_text(ui_path_label, buf);
}

// Send / Edit 按钮：选中 .txt 才可用。disabled 时置灰。
// 两个按钮状态一致 —— 有选中的 txt 就都能点。
// 选中的文件名显示在【状态行】上（没有单独的选择行，否则底部按钮会被挤出屏幕）。
void ui_file_set_send_enabled(bool enabled, const char * selected_name)
{
    lv_obj_t * btns[2] = { ui_send_btn, ui_edit_btn };
    for (int i = 0; i < 2; i++) {
        if (btns[i] == NULL) continue;
        // 底色：可用=主色蓝（告诉用户"现在可以按"），不可用交给 DISABLED 样式。
        // 同样必须走 uik_btn_states，按下态的提亮色才会跟着 base 一起更新。
        uik_btn_states(btns[i], UIK_C_PRIMARY);
        if (enabled) lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
        else         lv_obj_add_state(btns[i], LV_STATE_DISABLED);
        // 文字也要跟着变 —— LVGL 的状态不会往子对象级联，光改按钮底色
        // 会出现"灰底白字"，看起来还像能按。
        lv_obj_t * l = btn_label(btns[i]);
        if (l != NULL) {
            lv_obj_set_style_text_color(l,
                lv_color_hex(enabled ? UIK_C_TEXT : UIK_C_TEXT_FAINT), LV_STATE_DEFAULT);
        }
    }
    if (ui_status_label != NULL) {
        char buf[160];
        if (enabled && selected_name != NULL && selected_name[0] != '\0') {
            snprintf(buf, sizeof(buf), "Selected: %s", selected_name);
        } else {
            snprintf(buf, sizeof(buf), "Tap a .txt to select it");
        }
        lv_label_set_text(ui_status_label, buf);
        lv_obj_set_style_text_color(ui_status_label,
            lv_color_hex(enabled ? UIK_C_WARN_TEXT : UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    }
}

// 粘贴入口不在这里 —— 它做成文件列表的首行（只有复制过东西时才出现），
// 因为底部那一行放两个按钮刚好，放三个就得把字号压到 10pt。

void ui_file_screen_destroy(void)
{
    if (ui_file) lv_obj_del(ui_file);

    ui_file = NULL;
    ui_Button4 = NULL;
    ui_Label6 = NULL;
    ui_file_list = NULL;
    ui_path_label = NULL;
    ui_status_label = NULL;
    ui_refresh_btn = NULL;
    ui_title_label = NULL;
    ui_mode_normal_btn = NULL;
    ui_mode_copy_btn = NULL;
    ui_mode_delete_btn = NULL;
    ui_newfile_btn = NULL;
    ui_send_btn = NULL;
    ui_edit_btn = NULL;
    ui_sel_label = NULL;
}