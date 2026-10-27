// ============================================================================
//  ui_file.c - SD 卡文件管理器
//  统一后的布局（320x240，与蓝牙/WiFi 屏同一套规范）：
//      顶栏     y  2~ 32   [← Back]  File Manager  [⟳ 刷新]
//      工具栏   y 36~ 66   [Normal][Copy][Delete]        [＋ New]
//      路径行   y 70~ 84   Path: /xxx   （复制模式下附带 [copied: 文件名]）
//      列表     y 86~196   可滚动的文件列表（业务层填充）
//      状态行   y 200~214  操作结果 / 提示
//      操作行   y 218~240  [⬆ Send]   选中的文件     [Paste]
//
//  为什么"发送"要单独一个按钮：
//    以前点 .txt 文件 = 直接发串口，于是列表里已有的 txt **只能发不能编辑**，
//    只有通过 New 新建的文件才能进编辑器。现在改成：
//        点 .txt  → 只是"选中"（会显示在底部中间）
//        点 Send  → 发送选中的文件
//        New      → 新建并进编辑器
//    这样纯 txt 文件也有统一的编辑入口（New 那套编辑器可复用）。
//
//  相比旧版的两点改动（功能一个没少，只是更清楚）：
//    1) 模式原本是**一个按钮循环切换**（普通→复制→删除→普通），点错要绕两圈。
//       现在拆成三个互斥按钮，当前模式高亮：Normal 灰 / Copy 绿 / Delete 红。
//    2) "新建"和"模式"原本占着列表的前两行、还混在滚动区里，
//       现在移到固定工具栏，列表空间全给文件，操作也不随滚动跑掉。
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
lv_obj_t * ui_status_label = NULL;   // 状态栏标签
lv_obj_t * ui_refresh_btn = NULL;    // 刷新按钮（顶栏右侧）
lv_obj_t * ui_title_label = NULL;    // 预留（标题现在由顶栏持有）

lv_obj_t * ui_mode_normal_btn = NULL;   // 三个互斥的模式按钮
lv_obj_t * ui_mode_copy_btn = NULL;
lv_obj_t * ui_mode_delete_btn = NULL;
lv_obj_t * ui_newfile_btn = NULL;
lv_obj_t * ui_send_btn = NULL;          // 发送选中的 .txt（新增）
lv_obj_t * ui_edit_btn = NULL;          // 编辑选中的 .txt（新增）
lv_obj_t * ui_sel_label = NULL;         // 保留占位：现在固定为 NULL（见下）

// ==================== 几何常量 ====================
//  ★ 全部由 ui_kit 的统一缝隙(UIK_PANEL_GAP)推导，纵横间距一致。
//    必须满足：每一项的 y + h ≤ 240（屏高）。
//    曾经出过一次事故：状态行 + 选择行 + 操作行 纵向加起来 256，底部按钮被
//    推到屏幕外面。改这里的数字后请用脚本核对一遍边界。
//
//  ⚠️ 横向的整除问题：320 = 2*MARGIN + 4*F_MODE_W + 3*GAP
//       2*4 + 4*73 + 3*4 = 308 = UIK_CONTENT_W，正好；
//     但 4 + 73 + 4 + 73 + 4 + 73 + 4 + 73 = 308，右端到 308，
//     所以右侧实际留 12px、左侧 4px —— 320 无法让 4 个等宽按钮两端都留 4。
//     ★ 左边界/右边界/通栏宽度全部取自 ui_kit 的 UIK_LEFT / UIK_RIGHT_EDGE /
//       UIK_CONTENT_W，这样各屏的右留白是一致的，不会各算各的。
#define GAP          UIK_PANEL_GAP
#define MARGIN       UIK_LEFT                    // 4
#define RIGHT_EDGE   UIK_RIGHT_EDGE              // 308
#define CW           UIK_CONTENT_W               // 308：通栏元素宽度

