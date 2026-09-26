// ============================================================================
//  ui_file.c - SD 卡文件管理器屏
//  统一后的样式：顶栏(返回 + 刷新) + 路径行 + 列表容器 + 底部状态条
//  布局（320x240）：
//      顶栏            y 2~32，右侧按钮 = 刷新
//      路径标签        y 36（“Path: /xxx”）
//      列表容器        x 10~310 / y 52~202   ← 业务层往这里塞 scroll_panel + list
//      状态条          y 212
//
//  ★ 业务层 (LVGL_Demos.cpp) 会直接对 ui_file_list 做 lv_obj_clean()
//    并往里 create scroll_panel / lv_list，所以 ui_file_list 必须是一个
//    普通容器、且不带 flex 布局 —— 这里保持与原来完全一致的语义。
// ============================================================================

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
lv_obj_t * ui_title_label = NULL;    // 标题标签

// ==================== 事件函数 ====================
void ui_event_Button4(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Screen1_screen_init);
    }
}

static void back_cb(lv_event_t * e) { ui_event_Button4(e); }

// 刷新按钮的真实回调由 LVGL_Demos.cpp 用 lv_obj_add_event_cb 追加绑定
// （refresh_file_list_cb），这里不占用。

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
    ui_title_label = NULL;      // 标题现在在顶栏里，由 uik_topbar 内部持有

    // ---- 路径行 ----
    ui_path_label = uik_label(ui_file, "Path: /", UIK_PAD, 36,
                              UIK_C_TEXT_DIM, UIK_FONT_BODY, LV_TEXT_ALIGN_LEFT);

    // ---- 列表容器 ----
    // 业务层会 lv_obj_clean() 它并往里放 scroll_panel(280x140)，
    // 所以尺寸保持 290x150、位置 (10,52) —— 与业务层的假设一致。
    ui_file_list = lv_obj_create(ui_file);
    lv_obj_set_size(ui_file_list, 290, 150);
    lv_obj_align(ui_file_list, LV_ALIGN_TOP_LEFT, UIK_PAD, 52);
    lv_obj_set_style_bg_color(ui_file_list, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_file_list, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_file_list, lv_color_hex(UIK_C_BORDER), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_file_list, UIK_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_file_list, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(ui_file_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui_file_list, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 底部状态条 ----
    ui_status_label = uik_status_bar(ui_file);
    lv_label_set_text(ui_status_label, "Ready");
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(UIK_C_OK_TEXT), LV_STATE_DEFAULT);
}

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
}
