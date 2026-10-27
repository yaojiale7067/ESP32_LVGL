// ============================================================================
//  ui_dht11.c - 温湿度屏
//  统一后的样式：顶栏(返回) + 两张面板卡片 + 大号数值 + 底部状态条
//  布局（320x240）：
//      顶栏        y 2~32
//      内容 y 40~190：
//          左卡片 x 12~157 / y 40~190   TEMPERATURE + 大号数值
//          右卡片 x 163~308 / y 40~190  HUMIDITY    + 大号数值
//      状态条      y 212
//  对外符号全部保留（ui_Label3/4/10/rh/temp/Button7/Label8/Panel1/Panel2）
// ============================================================================

#include "../ui.h"
#include "../ui_kit.h"

lv_obj_t * ui_dht11 = NULL;
lv_obj_t * ui_Label3 = NULL;     // 左卡片标题 "TEMPERATURE"
lv_obj_t * ui_Panel1 = NULL;     // 左卡片
lv_obj_t * ui_Panel2 = NULL;     // 右卡片
lv_obj_t * ui_Label4 = NULL;     // 左卡片单位 "°C"
lv_obj_t * ui_Label10 = NULL;    // 右卡片标题 "HUMIDITY"
lv_obj_t * ui_Labelrh = NULL;    // 湿度数值
lv_obj_t * ui_Labeltemp = NULL;  // 温度数值
lv_obj_t * ui_Button7 = NULL;    // 返回
lv_obj_t * ui_Label8 = NULL;
lv_obj_t * ui_dht11_clock = NULL;      // 东八区时间显示
lv_obj_t * ui_dht11_chip = NULL;       // ESP32 片内温度（数值标签）
lv_obj_t * ui_PanelChip = NULL;        // ESP32 片内温度的卡片外框

// ============================================================================
//  几何：全部按"上→下顺序推导"，每一段缝隙都用同一个 UIK_PANEL_GAP
//  ----------------------------------------------------------------------------
//  改之前的问题：缝隙各处手填，5 个间距有 5 个值（顶栏6 / 边距12 / 卡间6 /
//  卡片到片内卡片2 / 到状态条12），其中"卡片到片内卡片"只有 2px，几乎贴住。
//
//  现在【所有】间距都等于同一个 GAP，横竖一样：
//      顶栏              4~ 34
//      ── GAP ──
//      数据卡片         38~150   左右也各留 GAP
//      ── GAP ──
//      片内温度卡片    154~208   横跨整宽
//      ── GAP ──
//      状态条         212~240   (UIK_STATUS_Y 由 ui_kit 固定)
//
//  横向：左边界 MARGIN(4)、右边界 RIGHT_EDGE(308)，与 ui_file.c 用的是
//        同一组 ui_kit 常量，所以各屏右留白一致。
//        两卡片：(308-4)/2 = 152  →  4 + 152 + 4 + 152 = 308 ✓
//  纵向：ROW2_H 不写死，而是"补到状态条前面留一个 GAP 为止"，
//        所以 ROW1_H 一改，ROW2_H 自动跟着变，四段缝隙必然相等，
//        状态条上方也不会留出死空间。
// ============================================================================
#define GAP         UIK_PANEL_GAP               // 唯一的缝隙值（横竖通用）
#define MARGIN      UIK_LEFT                    // 4
#define RIGHT_EDGE  UIK_RIGHT_EDGE              // 308
#define CW          UIK_CONTENT_W               // 304

// 第一行：两张数据卡片
#define ROW1_Y   UIK_CONTENT_Y                  // 38（= 顶栏下沿 + GAP）
#define ROW1_H   112
#define CARD_W   ((CW - GAP) / 2)               // 150
#define CARD_LX  MARGIN                         // 4
#define CARD_RX  (MARGIN + CARD_W + GAP)        // 158（与左卡间距正好是 GAP）

// 第二行：片内温度卡片，横跨整宽，高度补到状态条前一个缝隙
#define ROW2_Y   (ROW1_Y + ROW1_H + GAP)        // 154
#define ROW2_H   (UIK_STATUS_Y - ROW2_Y - GAP)  // 54
#define ROW2_X   MARGIN                         // 4
#define ROW2_W   CW                             // 304

void ui_event_Button7(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Screen1_screen_init);
    }
}

static void back_cb(lv_event_t * e)
{
    ui_event_Button7(e);
}

