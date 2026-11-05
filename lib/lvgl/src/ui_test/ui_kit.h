//  ui_kit.h - 全工程统一的 UI 视觉规范 

#ifndef UI_KIT_H
#define UI_KIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// ---------------- 尺寸常量 ----------------
#define UIK_W            320
#define UIK_H            240
// ★ 缝隙只有一个值：屏幕左右留白、面板横缝、面板竖缝全都用它。
//   曾经横向用 10、纵向用 4，结果同一屏上"两个卡片之间"看起来比
//   "上下两行之间"宽一倍多，很显眼。统一成 4（= ui_panel 内部 pad_all）。
#define UIK_PANEL_GAP    4
#define UIK_PAD          UIK_PANEL_GAP   // 左右留白 = 面板缝隙，同一个节奏
// 所有屏幕元素的左右边界都取这两个值，保证左右留白都是 4、全屏一致。
// 320 是个不好分的数：用 4 做留白、通栏宽 312 之后，
//   4 个等宽按钮正好 (312 - 3*4) / 4 = 75，
//   两张卡片正好 (312 - 4) / 2 = 154，
// 都能整除，所以"左右各留 4"是可以严格做到的，不必让右边多留一点。
#define UIK_LEFT         UIK_PAD                        // 4：左边界
#define UIK_RIGHT_EDGE   (UIK_W - UIK_PAD)              // 316：右边界
#define UIK_CONTENT_W    (UIK_RIGHT_EDGE - UIK_LEFT)    // 312：通栏元素宽度

#define UIK_TOPBAR_H     30      // 顶栏按钮高度
#define UIK_TOPBAR_Y     UIK_PANEL_GAP
#define UIK_TOPBAR_BOT   (UIK_TOPBAR_Y + UIK_TOPBAR_H)    // 34：顶栏按钮下沿
#define UIK_CONTENT_Y    (UIK_TOPBAR_BOT + UIK_PANEL_GAP) // 38：顶栏总高 / 内容起点
#define UIK_STATUS_H     28      // 底部状态条高度
#define UIK_STATUS_Y     (UIK_H - UIK_STATUS_H)      // 212
#define UIK_KB_H         120     // 键盘高度
#define UIK_KB_TOP       (UIK_H - UIK_KB_H)   // 120：键盘上沿（弹出时占 120..239）
#define UIK_RADIUS       5       // 面板 / 按钮 / 输入框
#define UIK_RADIUS_SM    3       // 徽章、小色条、列表容器


#define UIK_ROW_H        35

#define UIK_BADGE        27

// ---------------- 明度台阶 ----------------
#define UIK_C_BG         0x1A1A1A   // 屏幕底色
#define UIK_C_SURFACE    0x262626   // 面板 / 卡片 / 输入框
#define UIK_C_SURFACE_HI 0x333333   // 抬起一层：按下态、列表选中底
#define UIK_C_SUNKEN     0x0F0F0F   // 凹陷：终端 / 接收区底色
#define UIK_C_BORDER     0x343434   // 分隔线 / 面板描边
#define UIK_C_DARK       UIK_C_SUNKEN   // 旧名，保留兼容

// ---------------- 语义色 ----------------
#define UIK_C_PRIMARY    0x2D6CB5   // 主操作（蓝）
#define UIK_C_PRIMARY_HI 0x3D86D8   // 主操作按下
#define UIK_C_SUCCESS    0x2E8B57   // 确认 / 发送 / 新建（绿）
#define UIK_C_SUCCESS_HI 0x3CA86E   // 绿按下
#define UIK_C_NEUTRAL    0x3A3A3A   // 次要操作（灰）
#define UIK_C_NEUTRAL_HI 0x4D4D4D   // 灰按下
// 红从 0xFF5555 压到 0xC94A4A：在深色底上纯亮红会"跳"出画面，
// 而且它只是模式指示，不是警告灯。真正要警示时文字用 UIK_C_ERR_TEXT。
#define UIK_C_DANGER     0xC94A4A   // 破坏性操作（红）
#define UIK_C_DANGER_HI  0xE06060


