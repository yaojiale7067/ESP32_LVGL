// ============================================================================
//  ui_wifi.c - WiFi 设置屏
//
//  布局（320x240）：
//      0..37     顶栏：[← Back]   WiFi Settings   [Channels]
//      40..64    小节标题行："NETWORKS"  +  [⟳ Scan]
//      68..174   网络列表（面板外框 + 内嵌 lv_list，正好 4 行整）
//      178..208  密码行：[密码框 210x30]  [Connect 98x30]
//      212..239  状态提示（含上沿分隔线）
//
//  键盘弹出时（占底部 120px）重排：
//      40..71    列表压到一行整，小节标题行整行隐藏
//      86..116   密码行紧贴键盘上沿 —— 输入密码时一定看得见
//  ★ 旧版的密码行固定 y=166，键盘一弹（120..239）就把它和 Connect 一起盖住，
//    用户根本看不到自己输了几个字符。这是这一屏最实际的可用性问题。
//
//  相比旧版：
//    1) 左右留白统一 ui_kit 的 4（旧版 4 但通栏宽算成 308，右留白 8，与卡片对不齐）。
//    2) 列表行改成 uik_list/uik_list_row 的紧凑样式（内边距 10→5），
//       同样高度能多看一倍网络名。
//    3) "扫描"从顶栏挪到列表正上方的小节标题行 —— 它作用的对象就是下面这个列表，
//       放在列表头上比放在屏幕右上角更符合"就近"直觉。
//       顶栏右侧腾出来给 [Channels]，进信道占用分析屏（ui_wscan）。
//    4) 状态行加分隔线，不再是一条悬空的灰字。
// ============================================================================

#include "ui_wifi.h"
#include "../ui.h"       // ui_Screen1 / ui_Screen1_screen_init / _ui_screen_change
#include "ui_wscan.h"    // ui_wscan / ui_wscan_screen_init
#include "../ui_kit.h"

lv_obj_t * ui_wifi = NULL;
lv_obj_t * ui_wifi_list = NULL;
lv_obj_t * ui_wifi_password_ta = NULL;
lv_obj_t * ui_wifi_scan_btn = NULL;
lv_obj_t * ui_wifi_chan_btn = NULL;
lv_obj_t * ui_wifi_connect_btn = NULL;
lv_obj_t * ui_wifi_back_btn = NULL;
lv_obj_t * ui_wifi_status_label = NULL;

static lv_obj_t * keyboard = NULL;
static lv_obj_t * wifi_list_cont = NULL;
static lv_obj_t * wifi_section_label = NULL;

// ---- 几何 ----------------------------------------------------------------
//  ★ 列表可视高度必须是行高的整数倍，否则最后一行被裁一半，看着像渲染坏了。
//    uik_list_row() 的行高 = 12pt 行高 15 + 2*pad4 + 1px 底边框 = 25。
//    uik_panel 的 pad_all 2 + 1px 边框吃掉 6px，所以面板高 = 25*行数 + 6：
//        4 行 -> 106，1 行 -> 31。
#define WF_SEC_Y      (UIK_CONTENT_Y + 2)                     // 40：小节标题行
#define WF_SEC_H      24                                      // 40..64
#define WF_SECBTN_W   76
#define WF_LIST_Y     (WF_SEC_Y + WF_SEC_H + UIK_PANEL_GAP)   // 68
#define WF_LIST_H     (4 * 25 + 6)                            // 106 -> 68..174
#define WF_ROW_Y      (WF_LIST_Y + WF_LIST_H + UIK_PANEL_GAP) // 178：密码行
#define WF_ROW_H      30                                      // 178..208
#define WF_CONN_W     98
#define WF_TA_W       (UIK_CONTENT_W - UIK_PANEL_GAP - WF_CONN_W)   // 210
// 键盘弹起时（键盘占 120..239）：标题行整行隐藏，列表压到一行整，密码行贴键盘上沿
#define WF_LIST_Y_KB  (UIK_CONTENT_Y + 2)                     // 40
#define WF_LIST_H_KB  (1 * 25 + 6)                            // 31 -> 40..71
#define WF_ROW_Y_KB   (UIK_KB_TOP - WF_ROW_H - 4)             // 86 -> 86..116