void ui_dht11_screen_init(void)
{
    ui_dht11 = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_dht11, "Temperature / Humidity", NULL, back_cb, NULL,
               &ui_Button7, NULL);
    if (ui_Button7 != NULL) ui_Label8 = lv_obj_get_child(ui_Button7, 0);

    // ---- 左卡片：温度 ----
    ui_Panel1 = uik_panel(ui_dht11, CARD_LX, ROW1_Y, CARD_W, ROW1_H);

    ui_Label3 = uik_label(ui_Panel1, "TEMPERATURE", 0, 4, UIK_C_TEXT_DIM,
                          UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(ui_Label3, LV_PCT(100));
    lv_obj_align(ui_Label3, LV_ALIGN_TOP_MID, 0, 2);

    ui_Labeltemp = lv_label_create(ui_Panel1);
    lv_label_set_text(ui_Labeltemp, "--");
    lv_obj_set_style_text_font(ui_Labeltemp, UIK_FONT_NUM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Labeltemp, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(ui_Labeltemp, LV_ALIGN_CENTER, -14, 2);

    ui_Label4 = uik_label(ui_Panel1, "deg C", 0, 0, UIK_C_TEXT_DIM,
                          UIK_FONT_HINT, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(ui_Label4, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    // ---- 右卡片：湿度 ----
    ui_Panel2 = uik_panel(ui_dht11, CARD_RX, ROW1_Y, CARD_W, ROW1_H);

    ui_Label10 = uik_label(ui_Panel2, "HUMIDITY", 0, 4, UIK_C_TEXT_DIM,
                           UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(ui_Label10, LV_PCT(100));
    lv_obj_align(ui_Label10, LV_ALIGN_TOP_MID, 0, 2);

    ui_Labelrh = lv_label_create(ui_Panel2);
    lv_label_set_text(ui_Labelrh, "--");
    lv_obj_set_style_text_font(ui_Labelrh, UIK_FONT_NUM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Labelrh, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_align(ui_Labelrh, LV_ALIGN_CENTER, -14, 2);

    lv_obj_t * unit = uik_label(ui_Panel2, "%RH", 0, 0, UIK_C_TEXT_DIM,
                                UIK_FONT_HINT, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    // ---- 片内温度卡片（ESP32-S3 自带的温度传感器）----
    // 和上面两张卡片用同一个 uik_panel，所以"框框"样式完全一致；
    // 与上方卡片的间距也是同一个 GAP。
    // ⚠️ 这是【芯片自身温度】，不是室温：芯片会自己发热，通常比环境高 5~15°C，
    //    且随 CPU 负载、WiFi/BLE 开关变化。只能用来观察是否过热。
    ui_PanelChip = uik_panel(ui_dht11, ROW2_X, ROW2_Y, ROW2_W, ROW2_H);

    lv_obj_t * chip_title = uik_label(ui_PanelChip, "CHIP TEMP", 0, 0,
                                      UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(chip_title, LV_ALIGN_LEFT_MID, 8, 0);

    ui_dht11_chip = uik_label(ui_PanelChip, "--.- °C", 0, 0,
                              UIK_C_TEXT, UIK_FONT_TITLE, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(ui_dht11_chip, LV_ALIGN_RIGHT_MID, -8, 0);

    // ---- 时间显示（东八区）----
    // ★ 时间与片内温度卡片【互斥】：两者都要占第二行那条带（ROW2_Y 起），
    //   同时开会重叠。现在片内温度卡片优先（它是常驻信息），
    //   所以 ENABLE_CLOCK=1 时时间不创建。
    //   以后若两个都要，得改成并排布局（各占半宽），不能简单叠着放。
#if ENABLE_CLOCK
#warning "ENABLE_CLOCK=1 但片内温度卡片已占用第二行，时间标签将不创建"
    ui_dht11_clock = NULL;
#else
    ui_dht11_clock = NULL;
#endif

    uik_status_bar(ui_dht11);
}

void ui_dht11_screen_destroy(void)
{
    if (ui_dht11) lv_obj_del(ui_dht11);

    ui_dht11 = NULL;
    ui_Label3 = NULL;
    ui_Panel1 = NULL;
    ui_Panel2 = NULL;
    ui_Label4 = NULL;
    ui_Label10 = NULL;
    ui_Labelrh = NULL;
    ui_Labeltemp = NULL;
    ui_Button7 = NULL;
    ui_Label8 = NULL;
    ui_dht11_clock = NULL;
    ui_dht11_chip = NULL;
    ui_PanelChip = NULL;
}
