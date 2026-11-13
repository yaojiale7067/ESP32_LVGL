// ============================================================================
//  ui_dht11.c - 温湿度屏
//
//  视觉结构（320x240）：
//      0..37    顶栏：[← Back]   Temperature / Humidity     （下沿 1px 分隔线）
//      38..148  两张数据卡片（各 154x110，中间 4px 缝）
//                  ┌──────────────┐ ┌──────────────┐
//                  │▔▔▔ 琥珀色条 ▔▔│ │▔▔▔ 青色条 ▔▔▔│
//                  │  TEMPERATURE │ │   HUMIDITY   │
//                  │     24.5 °C  │ │     58.0 %RH │   ← 数值+单位同一行，整体居中
//                  └──────────────┘ └──────────────┘
//      152..208 片内温度卡片（312x56，横跨整宽，顶部暖红色条）
//                  CHIP TEMP                        41.3 °C
//      212..239 底部状态提示
//
//  相比旧版：
//    1) 每张卡片顶部加了一条 3px 的身份色条（琥珀/浅青/暖红），三张卡一眼分清。
//       以前三张卡完全同色同框，只有标题文字不同。
//    2) 数值从 28pt 提到 40pt —— 温湿度是这一屏唯一要读的信息，理应最大。
//    3) 单位从 "deg C" 改成真正的 "°C"（U+00B0）。Montserrat 字形表里
//       range_start=176 就是它，一直可用，之前只是写成了 ASCII。
//    4) 单位从"卡片右下角"改成紧跟数值右侧的同一行（flex 行）。
//       固定坐标会因为读数宽度变化（"--" / "24.5" / "-10.5"）而忽远忽近。
//    5) 底部状态条不再是空白：写清传感器与刷新周期。
//
//  对外符号全部保留（ui_Label3/4/10/rh/temp/Button7/Label8/Panel1/Panel2/PanelChip）
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
lv_obj_t * ui_dht11_clock = NULL;      // 东八区时间（本布局不使用，保留符号）
lv_obj_t * ui_dht11_chip = NULL;       // ESP32 片内温度数值标签
lv_obj_t * ui_PanelChip = NULL;        // ESP32 片内温度的卡片外框

// ============================================================================
//  几何：全部按"上→下顺序推导"，每一段缝隙都用同一个 UIK_PANEL_GAP
//  ----------------------------------------------------------------------------
//      顶栏              0 ~ 38（含下沿分隔线）
//      数据卡片         38 ~ 148   左右也各留 GAP
//      ── GAP ──
//      片内温度卡片    152 ~ 208   横跨整宽
//      ── GAP ──
//      状态条         212 ~ 240   (UIK_STATUS_Y 由 ui_kit 固定)
//
//  横向：(312 - 4) / 2 = 154  →  4 + 154 + 4 + 154 = 316 = UIK_RIGHT_EDGE ✓
//  纵向：ROW2_H 不写死，而是"补到状态条前面留一个 GAP 为止"，
//        所以 ROW1_H 一改，ROW2_H 自动跟着变，四段缝隙必然相等。
// ============================================================================
#define GAP         UIK_PANEL_GAP               // 唯一的缝隙值（横竖通用）
#define MARGIN      UIK_LEFT                    // 4
#define CW          UIK_CONTENT_W               // 312

// 第一行：两张数据卡片
#define ROW1_Y   UIK_CONTENT_Y                  // 38（顶栏下沿）
#define ROW1_H   110
#define CARD_W   ((CW - GAP) / 2)               // 154
#define CARD_LX  MARGIN                         // 4
#define CARD_RX  (MARGIN + CARD_W + GAP)        // 162

// 第二行：片内温度卡片，横跨整宽，高度补到状态条前一个缝隙
#define ROW2_Y   (ROW1_Y + ROW1_H + GAP)        // 152
#define ROW2_H   (UIK_STATUS_Y - ROW2_Y - GAP)  // 56
#define ROW2_X   MARGIN                         // 4
#define ROW2_W   CW                             // 312

// 卡片内部：uik_panel 的 pad_all=4 + 1px 边框 → 内容宽 = 卡宽 - 2*(4+1)
#define CARD_CW  (CARD_W - 10)                  // 144
#define CHIP_CW  (ROW2_W - 10)                  // 302

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