static void wifi_layout_keyboard_up(bool up)
{
    if (wifi_list_cont) {
        lv_obj_align(wifi_list_cont, LV_ALIGN_TOP_LEFT, UIK_LEFT,
                     up ? WF_LIST_Y_KB : WF_LIST_Y);
        lv_obj_set_height(wifi_list_cont, up ? WF_LIST_H_KB : WF_LIST_H);
    }
    if (ui_wifi_password_ta) {
        lv_obj_align(ui_wifi_password_ta, LV_ALIGN_TOP_LEFT, UIK_LEFT,
                     up ? WF_ROW_Y_KB : WF_ROW_Y);
    }
    if (ui_wifi_connect_btn) {
        lv_obj_align(ui_wifi_connect_btn, LV_ALIGN_TOP_RIGHT, -UIK_PAD,
                     up ? WF_ROW_Y_KB : WF_ROW_Y);
    }
    // 键盘一弹，纵向只够"列表 + 密码行"，标题行（NETWORKS + Scan）整行收起来，
    // 否则压缩后的列表会压到它上面。
    // 顶栏的 [Channels] 不藏 —— 它在顶栏里，键盘压不到，而且是进信道屏的唯一入口。
    lv_obj_t * sec_row[2] = { wifi_section_label, ui_wifi_scan_btn };
    for (int i = 0; i < 2; i++) {
        if (sec_row[i] == NULL) continue;
        if (up) lv_obj_add_flag(sec_row[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_clear_flag(sec_row[i], LV_OBJ_FLAG_HIDDEN);
    }
    // 状态条在键盘区域里，弹出时先收起来，否则会被压在键盘下面
    if (ui_wifi_status_label) {
        if (up) lv_obj_add_flag(ui_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_clear_flag(ui_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        wifi_layout_keyboard_up(false);
        // 整屏失效一次：把"隐藏键盘 + 列表/密码行复位 + 状态条恢复"产生的
        // 多块脏区合并成单次全帧 flush（见 password_value_cb 的注释）
        lv_obj_invalidate(ui_wifi);
    }
}

// ★ 键盘撕裂的修复（用户实测：软键盘弹出/打字时屏幕撕裂）。
//   根因不在刷新率，而在【局部写的性质】：这块 ILI9341 没有 TE 同步，
//   GRAM 是边被面板扫描边被 SPI 写入的。
//     - 小区域写（一个键帽 2KB、输入行 7KB）：撞上扫描线的概率不高，
//       但一旦撞上，新旧内容就【永久】混在同一帧 GRAM 里，直到下次整帧写才消失
//       —— 这正是"键盘区域花掉"的观感。
//     - 整帧写（153KB @ 60MHz ≈ 21ms ≈ 面板帧周期 28.5ms）：撞线只会产生
//       一道瞬时的缝，写完 GRAM 即一致，下一次扫描必然干净。
//       这就是 display_port.cpp 里"tear-safe for full-frame push"的含义。
//   打字路径的全帧合并已经内置在 uik_ta()（VALUE_CHANGED -> 整屏失效），
//   这里只需覆盖【布局切换】三个时机：键盘弹出 / READY / 失焦收起 ——
//   一次切换会同时挪列表、密码行、标题行并隐藏状态条，产生四五块脏区，
//   不合并就是四五次局部 flush、四五次撞线机会。
static void password_focus_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        if (keyboard) {
            lv_keyboard_set_textarea(keyboard, ui_wifi_password_ta);
            wifi_layout_keyboard_up(true);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(ui_wifi);   // 弹出瞬间同样合并成单次全帧写
        }
    }
}

// 密码框失焦（用户点了别处）就把键盘收起来，否则键盘会一直占着底部
static void password_defocus_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DEFOCUSED) {
        if (keyboard) {
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            wifi_layout_keyboard_up(false);
            lv_obj_invalidate(ui_wifi);
        }
    }
}

// 返回按钮：回主菜单。
// 用 _ui_screen_change 而不是裸 lv_scr_load：前者在目标屏还没建时会调用
// ui_Screen1_screen_init() 兜底创建，裸 load 碰上 NULL 会白屏。
static void back_btn_ui_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen1_screen_init);
    }
}

// 顶栏右侧 [Channels]：进信道占用分析屏。
// 同样走 _ui_screen_change —— 那个屏不在 ui_init() 里预建（见 ui.c 的注释），
// 第一次进来时才创建。
static void chan_btn_ui_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_wscan, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_wscan_screen_init);
    }
}

// 扫描和连接按钮留空，业务逻辑在 LVGL_Demos.cpp 中绑定
static void scan_btn_ui_cb(lv_event_t * e) { (void)e; }
static void connect_btn_ui_cb(lv_event_t * e) { (void)e; }