#define F_BAR_Y    (UIK_CONTENT_Y - 2)           // 36 模式工具栏
#define F_BAR_H    30                            //    → 36..66
#define F_MODE_W   ((CW - 3 * GAP) / 4)          // 73：4 等分（3 模式 + 新建）
#define F_PATH_Y   70                            // 路径行   → 70..84
#define F_LIST_Y   88                            // 文件列表 → 88..186
#define F_LIST_H   98
#define F_STATUS_Y 190                           // 状态行   → 190..204
#define F_ACT_H    28
#define F_ACT_Y    (UIK_H - GAP - F_ACT_H)       // 208：下沿 236，上面留 4 缝
#define F_BTN_W    ((CW - GAP) / 2)              // 150：Send / Edit 平分

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

// 刷新按钮的真实回调由 LVGL_Demos.cpp 追加绑定（refresh_file_list_cb）

// ==================== 小工具 ====================
static void style_mode_btn(lv_obj_t * b, const char * text)
{
    lv_obj_set_size(b, F_MODE_W, F_BAR_H);
    lv_obj_set_style_radius(b, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(b, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UIK_FONT_BODY, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    lv_obj_center(l);
}

// ==================== UI构建函数 ====================
void ui_file_screen_init(void)
{
    ui_file = uik_screen_init();

    // ---- 顶栏：返回 + 标题 + 刷新 ----
    uik_topbar(ui_file, "File Manager", LV_SYMBOL_REFRESH, back_cb, NULL,
               &ui_Button4, &ui_refresh_btn);
    if (ui_refresh_btn != NULL) {
        lv_obj_set_style_bg_color(ui_refresh_btn, lv_color_hex(UIK_C_PRIMARY), LV_STATE_DEFAULT);
    }
    if (ui_Button4 != NULL) ui_Label6 = lv_obj_get_child(ui_Button4, 0);
    ui_title_label = NULL;

    // ---- 工具栏：3 个互斥模式按钮 + 新建，四等分整宽 ----
    //   MARGIN + M + GAP + M + GAP + M + GAP + M + MARGIN = 320
    //   4 + 75 + 4 + 75 + 4 + 75 + 4 + 75 + 4 = 320
    int x = MARGIN;
    ui_mode_normal_btn = lv_btn_create(ui_file);
    lv_obj_align(ui_mode_normal_btn, LV_ALIGN_TOP_LEFT, x, F_BAR_Y);
    style_mode_btn(ui_mode_normal_btn, "Normal");
    lv_obj_add_event_cb(ui_mode_normal_btn, mode_normal_cb, LV_EVENT_CLICKED, NULL);

    x += F_MODE_W + GAP;
    ui_mode_copy_btn = lv_btn_create(ui_file);
    lv_obj_align(ui_mode_copy_btn, LV_ALIGN_TOP_LEFT, x, F_BAR_Y);
    style_mode_btn(ui_mode_copy_btn, "Copy");
    lv_obj_add_event_cb(ui_mode_copy_btn, mode_copy_cb, LV_EVENT_CLICKED, NULL);

    x += F_MODE_W + GAP;
    ui_mode_delete_btn = lv_btn_create(ui_file);
    lv_obj_align(ui_mode_delete_btn, LV_ALIGN_TOP_LEFT, x, F_BAR_Y);
    style_mode_btn(ui_mode_delete_btn, "Delete");
    lv_obj_add_event_cb(ui_mode_delete_btn, mode_delete_cb, LV_EVENT_CLICKED, NULL);

    // 第四个位置："新建"：绿色，和模式按钮区分开
    x += F_MODE_W + GAP;
    ui_newfile_btn = uik_btn(ui_file, LV_SYMBOL_PLUS " New",
                             x, F_BAR_Y, F_MODE_W, F_BAR_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(ui_newfile_btn, newfile_cb, LV_EVENT_CLICKED, NULL);

    // ---- 路径行 ----
    ui_path_label = uik_label(ui_file, "Path: /", MARGIN, F_PATH_Y,
                              UIK_C_TEXT_DIM, UIK_FONT_BODY, LV_TEXT_ALIGN_LEFT);

    // ---- 文件列表容器 ----
    // 业务层会 lv_obj_clean() 它并塞入自己的 scroll_panel（用 LV_PCT(100) 自适应），
    // 这里只负责给足空间、统一边框样式。
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
    // "当前选中哪个文件"就显示在这里，不再单独占一行 —— 之前多排了一行，
    // 把底部按钮挤出了屏幕（240 高度不够）。
    ui_sel_label = NULL;
    ui_status_label = uik_status_bar(ui_file);
    lv_obj_align(ui_status_label, LV_ALIGN_TOP_LEFT, MARGIN, F_STATUS_Y);
    lv_label_set_text(ui_status_label, "Tap a .txt to select it");
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(UIK_C_OK_TEXT), LV_STATE_DEFAULT);

    // ---- 底部操作行：Send / Edit，两个按钮平分整宽 ----
    //   MARGIN + B + GAP + B + MARGIN = 4 + 154 + 4 + 154 + 4 = 320
    // Paste 不在这里 —— 它只在"复制过东西"时才有意义，做成列表首行。
    int bx = MARGIN;
    ui_send_btn = lv_btn_create(ui_file);
    lv_obj_set_size(ui_send_btn, F_BTN_W, F_ACT_H);
    lv_obj_align(ui_send_btn, LV_ALIGN_TOP_LEFT, bx, F_ACT_Y);
    lv_obj_set_style_radius(ui_send_btn, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_send_btn, lv_color_hex(UIK_C_NEUTRAL), LV_STATE_DEFAULT);
    {
        lv_obj_t * l = lv_label_create(ui_send_btn);
        lv_label_set_text(l, LV_SYMBOL_UPLOAD " Send");
        lv_obj_set_style_text_font(l, UIK_FONT_BODY, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(ui_send_btn, send_cb, LV_EVENT_CLICKED, NULL);

    bx += F_BTN_W + GAP;
    ui_edit_btn = lv_btn_create(ui_file);
    lv_obj_set_size(ui_edit_btn, F_BTN_W, F_ACT_H);
    lv_obj_align(ui_edit_btn, LV_ALIGN_TOP_LEFT, bx, F_ACT_Y);
    lv_obj_set_style_radius(ui_edit_btn, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_edit_btn, lv_color_hex(UIK_C_NEUTRAL), LV_STATE_DEFAULT);
    {
        lv_obj_t * l = lv_label_create(ui_edit_btn);
        lv_label_set_text(l, LV_SYMBOL_EDIT " Edit");
        lv_obj_set_style_text_font(l, UIK_FONT_BODY, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(l, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(ui_edit_btn, edit_cb, LV_EVENT_CLICKED, NULL);
}

// ============================================================================
//  供业务层调用
// ============================================================================
// 刷新三个模式按钮的高亮。业务层每次重建列表时调用。
//   Normal=灰(默认) / Copy=绿 / Delete=红
void ui_file_update_mode_buttons(bool copy_mode, bool delete_mode)
{
    lv_obj_t * b[3]  = { ui_mode_normal_btn, ui_mode_copy_btn, ui_mode_delete_btn };
    uint32_t  on[3]  = { UIK_C_NEUTRAL,      UIK_C_SUCCESS,     UIK_C_DANGER };
    bool      sel[3] = { !copy_mode && !delete_mode, copy_mode, delete_mode };

    for (int i = 0; i < 3; i++) {
        if (b[i] == NULL) continue;
        lv_obj_set_style_bg_color(b[i],
            lv_color_hex(sel[i] ? on[i] : UIK_C_SURFACE), LV_STATE_DEFAULT);
        lv_obj_t * l = lv_obj_get_child(b[i], 0);
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
        lv_obj_set_style_bg_color(btns[i],
            lv_color_hex(enabled ? UIK_C_PRIMARY : UIK_C_NEUTRAL), LV_STATE_DEFAULT);
        if (enabled) lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
        else         lv_obj_add_state(btns[i], LV_STATE_DISABLED);
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