// ---------------------------------------------------------------------------
//  一张数据卡片：色条 + 小标题 + "大号数值 + 单位"
//
//  ★ 数值和单位放在一个 flex 行里，整体居中 —— 不能各写各的坐标：
//    数值宽度会随读数变化（"--" → "24.5" → "-10.5"），
//    单位若用固定坐标，短数值时离得远、长数值时又会叠上去。
//    flex 行会跟着内容重排，"24.5 °C" 永远是一个整体。
//
//  cap_out / unit_out 回传标题与单位标签，供旧的 ui_Label3 / ui_Label4 使用；
//  不再用"卡片第几个子对象"去猜，那样一改布局就会错位。
// ---------------------------------------------------------------------------
static lv_obj_t * make_data_card(lv_obj_t * parent, int x, int y, int w, int h,
                                 const char * caption, uint32_t accent,
                                 const char * unit_text,
                                 lv_obj_t ** cap_out, lv_obj_t ** unit_out)
{
    if (cap_out)  *cap_out  = NULL;
    if (unit_out) *unit_out = NULL;

    lv_obj_t * card = uik_panel(parent, x, y, w, h);

    // 身份色条：贴在卡片内容区最上面
    uik_accent_bar(card, 0, 0, CARD_CW, accent);

    lv_obj_t * cap = uik_label(card, caption, 0, 0, UIK_C_TEXT_DIM,
                               UIK_FONT_HINT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(cap, LV_PCT(100));
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 7);
    if (cap_out) *cap_out = cap;

    // 数值 + 单位：一行，整体居中
    lv_obj_t * row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    // 主轴居中、交叉轴贴底 —— 小号单位对齐大号数字的基线附近，不是垂直居中
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(row, 3, LV_STATE_DEFAULT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    // 卡片内容区 100 高：色条 0..3、标题 7..18，剩下 18..100 给数值。
    // 在这 82px 里居中一行 44 高的数字 → 顶到 37、底到 81，
    // 也就是相对整个内容区中心(50)下移 9px。上下留白就均匀了。
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 9);

    lv_obj_t * num = lv_label_create(row);
    lv_label_set_text(num, "--");
    lv_obj_set_style_text_font(num, UIK_FONT_NUM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(num, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
    lv_obj_clear_flag(num, LV_OBJ_FLAG_CLICKABLE);

    if (unit_text != NULL) {
        lv_obj_t * u = lv_label_create(row);
        lv_label_set_text(u, unit_text);
        lv_obj_set_style_text_font(u, UIK_FONT_BODY, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(u, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
        lv_obj_clear_flag(u, LV_OBJ_FLAG_CLICKABLE);
        // 基线对齐（LVGL 8 的 flex 没有 BASELINE，只能手工补偿）：
        //   montserrat_40 的 base_line = 8，montserrat_12 的 base_line = 3。
        //   两个 label 的框底对齐后，小字的基线还比大字低 5px。
        //   补 5px 下内边距，让 <label 高 15+5=20> 的基线正好抬高 5px。
        lv_obj_set_style_pad_bottom(u, 5, LV_STATE_DEFAULT);
        if (unit_out) *unit_out = u;
    }

    return num;
}

void ui_dht11_screen_init(void)
{
    ui_dht11 = uik_screen_init();

    // ---- 顶栏 ----
    uik_topbar(ui_dht11, "Temperature / Humidity", NULL, back_cb, NULL,
               &ui_Button7, NULL);
    if (ui_Button7 != NULL) ui_Label8 = lv_obj_get_child(ui_Button7, 0);

    // ---- 左卡片：温度 ----
    lv_obj_t * cap, * unit;
    ui_Labeltemp = make_data_card(ui_dht11, CARD_LX, ROW1_Y, CARD_W, ROW1_H,
                                  "TEMPERATURE", UIK_ACCENT_TEMP,
                                  "\xC2\xB0" "C",      // °C
                                  &cap, &unit);
    ui_Panel1 = lv_obj_get_parent(ui_Labeltemp);   // 数值的父对象是 flex 行，不是卡片
    ui_Panel1 = lv_obj_get_parent(ui_Panel1);      // 再上一层才是卡片
    ui_Label3 = cap;
    ui_Label4 = unit;

    // ---- 右卡片：湿度 ----
    ui_Labelrh = make_data_card(ui_dht11, CARD_RX, ROW1_Y, CARD_W, ROW1_H,
                                "HUMIDITY", UIK_ACCENT_HUM, "%RH", &cap, NULL);
    ui_Panel2 = lv_obj_get_parent(lv_obj_get_parent(ui_Labelrh));
    ui_Label10 = cap;

    // ---- 片内温度卡片（ESP32-S3 自带的温度传感器）----
    // 和上面两张卡片用同一个 uik_panel，所以"框框"样式完全一致。
    // ⚠️ 这是【芯片自身温度】，不是室温：芯片会自己发热，通常比环境高 5~15°C，
    //    且随 CPU 负载、WiFi/BLE 开关变化。只能用来观察是否过热。
    ui_PanelChip = uik_panel(ui_dht11, ROW2_X, ROW2_Y, ROW2_W, ROW2_H);
    uik_accent_bar(ui_PanelChip, 0, 0, CHIP_CW, UIK_ACCENT_CHIP);

    lv_obj_t * chip_title = uik_label(ui_PanelChip, "CHIP TEMP", 0, 0,
                                      UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(chip_title, LV_ALIGN_LEFT_MID, 2, 3);

    ui_dht11_chip = uik_label(ui_PanelChip, "--.- \xC2\xB0" "C", 0, 0,
                              UIK_C_TEXT, UIK_FONT_BIG, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(ui_dht11_chip, LV_ALIGN_RIGHT_MID, -2, 3);

    // ---- 时间显示（东八区）----
    // ★ 时间与片内温度卡片【互斥】：两者都要占第二行那条带（ROW2_Y 起），
    //   同时开会重叠。现在片内温度卡片优先（它是常驻信息），时间不创建。
    //   以后若两个都要，得改成并排布局（各占半宽），不能简单叠着放。
    //   注意：这里不再发 #warning —— platformio.ini 里 ENABLE_CLOCK 是 1，
    //   每次编译都刷一条警告，会淹没真正需要注意的警告。
    //   ui_dht11_clock 保持 NULL，业务层的 clock_update_once_per_second()
    //   开头就有 NULL 判断，会直接返回，不会崩。
    ui_dht11_clock = NULL;

    // ---- 底部状态条：与其留一条空白，不如写清"这是什么数据" ----
    // ★ 分隔符必须用 U+2022 (•, LV_SYMBOL_BULLET)，不能用 U+00B7 (·)：
    //   Montserrat 的字形表只有 range_start=176 那一段稀疏字符，
    //   里面含 0xB0(°) 和 0x2022(•)，但【没有】0xB7(·)。
    //   用了 0xB7 会画成 "缺字形" 的空心方框。
    lv_obj_t * status = uik_status_bar(ui_dht11);
    lv_label_set_text(status, "DHT11 on GPIO7  " LV_SYMBOL_BULLET "  refresh every 2.5 s");
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