#define UIK_C_TEXT       0xF0F0F0   // 主文本（不用纯白，见文件头说明）
#define UIK_C_TEXT_DIM   0x9C9C9C   // 次要：卡片小标题、单位、提示
#define UIK_C_TEXT_FAINT 0x8A8A8A   // 更弱：版本号、辅助说明（下限，别再调暗）
#define UIK_C_OK_TEXT    0x79D08A   // 成功 / 终端回显（原 0x88FF88 太荧光）
#define UIK_C_WARN_TEXT  0xE8C468
#define UIK_C_ERR_TEXT   0xE88383

// ---------------- 功能入口的身份色 ----------------
// 只用于菜单行左侧的图标徽章、以及卡片顶部的小色条。
// 相邻两行的色相至少差 60°，所以从上到下扫一眼就能区分。
#define UIK_ACCENT_TEMP    0xE8924A   // 温湿度：琥珀
#define UIK_ACCENT_FILE    0x43C6B0   // 文件：青绿
#define UIK_ACCENT_SERIAL  0x8CC461   // 串口：黄绿
#define UIK_ACCENT_WIFI    0x3FA9E0   // WiFi：蓝
#define UIK_ACCENT_BLE     0x9B7CF5   // 蓝牙：靛紫
#define UIK_ACCENT_HUM     0x4FC3D9   // 湿度：浅青
#define UIK_ACCENT_CHIP    0xE07A7A   // 片内温度：暖红

// ---------------- 字号（全工程只用这几档，不要再手写 lv_font_montserrat_XX）-
//
//   TITLE  14pt  顶栏标题 / 按钮文字 / 菜单行
//   BODY   12pt  正文、输入框、状态条、列表
//   HINT   10pt  次要说明、单位、卡片小标题
//   ICON   16pt  徽章里的符号（比 TITLE 大一档，符号才撑得满 25px 徽章）
//   NUM    40pt  仅用于温湿度这类"要大号显示"的数值
//
//  NUM 从 28pt 提到 40pt：卡片高 112，40pt 行高 44，加上色条/标题/单位
//  总共约 90，仍有富余；而温湿度是这一屏唯一要读的信息，理应最大。
#define UIK_FONT_TITLE   (&lv_font_montserrat_14)
#define UIK_FONT_BODY    (&lv_font_montserrat_12)
#define UIK_FONT_HINT    (&lv_font_montserrat_10)
#define UIK_FONT_ICON    (&lv_font_montserrat_16)
#define UIK_FONT_NUM     (&lv_font_montserrat_40)
#define UIK_FONT_BIG     (&lv_font_montserrat_20)   // 次级大数值（片内温度等）

// ---------------- 屏幕外壳 ----------------
lv_obj_t * uik_screen_init(void);                       // 建屏 + 统一底色
lv_obj_t * uik_status_bar(lv_obj_t * parent);           // 底部状态提示条（含分隔线）
lv_obj_t * uik_separator(lv_obj_t * parent, int x, int y, int w);  // 1px 分隔线

// 切屏请一律走这个，别直接 lv_scr_load()：
// 它在加载后补一次整屏失效，防止"进屏首帧残留一小块、切走再回来才好"。
// 详见 ui_kit.c 里的注释。
void uik_screen_load(lv_obj_t * scr);

// ---------------- 顶栏（三件套） ----------------
// title 可以为 NULL；right_btn_text 为 NULL 时不建右侧按钮。
// back_out / right_out 可以为 NULL；传入非空指针时会回传对应按钮句柄
// （避免用 lv_obj_get_child 猜索引那种脆弱写法）。
// 返回值：顶栏容器本身。
lv_obj_t * uik_topbar(lv_obj_t * parent, const char * title,
                      const char * right_btn_text,
                      lv_event_cb_t back_cb, lv_event_cb_t right_cb,
                      lv_obj_t ** back_out, lv_obj_t ** right_out);

void uik_topbar_set_title(lv_obj_t * topbar, const char * title);