void ui_wifi_screen_init(void) {
    ui_wifi = uik_screen_init();

    // ---------------- 顶栏：左"返回" / 中"标题" / 右"信道分析" ----------------
    // 右上角给 [Channels]（进另一屏 = 导航动作），列表扫描挪到列表头上，
    // 这样每个按钮都紧挨它作用的对象。
    uik_topbar(ui_wifi, "WiFi Settings", "Channels",
               back_btn_ui_cb, chan_btn_ui_cb, &ui_wifi_back_btn, &ui_wifi_chan_btn);
    if (ui_wifi_chan_btn != NULL) {
        // [Channels] 是**导航**（和"返回"同类），不是本页的主操作，
        // 所以从顶栏默认的 PRIMARY 蓝改成 NEUTRAL 灰 —— 主色留给下面的 [⟳ Scan]。
        // 顶栏两个按钮同为灰色，读起来就是"这一排都是走位用的"。
        uik_btn_states(ui_wifi_chan_btn, UIK_C_NEUTRAL);
        // uik_topbar 的右侧按钮是 72 宽 + TITLE(14pt)，"Channels" 八个字符会顶到边。
        // 降到 BODY(12pt) 约 52px，留出呼吸空间；左侧"← Back"仍用 14pt
        // （它带图标，信息量不一样，不必强行对齐字号）。
        lv_obj_t * cl = lv_obj_get_child(ui_wifi_chan_btn, 0);
        if (cl != NULL) lv_obj_set_style_text_font(cl, UIK_FONT_BODY, LV_STATE_DEFAULT);
    }

    // ---------------- 小节标题行：NETWORKS  +  [⟳ Scan] ----------------
    wifi_section_label = uik_label(ui_wifi, "NETWORKS", UIK_LEFT,
                                   WF_SEC_Y + (WF_SEC_H - 12) / 2,
                                   UIK_C_TEXT_DIM, UIK_FONT_HINT, LV_TEXT_ALIGN_LEFT);

    ui_wifi_scan_btn = uik_btn(ui_wifi, LV_SYMBOL_REFRESH " Scan",
                               UIK_RIGHT_EDGE - WF_SECBTN_W, WF_SEC_Y,
                               WF_SECBTN_W, WF_SEC_H, UIK_C_PRIMARY);
    lv_obj_add_event_cb(ui_wifi_scan_btn, scan_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 网络列表 ----------------
    wifi_list_cont = uik_panel(ui_wifi, UIK_LEFT, WF_LIST_Y, UIK_CONTENT_W, WF_LIST_H);
    lv_obj_set_style_pad_all(wifi_list_cont, 2, LV_STATE_DEFAULT);

    ui_wifi_list = lv_list_create(wifi_list_cont);
    // ★ 列表自己撑满面板并且自己滚（不能只写 LV_SIZE_CONTENT）：
    //   lv_list 默认是 SCROLLABLE 的，只有给它一个确定的高度它才会滚；
    //   写 LV_SIZE_CONTENT 会让它长到内容那么高，而外面的面板
    //   （uik_panel 已经清掉 SCROLLABLE）又不会滚 —— 网络一多，
    //   后面的条目就被裁掉、怎么拖都出不来。
    lv_obj_set_size(ui_wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ui_wifi_list, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scrollbar_mode(ui_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    uik_list(ui_wifi_list);          // 统一底色/内边距/滚动条

    // ---------------- 密码 + 连接（同一行，互不重叠） ----------------
    ui_wifi_password_ta = uik_ta(ui_wifi, UIK_LEFT, WF_ROW_Y, WF_TA_W, WF_ROW_H,
                                 "Password", true);
    lv_textarea_set_password_mode(ui_wifi_password_ta, true);
    // 密码用圆点遮住；默认主题给的掩码字符是 '*'，圆点更符合手机上"看不见"的直觉
    lv_textarea_set_password_bullet(ui_wifi_password_ta, "\xE2\x80\xA2");
    // ★ 遮住时长的默认值是 1500ms —— 输入时明文会整整暴露 1.5 秒。
    //   这块屏是在别人面前用的，密码不该在屏幕上停留那么久，改成立即变圆点。
    lv_textarea_set_password_show_time(ui_wifi_password_ta, 0);
    lv_obj_add_event_cb(ui_wifi_password_ta, password_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui_wifi_password_ta, password_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    // 打字时的全帧合并由 uik_ta() 统一挂（见 ui_kit.c 的 uik_ta_full_refresh_cb）

    ui_wifi_connect_btn = uik_btn(ui_wifi, "Connect",
                                  UIK_W - UIK_PAD - WF_CONN_W, WF_ROW_Y,
                                  WF_CONN_W, WF_ROW_H, UIK_C_SUCCESS);
    lv_obj_add_event_cb(ui_wifi_connect_btn, connect_btn_ui_cb, LV_EVENT_CLICKED, NULL);

    // ---------------- 状态条 ----------------
    ui_wifi_status_label = uik_status_bar(ui_wifi);
    lv_label_set_text(ui_wifi_status_label, "Tap Scan to list networks");

    // ---------------- 键盘（隐藏，聚焦密码框时弹出） ----------------
    keyboard = lv_keyboard_create(ui_wifi);
    lv_obj_set_size(keyboard, UIK_W, UIK_KB_H);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(keyboard);
    lv_keyboard_set_textarea(keyboard, ui_wifi_password_ta);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
}

void ui_wifi_screen_destroy(void) {
    if (ui_wifi) lv_obj_del(ui_wifi);
    ui_wifi = NULL;
    ui_wifi_list = NULL;
    ui_wifi_password_ta = NULL;
    ui_wifi_scan_btn = NULL;
    ui_wifi_chan_btn = NULL;
    ui_wifi_connect_btn = NULL;
    ui_wifi_back_btn = NULL;
    ui_wifi_status_label = NULL;
    keyboard = NULL;
    wifi_list_cont = NULL;
    wifi_section_label = NULL;
}