// ============================================================================
//  ui_music.h - 音乐播放器屏（读取 SD 卡里的 MP3/WAV）
//  与全工程统一 UI 规范（ui_kit）保持一致的布局：
//      顶栏      y 2~32   [← Back]  ♪ Music Player  [⟳ 刷新]
//      曲目列表  x 10~310 / y 38~196 （面板 + 可滚动列表）
//      控制/状态 y 200~236
//
//  音频输出：本屏只负责"选文件 / 显示进度 / 发播放命令"，
//  真正的解码与输出在 music_player.cpp 里，通过下面 4 个接口对接。
// ============================================================================

#ifndef UI_MUSIC_H
#define UI_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// ---------------- 布局常量（沿用 ui_kit 的尺寸体系） ----------------
#define MUSIC_LIST_Y     38
#define MUSIC_LIST_H     158
#define MUSIC_BAR_Y      200
#define MUSIC_BAR_H      34
#define MUSIC_BTN_W      44

// ---------------- 控件句柄 ----------------
extern lv_obj_t * ui_music;
extern lv_obj_t * ui_music_list;          // 曲目列表容器
extern lv_obj_t * ui_music_back_btn;
extern lv_obj_t * ui_music_refresh_btn;
extern lv_obj_t * ui_music_title_label;   // 当前曲目名
extern lv_obj_t * ui_music_state_label;   // 状态 / 进度
extern lv_obj_t * ui_music_prev_btn;
extern lv_obj_t * ui_music_play_btn;
extern lv_obj_t * ui_music_stop_btn;
extern lv_obj_t * ui_music_next_btn;

// ---------------- 界面接口 ----------------
void ui_music_screen_init(void);
void ui_music_screen_destroy(void);

// 刷新曲目列表（重新扫描 current_path 下的音频文件）
void ui_music_reload_list(void);

// 由 music_player.cpp 回调，更新界面上的标题/状态文字
// state: 0=停止 1=播放中 2=暂停 3=错误
void ui_music_set_now_playing(const char * filename, int state);
void ui_music_set_progress(unsigned long pos_ms, unsigned long total_ms);

// 业务回调（实现在 src/music_player.cpp）
// 在 SD 卡里扫描到的音频文件全路径上操作
void music_on_file_selected(const char * full_path);   // 列表里点了某首
void music_on_play_pause(void);
void music_on_stop(void);
void music_on_prev(void);
void music_on_next(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
