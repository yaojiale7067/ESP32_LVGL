// ============================================================================
//  ui_file.h - SD 卡文件管理器
// ============================================================================

#ifndef UI_FILE_H
#define UI_FILE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_file
extern void ui_file_screen_init(void);
extern void ui_file_screen_destroy(void);
extern lv_obj_t* ui_file;
extern void ui_event_Button4(lv_event_t* e);
extern lv_obj_t* ui_Button4;
extern lv_obj_t* ui_Label6;

// ==================== 文件管理器控件（必须导出） ====================
extern lv_obj_t* ui_file_list;      // 文件列表容器（业务层往这里填充）
extern lv_obj_t* ui_path_label;     // 路径显示标签
extern lv_obj_t* ui_status_label;   // 状态栏标签
extern lv_obj_t* ui_refresh_btn;    // 刷新按钮
extern lv_obj_t* ui_title_label;    // 预留

// 新增：工具栏上的三个模式按钮与"新建"按钮
extern lv_obj_t* ui_mode_normal_btn;
extern lv_obj_t* ui_mode_copy_btn;
extern lv_obj_t* ui_mode_delete_btn;
extern lv_obj_t* ui_newfile_btn;

// 新增：底部操作行的发送 / 编辑按钮
extern lv_obj_t* ui_send_btn;
extern lv_obj_t* ui_edit_btn;
// 保留占位（现在固定为 NULL，选中文件名显示在状态行里）
extern lv_obj_t* ui_sel_label;

// ==================== 界面辅助（实现在 ui_file.c） ====================
void ui_file_update_mode_buttons(bool copy_mode, bool delete_mode);
void ui_file_set_path_text(const char* path, const char* copied_name);
// 选中 txt 时启用 Send/Edit（置蓝可点），否则置灰且不可点；
// 同时会把选中文件名写进状态行
void ui_file_set_send_enabled(bool enabled, const char* selected_name);

// ==================== 业务回调 ====================
// 这些由 ui_file.c 的按钮触发，实现在 src/LVGL_Demos.cpp。
// 必须是 C 链接：ui_file.c 按 C 编译，C++ 的链接名对不上。
void file_set_mode_normal(void);
void file_set_mode_copy(void);
void file_set_mode_delete(void);
void create_new_text_file(void);
void file_send_selected(void);      // 发送当前选中的 .txt
void file_edit_selected(void);      // 编辑当前选中的 .txt
void file_paste_here(void);         // 把已复制的文件粘贴到当前目录（由列表首行调用）

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
