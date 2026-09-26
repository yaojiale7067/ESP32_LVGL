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

// 卡片几何
#define CARD_W   145
#define CARD_H   150
#define CARD_Y   40
#define CARD_LX  12
#define CARD_RX  (UIK_W - CARD_LX - CARD_W)     // 163

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
    ui_Panel1 = uik_panel(ui_dht11, CARD_LX, CARD_Y, CARD_W, CARD_H);

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
    ui_Panel2 = uik_panel(ui_dht11, CARD_RX, CARD_Y, CARD_W, CARD_H);

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

    // ---- 底部状态条（DHT11 记录状态由 LVGL_Demos.cpp 通过本屏状态标签显示）----
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
}
