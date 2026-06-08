#ifndef UI_FILE_H
#define UI_FILE_H

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
extern lv_obj_t* ui_file_list;      // 文件列表容器
extern lv_obj_t* ui_path_label;     // 路径显示标签
extern lv_obj_t* ui_status_label;   // 状态栏标签
extern lv_obj_t* ui_refresh_btn;    // 刷新按钮
extern lv_obj_t* ui_title_label;    // 标题标签

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif