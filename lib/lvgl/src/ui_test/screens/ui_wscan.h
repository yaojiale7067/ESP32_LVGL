// ============================================================================
//  ui_wscan.h - WiFi 信道扫描（2.4GHz 信道占用分析）
//
//  和 ui_wifi（网络列表 / 连网）是两件事，所以单独一屏：
//      ui_wifi  → "我要连哪个 SSID"   （只关心名字和能不能连）
//      ui_wscan → "哪个信道最干净"     （只关心频谱占用，隐藏 SSID 也算）
//
//  本文件只管"画"，不管"扫"：数据由 src/LVGL_Demos.cpp 通过 ui_wscan_set_bars()
//  喂进来，扫描请求通过 ui_wscan_on_scan_requested() 反向回调出去。
//  这样 UI 层不依赖 Arduino / WiFi.h（本文件按 C 编译，那两个头是 C++）。
// ============================================================================

#ifndef UI_WSCAN_H
#define UI_WSCAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../ui_kit.h"

// ---------------- 布局（320x240，纵向梯子见 ui_kit.h） ----------------
//      0..37     顶栏：[← Back]   WiFi Channels   [⟳ Scan]
//      40..52    汇总行（HINT 字号）："26 AP  ·  best CH 11"
//      56..138   信道占用图：13 根柱子 + 柱顶 AP 数 + 柱下信道号 + 基线
//      142..154  详情标题："CH 11 · 3 APs"（点了哪根柱子就显示哪个信道）
//      156..208  AP 详情列表（正好两行，可滚动）
//      212..239  状态提示
#define WSC_SUM_Y       (UIK_CONTENT_Y + 2)                            // 40
#define WSC_SUM_H       12
#define WSC_CHART_Y     (WSC_SUM_Y + WSC_SUM_H + UIK_PANEL_GAP)        // 56
#define WSC_CHART_H     82                                             // 56..138
#define WSC_DETHDR_Y    (WSC_CHART_Y + WSC_CHART_H + UIK_PANEL_GAP)    // 142
#define WSC_DETHDR_H    12
#define WSC_LIST_Y      (WSC_DETHDR_Y + WSC_DETHDR_H + UIK_PANEL_GAP)  // 158
// ★ 50 = 2 x 25：uik_list_row() 的行高是 25（12pt 行高 15 + 2*pad4 + 1px 边框），
//   可视高度必须是行高的整数倍，否则会露出半行，看着像渲染坏了（见 ui_kit.c 注释）。
#define WSC_LIST_H      50                                             // 158..208

// ---------------- 图内几何 ----------------
// 图用 uik_panel 再把 pad_all 压到 0，只剩 1px 边框，
// 所以内容区 = (312 - 2) x (82 - 2) = 310 x 80，子对象坐标都相对这个内容区。
#define WSC_CH_NUM      13      // 2.4GHz 信道数（1..13；ESP32 在 CN 区不用 14）
#define WSC_CONTENT_W   (UIK_CONTENT_W - 2)               // 310
#define WSC_CONTENT_H   (WSC_CHART_H - 2)                 // 80
// 底部 12px 放信道号，顶部留 12px 放"每柱 AP 数"，中间才是柱子
#define WSC_CHLBL_H     12
#define WSC_CHLBL_Y     (WSC_CONTENT_H - WSC_CHLBL_H)     // 68
#define WSC_BASE_Y      (WSC_CHLBL_Y - 1)                 // 67：基线（1px）
#define WSC_BAR_BOT     (WSC_BASE_Y - 1)                  // 66：柱子下沿（含）
#define WSC_BAR_TOPLIM  12                                // 柱顶最高到这一行，上面留给计数标签
#define WSC_BAR_MAXH    (WSC_BAR_BOT - WSC_BAR_TOPLIM + 1) // 55
#define WSC_BAR_MINH    3                                 // 0 个 AP 也留一小截，看得见"这个信道存在"
// 可点槽位覆盖"基线以上的整根柱子"，包括柱顶计数那 12px：
// 触摸目标是整列（23x67），而不是那根最矮只有 3px 的柱子。
#define WSC_SLOT_H      (WSC_BAR_BOT + 1)                 // 67
// 13 个槽位：13 x 23 = 299 <= 310，左右各留 ~5px
#define WSC_SLOT        23
#define WSC_SLOT_INSET  5
#define WSC_BAR_W       15
// 槽位 x（槽位 / 柱顶计数 / 信道号三者共用同一个横向网格）
#define WSC_SLOT_X(i)   (WSC_SLOT_INSET + (i) * WSC_SLOT)
// 柱体本身不用算 x：它是槽位的子对象，靠 LV_ALIGN_BOTTOM_MID 自动水平居中，
// 于是改柱高时只有顶部在动，底沿始终贴着基线。

// ---------------- 控件句柄 ----------------
extern lv_obj_t * ui_wscan;                 // 屏幕本体
extern lv_obj_t * ui_wscan_back_btn;
extern lv_obj_t * ui_wscan_scan_btn;        // 顶栏右侧"重新扫描"
extern lv_obj_t * ui_wscan_summary;         // 汇总行标签
extern lv_obj_t * ui_wscan_detail_hdr;      // 详情标题（"CH 11 · 3 APs"）
extern lv_obj_t * ui_wscan_detail;          // 详情列表（业务层往里塞行）
extern lv_obj_t * ui_wscan_status_label;    // 底部状态

// ---------------- 接口 ----------------
void ui_wscan_screen_init(void);
void ui_wscan_screen_destroy(void);

// 刷新柱状图。per_ch1_13[i] = 信道 (i+1) 上的 AP 数，共 WSC_CH_NUM 个字节；
// best_ch = 推荐信道（1..13，0 表示不标记）。柱高按本次最大值自动归一化。
void ui_wscan_set_bars(const uint8_t * per_ch1_13, uint8_t best_ch);

// 选中某个信道（1..13）；0 = 取消选中。只改视觉（柱子描边 + 信道号高亮）。
void    ui_wscan_select_channel(uint8_t ch);
uint8_t ui_wscan_selected_channel(void);

// 扫描进行中：禁用"重新扫描"按钮，避免连点堆请求。
void ui_wscan_set_scanning(bool scanning);

// 三个文本区（内部先判 NULL，业务层不必关心屏有没有建好）
void ui_wscan_set_summary(const char * text);
void ui_wscan_set_detail_hdr(const char * text);
void ui_wscan_set_status(const char * text, uint32_t color);

// ---------------- 业务回调（实现在 src/LVGL_Demos.cpp） ----------------
void ui_wscan_on_scan_requested(void);              // 点"重新扫描"
void ui_wscan_on_channel_selected(uint8_t channel); // 点了第 channel 根柱子（1..13）
// 建屏完成后调用：业务层把已缓存的扫描结果补画上去。
// 这一屏是懒创建的（不在 ui_init 里），而串口命令 /chan 可以在建屏之前就完成一次扫描，
// 不补画的话第一次进屏会是一张空图，直到用户再点一次 Scan。
void ui_wscan_on_screen_ready(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