// ---------------- 常用控件 ----------------
lv_obj_t * uik_panel(lv_obj_t * parent, int x, int y, int w, int h);   // 面板/卡片

lv_obj_t * uik_btn(lv_obj_t * parent, const char * text,
                   int x, int y, int w, int h, uint32_t bg);           // 普通按钮

lv_obj_t * uik_ta(lv_obj_t * parent, int x, int y, int w, int h,
                  const char * placeholder, bool one_line);             // 文本框

// 只读文本区（终端回显 / 接收缓冲）：凹陷底色 + 等宽感的绿字，
// 不给焦点环（它不是输入框，加主色描边会误导），但仍可点中拖动滚动。
lv_obj_t * uik_ta_readonly(lv_obj_t * parent, int x, int y, int w, int h,
                           const char * placeholder);

lv_obj_t * uik_label(lv_obj_t * parent, const char * text,
                     int x, int y, uint32_t color,
                     const lv_font_t * font, lv_text_align_t align);   // 文本

// ---------------- 状态反馈 ----------------
// 给按钮挂上"按下变亮一档 / 禁用变灰"的本地样式。
// 以前只靠默认主题的 color_filter 把按钮压暗 35，在深色底上几乎看不出变化，
// 手指按下去没有反馈。这里改成【按下变亮】，并且把主题那个压暗滤镜关掉。
// uik_btn() / uik_menu_row*() 内部已经调用，手工建的按钮才需要自己调。
void uik_btn_states(lv_obj_t * btn, uint32_t base);

// 输入框的焦点环：获得焦点时描边变主色并加粗到 2px。
// 触摸模式下 LVGL 不会自动加 LV_STATE_FOCUSED（那需要 indev group），
// 所以这里自己挂一对 FOCUSED/DEFOCUSED 回调，用 LV_STATE_USER_1 当"焦点"标记。
// uik_ta() 内部已经调用。
void uik_ta_focus_ring(lv_obj_t * ta);

// ---------------- 复杂控件的统一皮肤 ----------------
// 软键盘弹起来占半屏，默认主题给的是 LVGL 自带的深灰蓝，和本工程的
// 配色对不上。这里统一成"底色 BG / 键帽 SURFACE / 按下 SURFACE_HI"。
void uik_keyboard(lv_obj_t * kb);

// lv_list 的容器与行样式：默认主题给每行 10px 内边距，240 行高的屏上
// 一屏只能看到 2~3 行。压到 5px 后同样的空间能多看一倍。
void uik_list(lv_obj_t * list);
void uik_list_row(lv_obj_t * row);

// ---------------- 菜单行 ----------------
// 整行可点的条目；返回该行按钮。text 里可以自带图标符号（旧用法），
// 行内第一个子对象是文字标签。新代码请用 uik_menu_row_icon()。
lv_obj_t * uik_menu_row(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, uint32_t bg,
                        lv_event_cb_t cb, void * user_data);

// 带图标徽章的菜单行（推荐）：
//      [徽章] 标题                                   ›
//   徽章是 accent 色的圆角小方块，里面放深色符号；标题用 TITLE 字号。
//   title_out 回传标题标签 —— 旧代码里 ui_LabelN 指的就是它，
//   不要再用 lv_obj_get_child(row, 0) 猜索引（徽章会把索引顶掉）。
lv_obj_t * uik_menu_row_icon(lv_obj_t * parent, int x, int y, int w, int h,
                             const char * symbol, const char * title,
                             uint32_t accent, uint32_t bg,
                             lv_event_cb_t cb, void * user_data,
                             lv_obj_t ** title_out);

// 图标徽章本体（单独用也行，例如片内温度卡片）
lv_obj_t * uik_badge(lv_obj_t * parent, int x, int y, int size,
                     const char * symbol, uint32_t accent);

// 卡片顶部的强调小色条：宽 w、高 3、圆角，给卡片"点一下身份色"
lv_obj_t * uik_accent_bar(lv_obj_t * parent, int x, int y, int w, uint32_t accent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
