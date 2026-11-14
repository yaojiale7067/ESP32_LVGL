// LVGL_Demos.cpp — UI / 文件管理 / DHT11 / WiFi 业务逻辑
// 只在 LVGL 任务（core0）里执行；main.cpp 的 loop 任务通过 serialCmdQueue
// 把串口命令投递过来，同样由 LVGL 任务消费。

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <DHT.h>
#include <vector>
#include <TJpg_Decoder.h>
#include <demos/lv_demos.h>
#include <examples/lv_examples.h>
#include <time.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/rmt.h>
#include <driver/periph_ctrl.h>

#include "display_port.h"
#include "app_shared.h"
#include "ui_test/ui.h"
#include "ui_test/ui_kit.h"             // UIK_C_* 颜色规范 / UIK_FONT_* 字号
#include "ui_test/screens/ui_file.h"
#include "ui_test/screens/ui_wifi.h"
#include "ui_test/screens/ui_wscan.h"        // 信道占用分析屏
#include "ui_test/screens/ui_relay.h"        // 继电器（GPIO41）控制屏
#include "ui_test/screens/ui_led.h"          // WS2812 灯带（GPIO39）控制屏
#include "ui_test/screens/ui_rfmon.h"        // RF Monitor（802.11 被动监听）
#include "ui_test/screens/ui_nrf.h"          // nRF24 嗅探屏（2.4G 私有协议，被动）
#include "rf_monitor.h"                      // 被动监听数据层（只收不发）
#include "nrf24.h"                           // nRF24L01+ 嗅探数据层（只收不发）
#if ENABLE_BLE
#include "ui_test/screens/ui_ble.h"
#endif

#define DHTPIN  7
#define DHTTYPE DHT11
#define SD_CS   4

#define RELAY_PIN            41
#define RELAY_ACTIVE_LEVEL   HIGH

// POSIX TZ 字符串的符号是反的："CST-8" 才是 UTC+8，写成 "CST+8" 会变成 UTC-8
#define TZ_UTC8  "CST-8"

DHT dht(DHTPIN, DHTTYPE);
SPIClass sdSPI = SPIClass(HSPI);

static String current_path = "/";
static std::vector<String> file_names;
static std::vector<bool>   file_is_dir;
static bool sd_initialized  = false;
static bool showing_editor  = false;
static bool copy_mode       = false;
static bool delete_mode     = false;

static String copy_source_path = "";
static bool   has_copied       = false;

static lv_obj_t* newfile_container = NULL;
static lv_obj_t* newfile_textarea  = NULL;
static lv_obj_t* newfile_keyboard  = NULL;
static bool      creating_new_file = false;

static lv_obj_t* editor_container = NULL;
static lv_obj_t* editor_textarea  = NULL;
static lv_obj_t* editor_keyboard  = NULL;
static String    current_edit_path = "";

static bool dht11_logging = false;
static unsigned long last_log_time = 0;
static const unsigned long LOG_INTERVAL = 3000;

// DHT11 单次读取要关中断 1~2s，两次读取之间必须间隔 >=1s。
// UI 定时器和 SD 写日志若各自读一次，就会出现背靠背读取，第二次必然 NaN。
static unsigned long last_dht_read_ms  = 0;
static float         last_dht_temp     = NAN;
static float         last_dht_humi     = NAN;
static const unsigned long DHT_MIN_INTERVAL = 2500;

static String selected_ssid = "";

// WiFi 扫描状态机（不阻塞 LVGL）
enum WifiScanState { WSCAN_IDLE = 0, WSCAN_WAIT_RESULT };
static WifiScanState  wifi_scan_state = WSCAN_IDLE;
static unsigned long  wifi_scan_start = 0;
static const unsigned long WIFI_SCAN_TIMEOUT_MS = 25000;

// 信道扫描（ui_wscan 屏）的状态机。和上面那个是两套队列、两套状态，
// 因为两件事的语义不同：网络列表扫"能连的名字"，信道扫"频谱占用"。
// 共用一个状态位的话，在一屏点扫描会把另一屏也锁住。
static WifiScanState  wscan_state = WSCAN_IDLE;
static unsigned long  wscan_start = 0;
static const unsigned long WSCAN_TIMEOUT_MS = 25000;   // 全信道 active 扫描约 4s，留足余量
static ChanScanResult_t wscan_last;                    // 最近一次结果，供"点柱子看详情"复用
static bool             wscan_have_data = false;

// TXT 串口发送分片推进，避免整段阻塞 UI
static char*  tx_buffer   = nullptr;
static size_t tx_len      = 0;
static size_t tx_sent     = 0;
static String tx_file     = "";

// 列表延迟重建（不能在按键回调里销毁按键）
static lv_timer_t* file_list_rebuild_timer = NULL;

// 前向声明
void update_file_list();
void update_file_list_now();
void schedule_file_list_update(uint32_t delay_ms);
void read_directory(const char* path);
void show_jpeg_image(const char* path);
void show_json_editor(const char* path);
void save_json_file(const char* path, const String& content);
void close_editor();
void send_txt_via_serial(const char* path);
void pump_txt_transfer();
void copy_file(const char* src_path);
void paste_file(const char* dest_dir);
void delete_file_or_folder(const char* path);
void start_dht11_logging();
void stop_dht11_logging();
void process_serial_command(const String& cmd);
// create_new_text_file() 由 ui_file.c（按 C 编译）调用，必须是 C 链接，
// 声明在 ui_test/screens/ui_file.h，这里不要再写一份 C++ 链接的声明。
void show_newfile_editor(const String& filename);
void close_newfile_editor();
void print_system_info();
void get_ntp_time();
bool storage_mount();
bool wifi_load_config_from_sd(char* ssid, char* password, size_t ssid_size, size_t pwd_size);
void wifi_save_config_to_sd(const char* ssid, const char* password);
void wifi_scan_request();
void wifi_scan_poll();
void wifi_free_list_user_data();
void wscan_request();
void wscan_poll();
static void wscan_render_bars();
static void wscan_render_detail(uint8_t ch);
static void wscan_serial_dump();
void ui_set_status_ex(const char* text, uint32_t color);   // WiFi 屏状态标签
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

// ui_SerialTool.c（纯 C）调用，必须是 C 链接
extern "C" {
    void serial_send(const char* buf) {
        if (buf == nullptr || *buf == '\0') return;
        Serial.println(buf);
    }
}

static void ui_set_status(const char* text, uint32_t color) {
    if (ui_status_label == NULL) return;
    lv_label_set_text(ui_status_label, text);
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
}

// lv_label_set_text() 一进来就调 lv_obj_invalidate()，哪怕传进去的字符串
// 和现在显示的完全一样。定时器每秒无条件刷三个标签，就会在屏幕底部产生
// 周期性的 SPI 重绘区域 —— 在 ILI9341（没有 TE 同步）上表现为"底部偶尔撕裂"。
// 所以先比一下，没变就不写。
static void label_set_text_if_changed(lv_obj_t* lbl, const char* text) {
    if (lbl == NULL || text == NULL) return;
    const char* cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, text) == 0) return;
    lv_label_set_text(lbl, text);
}

// lv_obj_set_style_text_color() 走的是 lv_obj_refresh_style()，
// 而它内部会 invalidate **两次**。同样需要"值没变就不写"。
// 比较时两边都过一遍 lv_color_to32：屏幕是 16 位色深(5-6-5)，
// 直接拿 24 位的 0xRRGGBB 和取出来的值比会因为量化误差永远不相等，
// 于是每次都会写一次样式 —— 等于没起到去抖的作用。
static void label_set_color_if_changed(lv_obj_t* lbl, uint32_t rgb) {
    if (lbl == NULL) return;
    lv_color_t cur = lv_obj_get_style_text_color(lbl, LV_PART_MAIN);
    if (lv_color_to32(cur) == lv_color_to32(lv_color_hex(rgb))) return;
    lv_obj_set_style_text_color(lbl, lv_color_hex(rgb), LV_STATE_DEFAULT);
}

static bool is_jpeg_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

static bool is_json_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".json");
}

static bool is_txt_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".txt");
}

// DHT11 统一入口：限速 + 缓存
static bool dht_read_cached(float* temp, float* humi, bool force) {
    unsigned long now = millis();
    if (!force && last_dht_read_ms != 0 && (now - last_dht_read_ms) < DHT_MIN_INTERVAL) {
        // 间隔不足：复用上一次成功的结果
        if (isnan(last_dht_temp) || isnan(last_dht_humi)) return false;
        *temp = last_dht_temp;
        *humi = last_dht_humi;
        return true;
    }
    last_dht_read_ms = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
        // 只更新时间戳，保留上一次的有效值，避免一次抖动就显示 "--"
        return false;
    }
    last_dht_temp = t;
    last_dht_humi = h;
    *temp = t;
    *humi = h;
    return true;
}

void update_dht11_display() {
    float t, h;
    if (!dht_read_cached(&t, &h, false)) {
        label_set_text_if_changed(ui_Labeltemp, "--");
        label_set_text_if_changed(ui_Labelrh,   "--");
        return;
    }
    char temp_str[16], humi_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f", t);
    snprintf(humi_str, sizeof(humi_str), "%.1f", h);
    label_set_text_if_changed(ui_Labeltemp, temp_str);
    label_set_text_if_changed(ui_Labelrh,   humi_str);
}

// 片内温度：读到的是芯片自身温度（通常比环境高 5~15°C），只适合判断是否过热。
// temperatureRead() 每次都会 start/stop 传感器，必须限速，不能每帧读。
#define CHIP_TEMP_MIN_INTERVAL 5000UL        // 5 秒读一次足够

static unsigned long last_chip_read_ms = 0;
static float         last_chip_temp    = NAN;

static bool chip_temp_read_cached(float* out) {
    unsigned long now = millis();
    if (last_chip_read_ms != 0 && (now - last_chip_read_ms) < CHIP_TEMP_MIN_INTERVAL) {
        if (isnan(last_chip_temp)) return false;
        *out = last_chip_temp;
        return true;
    }
    last_chip_read_ms = now;
    float c = temperatureRead();
    if (isnan(c)) return false;              // 读失败：保留上一次的值
    last_chip_temp = c;
    *out = c;
    return true;
}

static void update_chip_temp_display(void) {
    if (ui_dht11_chip == NULL) return;
    float c;
    if (!chip_temp_read_cached(&c)) {
        label_set_text_if_changed(ui_dht11_chip, "--.- \xC2\xB0" "C");
        return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f \xC2\xB0" "C", c);
    label_set_text_if_changed(ui_dht11_chip, buf);
    label_set_color_if_changed(ui_dht11_chip, c >= 70.0f ? UIK_C_ERR_TEXT : UIK_C_TEXT);
}

// 时区与校时（东八区）：屏幕时钟和串口的 date 命令共用同一套设置
static bool s_tz_applied = false;
static bool s_sntp_started = false;

// 用 POSIX 的 TZ（setenv + tzset），而不是 newlib 的 _tzname：
// 后者只是显示用的名字，不会真正把 localtime 偏移成东八区。
static void tz_apply_utc8(void)
{
    if (s_tz_applied) return;
    setenv("TZ", TZ_UTC8, 1);
    tzset();
    s_tz_applied = true;
    Serial.printf("[TIME] timezone set to UTC+8 (TZ=\"%s\")\n", TZ_UTC8);
}

// 启动 SNTP 校时，幂等
static void sntp_start_once(void)
{
    tz_apply_utc8();
    if (s_sntp_started) return;
    s_sntp_started = true;
    // configTime 的单位是秒；UTC+8 = 28800 秒。内部会启动 SNTP 并定期重同步。
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("[TIME] SNTP started (pool.ntp.org / time.nist.gov)");
}

#if ENABLE_CLOCK
// 用年号判断是否已校时（sntp_get_sync_status 在不同 IDF 版本里头文件位置不一致）。
// 只有时钟功能需要它，所以一起用 ENABLE_CLOCK 包住，否则关时钟会报 -Wunused-function。
static bool time_is_synced(void)
{
    time_t now = time(nullptr);
    if (now < 100000) return false;                 // 还没跨过 1970-01-02
    struct tm t;
    if (localtime_r(&now, &t) == nullptr) return false;
    return (t.tm_year + 1900) >= 2020;
}
#endif  // ENABLE_CLOCK

#if ENABLE_CLOCK
// 刷新屏幕时钟（东八区）。只在秒变化时才 set_text，避免每个 tick 都重绘。
static void clock_update_once_per_second(void)
{
    if (ui_dht11_clock == NULL) return;

    static unsigned long last_ms = 0;
    static int last_sec = -1;

    unsigned long now_ms = millis();
    if (now_ms - last_ms < 250) return;             // 不必比 4Hz 更勤
    last_ms = now_ms;

    if (!time_is_synced()) {
        // 未同步时显示占位符，而不是 1970 年那种假时间
        if (last_sec != -2) {
            last_sec = -2;
            lv_label_set_text(ui_dht11_clock, "--:--:--  (waiting NTP)");
            lv_obj_set_style_text_color(ui_dht11_clock, lv_color_hex(UIK_C_TEXT_DIM),
                                        LV_STATE_DEFAULT);
        }
        // WiFi 连上后自动开始校时（幂等）
        if (WiFi.status() == WL_CONNECTED) sntp_start_once();
        return;
    }

    time_t now_t = time(nullptr);
    struct tm t;
    if (localtime_r(&now_t, &t) == nullptr) return;
    if (t.tm_sec == last_sec) return;               // 同一秒内不重复设文本
    last_sec = t.tm_sec;

    char buf[24];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    lv_label_set_text(ui_dht11_clock, buf);
    lv_obj_set_style_text_color(ui_dht11_clock, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);
}
#endif  // ENABLE_CLOCK

// 定时器只负责显示，读不读由 dht_read_cached 的限速决定
void dht_timer_cb(lv_timer_t* timer) {
    (void)timer;
    update_dht11_display();
    update_chip_temp_display();         // 片内温度（内部另有 5 秒限速）
#if ENABLE_CLOCK
    clock_update_once_per_second();
#endif
}

// 保证温湿度标签是干净的占位符；函数名保留给 original_setup() 的调用点。
void fix_dht11_label_display() {
    if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, "--");
    if (ui_Labelrh   != NULL) lv_label_set_text(ui_Labelrh,   "--");
    if (ui_dht11_chip != NULL) {
        lv_label_set_text(ui_dht11_chip, "--.- °C");
        lv_obj_set_style_text_color(ui_dht11_chip, lv_color_hex(UIK_C_TEXT),
                                    LV_STATE_DEFAULT);
    }
}

// SD 卡：挂载 + 加锁的公共入口
bool storage_mount() {
    if (sd_initialized) return true;             // 幂等：重复挂载会二次初始化 HSPI
    SD_LOCK();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    sdSPI.begin(12, 13, 11, SD_CS);
    delay(10);
    // 显式给 20MHz（默认 4MHz 太慢）；与 TFT 共用 HSPI，频率各自在 beginTransaction 里设置
    bool ok = SD.begin(SD_CS, sdSPI, 20000000);
    SD_UNLOCK();

    if (!ok) {
        sd_initialized = false;
        Serial.println("SD Card Mount Failed!");
        return false;
    }
    sd_initialized = true;
    TJpgDec.setCallback(tft_output);
    TJpgDec.setJpgScale(1);
    Serial.println("SD Card mounted");
    return true;
}

void read_directory(const char* path) {
    if (!sd_initialized) return;
    SD_LOCK();
    file_names.clear();
    file_is_dir.clear();
    File root = SD.open(path);
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            file_names.push_back(String(file.name()));
            file_is_dir.push_back(file.isDirectory());
            file = root.openNextFile();
        }
    } else {
        Serial.printf("read_directory: cannot open %s\n", path);
    }
    if (root) root.close();
    SD_UNLOCK();
}

// 列表重建：不能在按键回调里销毁按键
static void file_list_rebuild_cb(lv_timer_t* t) {
    (void)t;
    file_list_rebuild_timer = NULL;
    update_file_list_now();
}

void schedule_file_list_update(uint32_t delay_ms) {
    if (file_list_rebuild_timer != NULL) return;      // 已经排了一次
    file_list_rebuild_timer = lv_timer_create(file_list_rebuild_cb, delay_ms, NULL);
    if (file_list_rebuild_timer) lv_timer_set_repeat_count(file_list_rebuild_timer, 1);
}

// 文件管理器的模式回调（ui_file.c 按 C 编译，必须 C 链接）
extern "C" void file_set_mode_normal(void)
{
    copy_mode = false;
    delete_mode = false;
    schedule_file_list_update(1);
}

extern "C" void file_set_mode_copy(void)
{
    copy_mode = true;
    delete_mode = false;
    ui_set_status("Copy mode: tap a file to copy it", UIK_C_OK_TEXT);
    schedule_file_list_update(1);
}

extern "C" void file_set_mode_delete(void)
{
    copy_mode = false;
    delete_mode = true;
    ui_set_status("Delete mode: tap a file/folder to delete", UIK_C_ERR_TEXT);
    schedule_file_list_update(1);
}

// 底部操作行：选中的 .txt 全路径。点列表里的 .txt 只是选中，
// 发送 / 编辑由底部按钮决定。
static String selected_txt_path = "";

extern "C" void file_send_selected(void)
{
    if (selected_txt_path.length() == 0) {
        ui_set_status("Select a .txt file first", UIK_C_WARN_TEXT);
        return;
    }
    send_txt_via_serial(selected_txt_path.c_str());
}

extern "C" void file_edit_selected(void)
{
    if (selected_txt_path.length() == 0) {
        ui_set_status("Select a .txt file first", UIK_C_WARN_TEXT);
        return;
    }
    // 复用"新建文件"那套编辑器（文本区 + 键盘 + X 键保存）
    show_newfile_editor(selected_txt_path);
}

extern "C" void file_paste_here(void)
{
    if (!has_copied) {
        ui_set_status("Nothing to paste. Copy a file first.", UIK_C_ERR_TEXT);
        return;
    }
    paste_file(current_path.c_str());
}

void update_file_list_now() {
    if (ui_file_list == NULL) return;
    lv_obj_clean(ui_file_list);

    // 三个模式按钮的高亮由业务层驱动
    ui_file_update_mode_buttons(copy_mode, delete_mode);

    // 路径行：带上"已复制哪个文件"的提示
    String copied_name = "";
    if (has_copied) {
        copied_name = copy_source_path.substring(copy_source_path.lastIndexOf('/') + 1);
    }
    ui_file_set_path_text(current_path.c_str(), copied_name.c_str());

    // 换了目录后，不在当前目录下的选中项要清掉
    if (selected_txt_path.length() > 0) {
        String prefix = (current_path == "/") ? String("/") : (current_path + "/");
        if (!selected_txt_path.startsWith(prefix)) selected_txt_path = "";
    }

    // 底部操作行只有 Send / Edit 两个按钮的状态要更新
    {
        String sel_name = "";
        if (selected_txt_path.length() > 0) {
            sel_name = selected_txt_path.substring(selected_txt_path.lastIndexOf('/') + 1);
        }
        ui_file_set_send_enabled(selected_txt_path.length() > 0, sel_name.c_str());
    }

    if (!sd_initialized) {
        lv_obj_t* label = lv_label_create(ui_file_list);
        lv_label_set_text(label, "SD Card not found\nTap the refresh button to retry");
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_hex(UIK_C_ERR_TEXT), LV_STATE_DEFAULT);
        return;
    }

    // 占满 ui_file_list 的内容区（尺寸由 ui_file.c 决定，这里不写死像素）
    lv_obj_t* scroll_panel = lv_obj_create(ui_file_list);
    lv_obj_set_size(scroll_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_align(scroll_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(scroll_panel, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scroll_panel, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scroll_panel, 2, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(scroll_panel, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* list = lv_list_create(scroll_panel);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 0);
    uik_list(list);      // 统一底色/内边距/滚动条（详见 ui_kit.c）

    // 粘贴入口做成列表首行（底部再放第三个按钮就得压字号，纵向空间不够）
    if (has_copied) {
        lv_obj_t* paste_row = lv_list_add_btn(list, LV_SYMBOL_PASTE, " Paste Here");
        uik_list_row(paste_row);
        lv_obj_set_style_bg_color(paste_row, lv_color_hex(UIK_C_SUCCESS), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(paste_row, [](lv_event_t* e) {
            (void)e;
            file_paste_here();
        }, LV_EVENT_CLICKED, NULL);
    }

    if (current_path != "/") {
        lv_obj_t* back_btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, ".. (Parent)");
        uik_list_row(back_btn);
        lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
            (void)e;
            if (current_path == "/") return;
            int last_slash = current_path.lastIndexOf('/');
            current_path = (last_slash <= 0) ? String("/") : current_path.substring(0, last_slash);
            read_directory(current_path.c_str());
            schedule_file_list_update(1);
        }, LV_EVENT_CLICKED, NULL);
    }

    for (size_t i = 0; i < file_names.size(); i++) {
        String name        = file_names[i];
        bool is_dir_item   = file_is_dir[i];
        const char* symbol = is_dir_item ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        String display_name = name;
        if (!is_dir_item) {
            // [IMG] 看图、[EDIT] 编辑 JSON、[TXT] 仅选中（发送/编辑交给底部按钮）
            if (is_jpeg_file(name))      display_name = name + " [IMG]";
            else if (is_json_file(name)) display_name = name + " [EDIT]";
            else if (is_txt_file(name))  display_name = name + " [TXT]";
        }
        lv_obj_t* item_btn = lv_list_add_btn(list, symbol, display_name.c_str());
        uik_list_row(item_btn);      // 紧凑行高 + 统一按下反馈 + 长文件名截断
        lv_obj_set_user_data(item_btn, (void*)(intptr_t)i);
        // 模式指示色：暗一档的同色系，不用饱和的 0x336633 / 0x663333。
        // 饱和色在深色列表上会跳出来，看着像"已选中"而不是"当前处于某模式"。
        if (copy_mode && !is_dir_item) lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x1F4032), LV_STATE_DEFAULT);
        else if (delete_mode)          lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x4A2222), LV_STATE_DEFAULT);
        // 当前选中的 txt 高亮成蓝色
        else if (selected_txt_path.length() > 0) {
            String this_path = (current_path == "/") ? ("/" + name) : (current_path + "/" + name);
            if (this_path == selected_txt_path) {
                lv_obj_set_style_bg_color(item_btn, lv_color_hex(UIK_C_PRIMARY), LV_STATE_DEFAULT);
            }
        }

        lv_obj_add_event_cb(item_btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx < 0 || idx >= (int)file_names.size()) return;

            // 先取出所有需要的数据 —— 重建列表后 btn 就失效了
            String  name     = file_names[idx];
            bool    is_dir   = file_is_dir[idx];
            String  full_path;
            if (current_path == "/") full_path = "/" + name;
            else                     full_path = current_path + "/" + name;

            if (delete_mode) {
                delete_mode = false;
                delete_file_or_folder(full_path.c_str());   // 内部会 schedule 重建
                return;
            }
            if (copy_mode && !is_dir) {
                copy_mode = false;
                copy_file(full_path.c_str());
                schedule_file_list_update(1);
                return;
            }
            if (is_dir) {
                current_path = (current_path == "/") ? ("/" + name) : (current_path + "/" + name);
                read_directory(current_path.c_str());
                schedule_file_list_update(1);
                return;
            }
            if (is_jpeg_file(name))      { show_jpeg_image(full_path.c_str()); }
            else if (is_json_file(name)) { show_json_editor(full_path.c_str()); }
            else if (is_txt_file(name))  {
                // 只选中不发送，这样列表里已有的 txt 也能被编辑
                selected_txt_path = full_path;
                char msg[128];
                snprintf(msg, sizeof(msg), "Selected: %s", name.c_str());
                ui_set_status(msg, UIK_C_OK_TEXT);
                schedule_file_list_update(1);      // 刷新高亮和按钮状态
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    if (file_names.empty()) {
        lv_obj_t* hint = lv_label_create(list);
        lv_label_set_text(hint, (current_path == "/")
                                ? "No files found\nTap 'New' to create one"
                                : "Empty directory\nTap 'New' to create one");
        lv_obj_center(hint);
        lv_obj_set_style_text_color(hint, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    }
}

// 兼容旧调用点：立即重建（不能在按键回调里调用）
void update_file_list() {
    update_file_list_now();
}

void refresh_file_list_cb(lv_event_t* e) {
    (void)e;
    if (!sd_initialized) {
        // SD 没挂上时，点刷新可以重试挂载
        if (!storage_mount()) {
            ui_set_status("SD mount failed", UIK_C_ERR_TEXT);
            update_file_list_now();
            return;
        }
    }
    read_directory(current_path.c_str());
    schedule_file_list_update(1);
    ui_set_status("Refreshed", UIK_C_OK_TEXT);
}

// 新建文件
void close_newfile_editor() {
    // 先删子对象再删容器，删完立即把指针置空，避免野指针
    if (newfile_keyboard  != NULL) { lv_obj_del(newfile_keyboard);  newfile_keyboard  = NULL; }
    if (newfile_textarea  != NULL) { newfile_textarea  = NULL; }   // 随容器一起销毁
    if (newfile_container != NULL) { lv_obj_del(newfile_container); newfile_container = NULL; }
    creating_new_file = false;
    read_directory(current_path.c_str());
    schedule_file_list_update(1);
}

static void newfile_save_from_textarea() {
    if (newfile_textarea == NULL) return;
    const char* text = lv_textarea_get_text(newfile_textarea);
    if (text == NULL) return;
    SD_LOCK();
    File file = SD.open(current_edit_path.c_str(), FILE_WRITE);
    if (file) {
        file.print(text);
        file.close();
        Serial.printf("Saved: %s (%u bytes)\n", current_edit_path.c_str(), (unsigned)strlen(text));
        ui_set_status("File saved!", UIK_C_OK_TEXT);
    } else {
        Serial.printf("Failed to save: %s\n", current_edit_path.c_str());
        ui_set_status("Save failed!", UIK_C_ERR_TEXT);
    }
    SD_UNLOCK();
}

void newfile_kb_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {          // 键盘上的"回车/√"
        newfile_save_from_textarea();
        close_newfile_editor();
    } else if (code == LV_EVENT_CANCEL) {  // 键盘上的"×"
        close_newfile_editor();
    }
}

void show_newfile_editor(const String& path) {
    if (newfile_container != NULL) return;      // 已经在编辑了，别叠第二层

    creating_new_file = true;
    current_edit_path = path;

    // ---- 编辑器浮层 ----
    // 这一层以前是硬编码的一套配色（0x1E1E1E / 0x2D2D2D / 0xFF5555 / 0x555555），
    // 和主界面的 ui_kit 规范不是一路，从文件管理器点进来像切到了另一个软件。
    // 现在统一走 ui_kit：容器=屏幕底色，标题栏=面板色，
    // 保存=SUCCESS 绿，输入框=uik_ta，键盘=uik_keyboard。
    //
    // 布局：标题栏 0..38，输入区 40..136，键盘 140..240（高 100）。
    // 键盘这一屏用 100 而不是 UIK_KB_H(120)，因为这里要多留输入区。
    const int kb_h = 100;
    newfile_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(newfile_container, DISP_W, DISP_H);
    lv_obj_set_pos(newfile_container, 0, 0);
    lv_obj_set_style_bg_color(newfile_container, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(newfile_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(newfile_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(newfile_container);
    lv_obj_set_size(title_bar, DISP_W, UIK_CONTENT_Y);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    String title_text = "New File: " + path.substring(path.lastIndexOf('/') + 1);
    lv_label_set_text(title_label, title_text.c_str());
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, DISP_W - 2 * UIK_PAD - 68);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, UIK_PAD, 0);
    lv_obj_set_style_text_font(title_label, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UIK_C_TEXT), LV_STATE_DEFAULT);

    // 这个按钮是"保存并关闭"，所以直接写成 Save —— 原来是个红底 X，
    // 用户会以为按下去内容被丢掉了。绿色 + 保存图标才说得清。
    lv_obj_t* close_btn = uik_btn(title_bar, LV_SYMBOL_SAVE " Save",
                                  DISP_W - UIK_PAD - 68, UIK_TOPBAR_Y, 68, UIK_TOPBAR_H,
                                  UIK_C_SUCCESS);
    // 先保存再关闭，避免用户以为点了关闭内容还在
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        (void)e;
        newfile_save_from_textarea();
        close_newfile_editor();
    }, LV_EVENT_CLICKED, NULL);

    uik_separator(newfile_container, 0, UIK_CONTENT_Y - 1, DISP_W);

    newfile_textarea = uik_ta(newfile_container, UIK_LEFT, UIK_CONTENT_Y + 2,
                              UIK_CONTENT_W, (DISP_H - kb_h) - UIK_CONTENT_Y - 6,
                              "Enter text here...", false);
    lv_textarea_set_cursor_click_pos(newfile_textarea, true);

    newfile_keyboard = lv_keyboard_create(newfile_container);
    lv_keyboard_set_textarea(newfile_keyboard, newfile_textarea);
    lv_obj_set_size(newfile_keyboard, DISP_W, kb_h);
    lv_obj_align(newfile_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    uik_keyboard(newfile_keyboard);
    lv_obj_add_event_cb(newfile_keyboard, newfile_kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(newfile_keyboard, newfile_kb_event_cb, LV_EVENT_CANCEL, NULL);
}

// 由 ui_file.c 工具栏的 "New" 按钮调用（C 链接）
extern "C" void create_new_text_file() {
    if (creating_new_file) {
        Serial.println("New-file editor already open");
        return;
    }
    int nextNum = 1;
    for (size_t i = 0; i < file_names.size(); i++) {
        const String& name = file_names[i];
        if (!file_is_dir[i] && name.startsWith("newtext") && name.endsWith(".txt")) {
            int num = name.substring(7, name.length() - 4).toInt();
            if (num >= nextNum) nextNum = num + 1;
        }
    }
    String filename  = "newtext" + String(nextNum) + ".txt";
    String full_path = (current_path == "/") ? ("/" + filename) : (current_path + "/" + filename);

    SD_LOCK();
    File file = SD.open(full_path.c_str(), FILE_WRITE);
    bool ok = (bool)file;
    if (ok) file.close();
    SD_UNLOCK();

    if (!ok) {
        Serial.printf("Failed to create file: %s\n", full_path.c_str());
        ui_set_status("Failed to create file!", UIK_C_ERR_TEXT);
        return;
    }
    Serial.printf("Created file: %s\n", full_path.c_str());
    show_newfile_editor(full_path);
}

// JSON 编辑器
#define JSON_EDIT_MAX_BYTES 32768

static String read_text_file(const char* path, size_t max_bytes, bool* truncated) {
    *truncated = false;
    SD_LOCK();
    File file = SD.open(path);
    if (!file) { SD_UNLOCK(); return String(); }

    size_t fsize = file.size();
    if (fsize > max_bytes) { *truncated = true; fsize = max_bytes; }

    String content;
    content.reserve(fsize + 1);
    uint8_t buf[256];
    size_t remaining = fsize;
    while (remaining > 0) {
        size_t want = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        size_t got  = file.read(buf, want);
        if (got == 0) break;
        content.concat((const char*)buf, got);  
        remaining -= got;
    }
    file.close();
    SD_UNLOCK();
    return content;
}

void save_json_file(const char* path, const String& content) {
    SD_LOCK();
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        SD_UNLOCK();
        Serial.printf("Failed to save: %s\n", path);
        return;
    }
    file.print(content);
    file.close();
    SD_UNLOCK();
    Serial.printf("Saved: %s\n", path);
}

void close_editor() {
    showing_editor = false;
    editor_textarea = NULL;
    if (editor_keyboard  != NULL) { lv_obj_del(editor_keyboard);  editor_keyboard  = NULL; }
    if (editor_container != NULL) { lv_obj_del(editor_container); editor_container = NULL; }
    schedule_file_list_update(1);
}

void save_and_close_cb(lv_event_t* e) {
    (void)e;
    if (editor_textarea != NULL && current_edit_path.length() > 0) {
        save_json_file(current_edit_path.c_str(), String(lv_textarea_get_text(editor_textarea)));
        ui_set_status("Saved", UIK_C_OK_TEXT);
    }
    close_editor();
}

void show_json_editor(const char* path) {
    if (showing_editor) return;

    bool truncated = false;
    String content = read_text_file(path, JSON_EDIT_MAX_BYTES, &truncated);

    // 超限时只读打开，绝不提供保存入口（截断后保存会把原文件写坏）
    if (truncated) {
        ui_set_status("File too big (>32KB), read-only", UIK_C_WARN_TEXT);
    }
    showing_editor = true;
    current_edit_path = String(path);
    if (content.length() == 0) content = "";

    // ---- 编辑器浮层（与 show_newfile_editor 同一套 ui_kit 配色）----
    // 布局：标题栏 0..38，正文 40..136，键盘 140..240（高 100）。
    // 只读模式没有键盘，正文就一路铺到底（40..236），能看到更多内容。
    const int kb_h = 100;
    const int body_h = truncated ? (DISP_H - UIK_CONTENT_Y - 4)
                                 : ((DISP_H - kb_h) - UIK_CONTENT_Y - 6);

    editor_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(editor_container, DISP_W, DISP_H);
    lv_obj_set_pos(editor_container, 0, 0);
    lv_obj_set_style_bg_color(editor_container, lv_color_hex(UIK_C_BG), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(editor_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(editor_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(editor_container);
    lv_obj_set_size(title_bar, DISP_W, UIK_CONTENT_Y);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(UIK_C_SURFACE), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    // 关闭键永远在最右边（56 宽）；Save 在它左边（可编辑时才有）。
    const int BTN_W = 56;
    const int close_x = DISP_W - UIK_PAD - BTN_W;

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, truncated ? "JSON (READ-ONLY)" : "JSON Editor");
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, close_x - UIK_PANEL_GAP - (truncated ? 0 : BTN_W + UIK_PANEL_GAP));
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, UIK_PAD, 0);
    lv_obj_set_style_text_font(title_label, UIK_FONT_TITLE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label,
        lv_color_hex(truncated ? UIK_C_WARN_TEXT : UIK_C_TEXT), LV_STATE_DEFAULT);

    if (!truncated) {
        lv_obj_t* save_btn = uik_btn(title_bar, LV_SYMBOL_SAVE " Save",
                                     close_x - UIK_PANEL_GAP - BTN_W, UIK_TOPBAR_Y,
                                     BTN_W, UIK_TOPBAR_H, UIK_C_SUCCESS);
        lv_obj_add_event_cb(save_btn, save_and_close_cb, LV_EVENT_CLICKED, NULL);
    }

    // 关闭键：这里只关不存（和新建文件那个"保存并关闭"不同），
    // 所以用 DANGER 红 + 叉号，明确"未保存的改动会丢"。
    lv_obj_t* close_btn = uik_btn(title_bar, LV_SYMBOL_CLOSE, close_x, UIK_TOPBAR_Y,
                                  BTN_W, UIK_TOPBAR_H, UIK_C_DANGER);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) { (void)e; close_editor(); },
                        LV_EVENT_CLICKED, NULL);

    uik_separator(editor_container, 0, UIK_CONTENT_Y - 1, DISP_W);

    editor_textarea = uik_ta(editor_container, UIK_LEFT, UIK_CONTENT_Y + 2,
                             UIK_CONTENT_W, body_h, "Edit JSON content...", false);
    lv_textarea_set_text(editor_textarea, content.c_str());

    // 只读模式下不给键盘，物理上就没法改
    if (!truncated) {
        editor_keyboard = lv_keyboard_create(editor_container);
        lv_keyboard_set_textarea(editor_keyboard, editor_textarea);
        lv_obj_set_size(editor_keyboard, DISP_W, kb_h);
        lv_obj_align(editor_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        uik_keyboard(editor_keyboard);
        lv_obj_add_event_cb(editor_keyboard, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_READY) {
                save_and_close_cb(e);
            } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
                close_editor();
            }
        }, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(editor_keyboard, [](lv_event_t* e) {
            (void)e;
            close_editor();
        }, LV_EVENT_CANCEL, NULL);
    }
}

// JPEG 显示
// TJpgDec 的输出回调：转发到显示移植层（那边内部已加 SPI 总线锁）
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    return disp_tft_output(x, y, w, h, bitmap);
}

void show_jpeg_image(const char* path) {
    // 清屏后立即刷一帧，保证 LVGL 的缓冲不会在下面直接画图时被改写
    disp_fill_screen(TFT_BLACK);

    uint16_t w = 0, h = 0;
    JRESULT jr = TJpgDec.getSdJpgSize(&w, &h, path);
    if (jr != JDR_OK || w == 0 || h == 0) {
        Serial.printf("JPEG header failed (%d): %s\n", (int)jr, path);
        ui_set_status("Not a valid JPEG", UIK_C_ERR_TEXT);
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        return;
    }

    int16_t x = (DISP_W > (int)w) ? (DISP_W - w) / 2 : 0;
    int16_t y = (DISP_H > (int)h) ? (DISP_H - h) / 2 : 0;

    TJpgDec.setCallback(tft_output);
    TJpgDec.setJpgScale(1);
    jr = TJpgDec.drawSdJpg(x, y, path);
    if (jr != JDR_OK) {
        Serial.printf("JPEG decode failed (%d): %s\n", (int)jr, path);
        ui_set_status("JPEG decode failed", UIK_C_ERR_TEXT);
    } else {
        // 轮询等待抬手，期间继续跑 lv_timer_handler()，界面不会假死
        bool was_down = false;
        unsigned long t0 = millis();
        while (millis() - t0 < 30000UL) {
            bool down = disp_touch_pressed();
            if (was_down && !down) break;        // 抬手才算确认
            was_down = down;
            lv_timer_handler();
            delay(10);
        }
    }

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    ui_set_status("Image closed", UIK_C_OK_TEXT);
}

// 文件操作
void delete_file_or_folder(const char* path) {
    String path_str = String(path);

    SD_LOCK();
    File probe = SD.open(path);
    bool is_directory = (bool)probe && probe.isDirectory();
    if (probe) probe.close();

    if (is_directory) {
        File dir = SD.open(path);
        if (dir) {
            File file = dir.openNextFile();
            while (file) {
                String child_path = (path_str == "/") ? ("/" + String(file.name()))
                                                      : (path_str + "/" + String(file.name()));
                bool child_is_dir = file.isDirectory();
                file.close();                    // 递归删除前先关掉句柄
                if (child_is_dir) delete_file_or_folder(child_path.c_str());
                else {
                    SD.remove(child_path);
                    Serial.printf("Deleted file: %s\n", child_path.c_str());
                }
                file = dir.openNextFile();
            }
            dir.close();
        }
        SD.rmdir(path);
        Serial.printf("Deleted directory: %s\n", path_str.c_str());
    } else {
        if (SD.remove(path)) Serial.printf("Deleted file: %s\n", path_str.c_str());
        else                 Serial.printf("Failed to delete: %s\n", path_str.c_str());
    }
    SD_UNLOCK();

    read_directory(current_path.c_str());
    schedule_file_list_update(1);            // 不能在这里直接销毁按钮

    char msg[128];
    const char* base = strrchr(path, '/');
    snprintf(msg, sizeof(msg), "Deleted: %s", base ? base + 1 : path);
    ui_set_status(msg, UIK_C_ERR_TEXT);
}

void copy_file(const char* src_path) {
    copy_source_path = String(src_path);
    has_copied = true;
    Serial.printf("Copied: %s\n", src_path);
    char msg[128];
    const char* base = strrchr(src_path, '/');
    snprintf(msg, sizeof(msg), "Copied: %s", base ? base + 1 : src_path);
    ui_set_status(msg, UIK_C_WARN_TEXT);
}

void paste_file(const char* dest_dir) {
    if (!has_copied) {
        ui_set_status("Nothing to paste. Copy a file first.", UIK_C_ERR_TEXT);
        return;
    }
    String filename  = copy_source_path.substring(copy_source_path.lastIndexOf('/') + 1);
    String dest_path = (String(dest_dir) == "/") ? ("/" + filename)
                                                 : (String(dest_dir) + "/" + filename);
    if (dest_path == copy_source_path) {
        ui_set_status("Source and destination are the same", UIK_C_ERR_TEXT);
        has_copied = false;
        return;
    }

    SD_LOCK();
    bool exists = SD.exists(dest_path);
    File src = exists ? File() : SD.open(copy_source_path);
    File dest;
    if (!exists && src) dest = SD.open(dest_path, FILE_WRITE);
    if (!exists && src && dest) {
        uint8_t buffer[512];
        size_t total = 0;
        while (src.available()) {
            size_t bytes = src.read(buffer, sizeof(buffer));
            if (bytes == 0) break;
            dest.write(buffer, bytes);
            total += bytes;
        }
        Serial.printf("Copied '%s' -> '%s' (%u bytes)\n",
                      copy_source_path.c_str(), dest_path.c_str(), (unsigned)total);
        char msg[128];
        snprintf(msg, sizeof(msg), "Pasted: %s (%u bytes)", filename.c_str(), (unsigned)total);
        ui_set_status(msg, UIK_C_OK_TEXT);
    } else if (exists) {
        ui_set_status("File exists! Copy cancelled.", UIK_C_ERR_TEXT);
    } else {
        ui_set_status("Copy failed (open source/dest)", UIK_C_ERR_TEXT);
    }
    if (src)  src.close();
    if (dest) dest.close();
    SD_UNLOCK();

    has_copied = false;
    copy_source_path = "";
    read_directory(current_path.c_str());
    schedule_file_list_update(1);
}

// TXT 串口发送：分片推进，避免在按键回调里阻塞
#define TX_MAX_BYTES   (256 * 1024)
#define TX_CHUNK_BYTES 128      // 每轮最多 128B（115200 下约 11ms），保证 LVGL 仍有 ~30fps
// cat 走同一条发送链路，但上限放宽到 1MB：很多日志/JSON 超过 256KB，
// 内容整段落在 PSRAM（8MB），不会挤占 WiFi/BLE 的内部堆。
#define CAT_MAX_BYTES  (1024 * 1024)

static String tx_tag = "TXT";   // 传输横幅里的标签：TXT（文件屏）/ CAT（命令行）

// 通用文件->串口发送。tag 决定横幅文字，max_bytes 是这次允许的大小上限。
// 失败时把原因写进 *err（可为 NULL），返回 false。
static bool send_file_via_serial(const char* path, const char* tag,
                                 size_t max_bytes, String* err) {
    if (tx_buffer != nullptr) {
        if (err) *err = "A transfer is already running";
        return false;
    }

    SD_LOCK();
    File file = SD.open(path);
    if (!file) {
        SD_UNLOCK();
        if (err) *err = "Cannot open file!";
        return false;
    }
    if (file.isDirectory()) {
        file.close();
        SD_UNLOCK();
        if (err) *err = "That is a directory";
        return false;
    }
    size_t fsize = file.size();
    if (fsize == 0) {
        file.close();
        SD_UNLOCK();
        if (err) *err = "Empty file";
        return false;
    }
    if (fsize > max_bytes) {
        file.close();
        SD_UNLOCK();
        if (err) *err = "File too big for this command";
        return false;
    }
    char* buffer = (char*)heap_caps_malloc(fsize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) buffer = (char*)malloc(fsize + 1);
    if (buffer == nullptr) {
        file.close();
        SD_UNLOCK();
        if (err) *err = "Out of memory for buffer";
        return false;
    }
    size_t got = file.read((uint8_t*)buffer, fsize);
    file.close();
    SD_UNLOCK();

    buffer[got] = '\0';
    tx_buffer = buffer;
    tx_len    = got;
    tx_sent   = 0;
    tx_file   = String(path);
    tx_tag    = String(tag);

    Serial.printf("\n========================================\n");
    Serial.printf("%s: %s\n", tag, path);
    Serial.printf("----------------------------------------\n");
    Serial.printf("%u bytes\n", (unsigned)got);
    Serial.println("----------------------------------------");
    return true;
}

void send_txt_via_serial(const char* path) {
    String err;
    if (!send_file_via_serial(path, "SENDING TXT FILE VIA SERIAL", TX_MAX_BYTES, &err)) {
        ui_set_status(err.c_str(), UIK_C_ERR_TEXT);
        return;
    }
    ui_set_status("Sending over serial...", UIK_C_WARN_TEXT);
}

void pump_txt_transfer() {
    if (tx_buffer == nullptr) return;

    size_t remaining = tx_len - tx_sent;
    size_t chunk = (remaining > TX_CHUNK_BYTES) ? TX_CHUNK_BYTES : remaining;
    Serial.write((const uint8_t*)(tx_buffer + tx_sent), chunk);
    tx_sent += chunk;

    static size_t last_report = 0;
    if (tx_sent - last_report >= 8192) {
        last_report = tx_sent;
        char msg[64];
        snprintf(msg, sizeof(msg), "Sending %u/%u", (unsigned)tx_sent, (unsigned)tx_len);
        ui_set_status(msg, UIK_C_WARN_TEXT);
    }

    if (tx_sent >= tx_len) {
        Serial.println();
        Serial.println("----------------------------------------");
        Serial.printf("%s done: %s (%u bytes)\n", tx_tag.c_str(),
                      tx_file.c_str(), (unsigned)tx_len);
        Serial.println("========================================\n");
        char msg[96];
        snprintf(msg, sizeof(msg), "Sent: %s (%u bytes)",
                 tx_file.substring(tx_file.lastIndexOf('/') + 1).c_str(), (unsigned)tx_len);
        ui_set_status(msg, UIK_C_OK_TEXT);
        free(tx_buffer);
        tx_buffer  = nullptr;
        tx_len     = 0;
        tx_sent    = 0;
        last_report = 0;
        tx_file    = "";
    }
}

// DHT11 记录到 SD
static bool init_dht11_log_file() {
    SD_LOCK();
    bool ok = true;
    if (!SD.exists("/dht11.json")) {
        File file = SD.open("/dht11.json", FILE_WRITE);
        if (!file) ok = false;
        else {
            file.println("[");
            file.close();
            Serial.println("Created dht11.json");
        }
    }
    SD_UNLOCK();
    if (!ok) Serial.println("Failed to create dht11.json");
    return ok;
}

void write_dht11_data_to_sd() {
    if (!dht11_logging) return;
    unsigned long now = millis();
    if (now - last_log_time < LOG_INTERVAL) return;
    last_log_time = now;

    float t, h;
    if (!dht_read_cached(&t, &h, false)) {
        Serial.println("DHT11 read failed, cannot log!");
        return;
    }

    SD_LOCK();
    File file = SD.open("/dht11.json", FILE_APPEND);
    if (!file) {
        SD_UNLOCK();
        Serial.println("Failed to open dht11.json");
        return;
    }
    unsigned long timestamp = now / 1000;
    file.printf("  {\"t\":%lu,\"temp\":%.1f,\"humi\":%.1f},\n", timestamp, t, h);
    file.close();
    SD_UNLOCK();
    Serial.printf("Logged: %.1fC, %.1f%%\n", t, h);
}

void stop_dht11_logging() {
    if (!dht11_logging) return;
    dht11_logging = false;

    SD_LOCK();
    File file = SD.open("/dht11.json", FILE_READ);
    if (!file) {
        SD_UNLOCK();
        Serial.println("Failed to open dht11.json");
        return;
    }
    String content;
    content.reserve(file.size() + 4);
    while (file.available()) content += (char)file.read();
    file.close();

    if (content.endsWith(",\n")) {
        content = content.substring(0, content.length() - 2);
        content += "\n";
    } else if (content.endsWith(",")) {
        content = content.substring(0, content.length() - 1);
    }
    content += "]\n";

    file = SD.open("/dht11.json", FILE_WRITE);
    if (file) {
        file.print(content);
        file.close();
    }
    SD_UNLOCK();
    Serial.println("DHT11 logging stopped. Data saved to /dht11.json");
}

void start_dht11_logging() {
    if (dht11_logging) {
        Serial.println("Already logging");
        return;
    }
    if (!sd_initialized) {
        Serial.println("SD card not ready");
        return;
    }
    if (!init_dht11_log_file()) {
        Serial.println("Failed to init log file");
        return;
    }
    dht11_logging = true;
    last_log_time = 0;
    Serial.println("DHT11 logging started. Recording every 3 seconds to /dht11.json");
}

// WiFi：配置持久化 + 异步扫描
void wifi_save_config_to_sd(const char* ssid, const char* password) {
    if (!sd_initialized) {
        Serial.println("wifi.cfg not saved (no SD)");
        return;
    }
    SD_LOCK();
    File f = SD.open("/wifi.cfg", FILE_WRITE);
    bool ok = (bool)f;
    if (ok) {
        f.println(ssid);
        f.println(password);
        f.close();
    }
    SD_UNLOCK();
    Serial.println(ok ? "WiFi config saved to /wifi.cfg" : "Failed to save wifi.cfg");
}

bool wifi_load_config_from_sd(char* ssid, char* password, size_t ssid_size, size_t pwd_size) {
    if (!sd_initialized) return false;

    SD_LOCK();
    bool exists = SD.exists("/wifi.cfg");
    File f;
    if (exists) f = SD.open("/wifi.cfg", FILE_READ);
    if (!exists || !f) {
        SD_UNLOCK();
        Serial.println("wifi.cfg not found");
        return false;
    }
    String line1 = f.readStringUntil('\n');
    String line2 = f.readStringUntil('\n');
    f.close();
    SD_UNLOCK();

    line1.trim();
    line2.trim();
    if (line1.length() == 0) return false;

    memset(ssid, 0, ssid_size);
    memset(password, 0, pwd_size);
    strncpy(ssid, line1.c_str(), ssid_size - 1);
    strncpy(password, line2.c_str(), pwd_size - 1);
    return true;
}

void wifi_free_list_user_data() {
    if (ui_wifi_list == NULL) return;
    uint32_t n = lv_obj_get_child_cnt(ui_wifi_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* child = lv_obj_get_child(ui_wifi_list, i);
        void* data = lv_obj_get_user_data(child);
        if (data) {
            free(data);                      // user_data 是 malloc 的，必须释放
            lv_obj_set_user_data(child, NULL);
        }
    }
}

void wifi_scan_request() {
    if (wifi_scan_state != WSCAN_IDLE) {
        ui_set_status_ex("Scan already running...", UIK_C_WARN_TEXT);
        return;
    }
    if (rfmon_is_active()) {
        // RF Monitor 在跳频采集，此时 scanNetworks 会抢射频、两边数据都废。
        // WiFi_Task 那边也会拒，但在这里先说清楚原因，免得用户只看到转圈超时。
        ui_set_status_ex("Stop RF Monitor first", UIK_C_ERR_TEXT);
        return;
    }
    if (!wifiScanRequestQueue || !wifiScanResultQueue) {
        ui_set_status_ex("WiFi queues not ready", UIK_C_ERR_TEXT);
        return;
    }

    // 先排空结果队列，避免把上一次的扫描结果当成这次的
    ScanResultMsg_t stale;
    while (xQueueReceive(wifiScanResultQueue, &stale, 0) == pdTRUE) { }
    ScanRequestMsg_t old;
    while (xQueueReceive(wifiScanRequestQueue, &old, 0) == pdTRUE) { }

    ScanRequestMsg_t req = { true };
    if (xQueueSend(wifiScanRequestQueue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        ui_set_status_ex("Scan request failed", UIK_C_ERR_TEXT);
        return;
    }
    wifi_scan_state = WSCAN_WAIT_RESULT;
    wifi_scan_start = millis();
    ui_set_status_ex("Scanning...", UIK_C_WARN_TEXT);   // 只发请求，立刻返回
}

void wifi_scan_poll() {
    if (wifi_scan_state != WSCAN_WAIT_RESULT) return;

    if (millis() - wifi_scan_start > WIFI_SCAN_TIMEOUT_MS) {
        wifi_scan_state = WSCAN_IDLE;
        ui_set_status_ex("Scan timeout", UIK_C_ERR_TEXT);
        return;
    }

    ScanResultMsg_t result;
    if (xQueueReceive(wifiScanResultQueue, &result, 0) != pdTRUE) return;   // 结果还没到
    wifi_scan_state = WSCAN_IDLE;

    if (result.count <= 0) {
        ui_set_status_ex("No networks found", UIK_C_ERR_TEXT);
        return;
    }

    if (ui_wifi_list) {
        wifi_free_list_user_data();
        lv_obj_clean(ui_wifi_list);
        for (int i = 0; i < result.count; i++) {
            if (result.ssids[i][0] == '\0') continue;
            lv_obj_t* btn = lv_list_add_btn(ui_wifi_list, LV_SYMBOL_WIFI, result.ssids[i]);
            uik_list_row(btn);       // 紧凑行高 + 统一按下反馈
            char* ssid_copy = (char*)malloc(strlen(result.ssids[i]) + 1);
            if (ssid_copy) strcpy(ssid_copy, result.ssids[i]);
            lv_obj_set_user_data(btn, ssid_copy);
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                lv_obj_t* b = lv_event_get_target(e);
                const char* ssid = (const char*)lv_obj_get_user_data(b);
                if (ssid == NULL) return;
                selected_ssid = String(ssid);
                // 选中的网络高亮成主色 —— 否则用户点完不知道该看哪一行，
                // 只能从底部状态栏那串 SSID 去比对。
                lv_obj_t* list = lv_obj_get_parent(b);
                uint32_t n = lv_obj_get_child_cnt(list);
                for (uint32_t k = 0; k < n; k++) {
                    lv_obj_t* row = lv_obj_get_child(list, k);
                    lv_obj_set_style_bg_color(row,
                        lv_color_hex(row == b ? UIK_C_PRIMARY : UIK_C_SURFACE), LV_STATE_DEFAULT);
                }
                ui_set_status_ex(ssid, UIK_C_TEXT);
            }, LV_EVENT_CLICKED, NULL);
        }
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "Found %d networks. Select one.", result.count);
    ui_set_status_ex(msg, UIK_C_OK_TEXT);
}

void wifi_connect_to_selected() {
    if (selected_ssid.length() == 0) {
        ui_set_status_ex("No network selected", UIK_C_ERR_TEXT);
        return;
    }
    const char* password = ui_wifi_password_ta ? lv_textarea_get_text(ui_wifi_password_ta) : "";
    if (password == NULL || strlen(password) == 0) {
        ui_set_status_ex("Password cannot be empty", UIK_C_ERR_TEXT);
        return;
    }

    WifiConfigMsg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, selected_ssid.c_str(), sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, password, sizeof(cfg.password) - 1);

    if (xQueueSend(wifiConfigQueue, &cfg, pdMS_TO_TICKS(500)) != pdTRUE) {
        ui_set_status_ex("Failed to send config", UIK_C_ERR_TEXT);
        return;
    }
    wifi_save_config_to_sd(cfg.ssid, cfg.password);
    ui_set_status_ex("Config saved, connecting in background...", UIK_C_OK_TEXT);
    if (ui_wifi_password_ta) lv_textarea_set_text(ui_wifi_password_ta, "");
}

// ui_wifi 的标签包装（ui_wifi_status_label 可能为 NULL）
void ui_set_status_ex(const char* text, uint32_t color) {
    if (ui_wifi_status_label == NULL) return;
    lv_label_set_text(ui_wifi_status_label, text);
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
}

// ============================================================================
//  WiFi 信道扫描（2.4GHz 信道占用分析，屏体在 ui_test/screens/ui_wscan.c）
//  ----------------------------------------------------------------------------
//  和上面的"网络列表扫描"完全独立：独立队列、独立状态机、独立结果缓存。
//  分工也和其它屏一致 ——
//      ui_wscan.c      只画：柱子、信道号、详情列表（不认识 WiFi.h / 队列）
//      LVGL_Demos.cpp  只翻译：队列里的 ChanScanResult_t -> 控件
//  数据流：点 Scan -> wscan_request() -> wifiChanScanReqQueue -> WiFi_Task(core1)
//          -> scanNetworks(全信道, show_hidden=true) -> wifiChanScanResQueue
//          -> wscan_poll()(core0) -> 缓存进 wscan_last -> 刷柱状图
//  点柱子不再重扫：wscan_last 里已经有全量 AP，直接按信道过滤出详情，
//          否则每点一根柱子等 4 秒，工具就没法用了。
// ============================================================================

// wifi_auth_mode_t（esp_wifi_types.h）压成详情行里放得下的短名。
// 用数字而不是枚举名：这个枚举要经 WiFi.h -> WiFiGeneric.h 才间接可见，
// 数字映射不受包含顺序影响，值本身是 SDK 头里固定的。
static const char* wscan_auth_short(uint8_t enc) {
    switch (enc) {
        case 0:  return "OPEN";
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA/WPA2";
        case 5:  return "ENT";
        case 6:  return "WPA3";
        case 7:  return "WPA2/3";
        case 8:  return "WAPI";
        case 9:  return "WPA3-192";
        default: return "?";
    }
}

// 2.4G 总 AP 数 = 各信道计数之和（ap_per_ch 是全量的，不受 MAX_AP 截断影响）
static int wscan_total_ap() {
    int t = 0;
    for (int c = WCH_MIN; c <= WCH_MAX; c++) t += wscan_last.ap_per_ch[c];
    return t;
}

// 详情行：SSID 靠左（占满剩余宽度），"RSSI + 加密" 靠右。
// 不放 WiFi 图标：行内可用宽只有 304，图标(16) + 间隙(6) 会把 SSID 再挤掉 22px，
// 而 12pt 下一个字符约 6.5px —— 少 22px 就是少三个半字符，不划算。
// ★ 这些行不带 user_data（数据直接读静态的 wscan_last），所以重建列表时
//   不需要像 WiFi 网络列表那样先 free 一遍 user_data。
static void wscan_add_detail_row(const ChanScanAp_t* ap) {
    if (ui_wscan_detail == NULL) return;

    lv_obj_t* row = lv_list_add_btn(ui_wscan_detail, NULL,
                                    ap->ssid[0] ? ap->ssid : "<hidden>");
    // 先把右侧的元信息标签建出来，再统一过 uik_list_row()：
    // 那个函数会遍历行内所有子对象刷字体/颜色，顺序反了就会漏掉这个标签。
    // 定宽 + 右对齐 + 更小字号：SSID 那个标签 flex_grow 占掉剩下的 186px，
    // 元信息永远贴在行尾不会被长 SSID 顶出去。
    char meta[24];
    snprintf(meta, sizeof(meta), "%d dBm %s", (int)ap->rssi, wscan_auth_short(ap->enc));
    lv_obj_t* m = lv_label_create(row);
    lv_label_set_text(m, meta);
    lv_obj_set_width(m, 112);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
    uik_list_row(row);   // 会把两个标签都设成 BODY，下面再单独把元信息降一档
    lv_obj_set_style_text_font(m, UIK_FONT_HINT, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(m, lv_color_hex(UIK_C_TEXT_DIM), LV_STATE_DEFAULT);
    // 详情行只是"读"的，点了没有动作：去掉 CLICKABLE，
    // 否则会有按下高亮，暗示它可点（滚动不受影响，拖动落到父 list 上）。
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
}

// 把 wscan_last 里属于信道 ch 的 AP 填进详情列表
static void wscan_render_detail(uint8_t ch) {
    if (ui_wscan_detail == NULL) return;
    if (ch < WCH_MIN || ch > WCH_MAX) return;

    lv_obj_clean(ui_wscan_detail);
    lv_obj_scroll_to_y(ui_wscan_detail, 0, LV_ANIM_OFF);   // 换信道后要回到顶，否则停在上一屏的位置

    int shown = 0;
    for (uint16_t i = 0; i < wscan_last.count; i++) {
        if (wscan_last.aps[i].channel != ch) continue;
        wscan_add_detail_row(&wscan_last.aps[i]);
        shown++;
    }

    int exact = wscan_last.ap_per_ch[ch];
    char hdr[72];
    if (exact == 0) {
        snprintf(hdr, sizeof(hdr), "CH %u " LV_SYMBOL_BULLET " no AP heard", (unsigned)ch);
    } else {
        // aps[] 只保留最强的 APP_CSCAN_MAX_AP 个，所以某个信道可能显示不全，
        // 这时必须说清楚"这是最强的 N 个"，否则总数对不上会被当成 bug。
        snprintf(hdr, sizeof(hdr),
                 "CH %u " LV_SYMBOL_BULLET " %d AP " LV_SYMBOL_BULLET " best %d dBm%s",
                 (unsigned)ch, exact, (int)wscan_last.best_rssi[ch],
                 (shown < exact) ? " (strongest)" : "");
    }
    ui_wscan_set_detail_hdr(hdr);

    if (shown == 0) {
        // 空信道也要在列表里留一行说明，否则那块区域是纯底色，
        // 用户分不清"这个信道没 AP"和"还没扫"。
        lv_obj_t* row = lv_list_add_btn(ui_wscan_detail, NULL,
                                        (exact == 0) ? "This channel is clean"
                                                     : "Details not kept (too many APs)");
        uik_list_row(row);
        lv_obj_t* lbl = lv_obj_get_child(row, 0);
        if (lbl != NULL)
            lv_obj_set_style_text_color(lbl, lv_color_hex(UIK_C_TEXT_FAINT), LV_STATE_DEFAULT);
    }
}

// 汇总行 + 柱状图一起刷（数据来自 wscan_last）
static void wscan_render_bars() {
    uint8_t per_ch[WSC_CH_NUM];
    for (int i = 0; i < WSC_CH_NUM; i++) per_ch[i] = wscan_last.ap_per_ch[i + 1];
    ui_wscan_set_bars(per_ch, wscan_last.best_ch);

    // 推荐信道有两个：best_ch 考虑了邻道干扰（±2 信道之和），
    // best_1611 只在互不重叠的 1/6/11 里挑。家用路由器只能设 1/6/11，
    // 所以两个都给出来，让用户按自己的设备能力选。
    char sum[80];
    snprintf(sum, sizeof(sum),
             "%d AP " LV_SYMBOL_BULLET " best CH %u " LV_SYMBOL_BULLET " 1/6/11: CH %u",
             wscan_total_ap(), (unsigned)wscan_last.best_ch, (unsigned)wscan_last.best_1611);
    ui_wscan_set_summary(sum);
}

// 请求一次全信道扫描（不阻塞：只投队列就返回）
void wscan_request() {
    if (wscan_state != WSCAN_IDLE) {
        ui_wscan_set_status("Scan already running...", UIK_C_WARN_TEXT);
        return;
    }
    if (rfmon_is_active()) {
        // 同上：跳频监听期间不能扫信道
        ui_wscan_set_status("Stop RF Monitor first", UIK_C_ERR_TEXT);
        return;
    }
    if (!wifiChanScanReqQueue || !wifiChanScanResQueue) {
        ui_wscan_set_status("WiFi queues not ready", UIK_C_ERR_TEXT);
        return;
    }

    // 排空两个队列，避免把上一次的结果当成这次的（和网络列表扫描同样的做法）。
    // 用 xQueueReset 而不是 xQueueReceive 循环：ChanScanResult_t 有 1202B，
    // 循环需要一个同大小的栈缓冲，纯为"丢掉旧值"付这个栈钱不值。
    xQueueReset(wifiChanScanResQueue);
    xQueueReset(wifiChanScanReqQueue);

    ChanScanReq_t req = { true };
    if (xQueueSend(wifiChanScanReqQueue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        ui_wscan_set_status("Scan request failed", UIK_C_ERR_TEXT);
        return;
    }
    wscan_state = WSCAN_WAIT_RESULT;
    wscan_start = millis();
    ui_wscan_set_scanning(true);
    ui_wscan_set_summary("Scanning 13 channels...");
    ui_wscan_set_status("Surveying 2.4 GHz, about 4 s", UIK_C_WARN_TEXT);
}

void wscan_poll() {
    if (wscan_state != WSCAN_WAIT_RESULT) return;

    if (millis() - wscan_start > WSCAN_TIMEOUT_MS) {
        wscan_state = WSCAN_IDLE;
        ui_wscan_set_scanning(false);
        ui_wscan_set_status("Scan timeout", UIK_C_ERR_TEXT);
        return;
    }

    // ★ 先收进栈上的临时副本，只有成功才覆盖 wscan_last：
    //   扫描失败时 WiFi_Task 送回来的是一份全零结果，直接收进 wscan_last
    //   会把上一次的好数据抹掉 —— 柱子还画着旧值（失败分支提前 return 没重画），
    //   详情却已经变空，两边对不上。1202B 的栈帧和 wifi_scan_poll() 里那个
    //   1654B 的 ScanResultMsg_t 同量级，LVGL_Task 栈 16010 够用，
    //   而且这两个 poll 是从 original_loop() 顺序调用的，帧不会同时存在。
    ChanScanResult_t rx;
    if (xQueueReceive(wifiChanScanResQueue, &rx, 0) != pdTRUE) return;   // 还没到
    wscan_state = WSCAN_IDLE;
    ui_wscan_set_scanning(false);

    if (rx.scan_ret < 0) {
        // scanNetworks 返回 WIFI_SCAN_FAILED(-1) / WIFI_SCAN_RUNNING(-2)：
        // 和"扫到了 0 个网络"是两件事，必须分开说，否则用户会以为周围真没 WiFi。
        char msg[48];
        snprintf(msg, sizeof(msg), "Scan failed (%d)", (int)rx.scan_ret);
        ui_wscan_set_status(msg, UIK_C_ERR_TEXT);
        return;   // 保留 wscan_last 与画面不动
    }
    memcpy(&wscan_last, &rx, sizeof(wscan_last));
    wscan_have_data = true;

    unsigned long ms = millis() - wscan_start;
    char msg[72];
    snprintf(msg, sizeof(msg), "%d AP in %lu.%01lu s " LV_SYMBOL_BULLET " tap a bar",
             wscan_total_ap(), ms / 1000UL, (ms % 1000UL) / 100UL);
    ui_wscan_set_status(msg, UIK_C_OK_TEXT);

    wscan_render_bars();
    // 保持当前选中：重扫之后柱子会重排，但用户看的那个信道不该被悄悄换掉。
    // 首次扫描（还没选过）才落到推荐信道上。
    uint8_t sel = ui_wscan_selected_channel();
    if (sel == 0) sel = wscan_last.best_ch;
    ui_wscan_select_channel(sel);
    wscan_render_detail(sel);
}

// 串口 /chan：把占用图打到串口。没有屏幕也能用，方便对着真机核对聚合逻辑。
static void wscan_serial_dump() {
    if (!wscan_have_data) {
        Serial.println("[wscan] no data yet, send /chan scan first");
        return;
    }
    Serial.println("\n=== 2.4GHz channel occupancy ===");
    Serial.println(" CH  APs  load(+-2)  best RSSI");
    for (int c = WCH_MIN; c <= WCH_MAX; c++) {
        Serial.printf(" %2d  %3u  %9u  %4d%s\n",
                      c, (unsigned)wscan_last.ap_per_ch[c],
                      (unsigned)wscan_last.load[c],
                      (int)wscan_last.best_rssi[c],
                      (c == wscan_last.best_ch) ? "  <-- recommended" : "");
    }
    Serial.printf(" total %d AP, pick CH %u (1/6/11: CH %u)\n",
                  wscan_total_ap(), (unsigned)wscan_last.best_ch,
                  (unsigned)wscan_last.best_1611);
    for (uint16_t i = 0; i < wscan_last.count; i++) {
        const ChanScanAp_t& a = wscan_last.aps[i];
        Serial.printf("   ch%-2u %4d dBm %-9s %s\n",
                      (unsigned)a.channel, (int)a.rssi,
                      wscan_auth_short(a.enc), a.ssid[0] ? a.ssid : "<hidden>");
    }
    Serial.println("================================\n");
}

// ---- ui_wscan.c 的回调（那边按 C 编译，必须是 C 链接）----------------------
extern "C" {

void ui_wscan_on_scan_requested(void) {
    wscan_request();
}

void ui_wscan_on_channel_selected(uint8_t channel) {
    ui_wscan_select_channel(channel);
    if (!wscan_have_data) {
        ui_wscan_set_detail_hdr("CH ?" LV_SYMBOL_BULLET "no scan yet");
        ui_wscan_set_status("Tap Scan first", UIK_C_WARN_TEXT);
        return;
    }
    wscan_render_detail(channel);
    char msg[56];
    snprintf(msg, sizeof(msg), "CH %u" LV_SYMBOL_BULLET "%u AP" LV_SYMBOL_BULLET "load %u",
             (unsigned)channel, (unsigned)wscan_last.ap_per_ch[channel],
             (unsigned)wscan_last.load[channel]);
    ui_wscan_set_status(msg, (wscan_last.ap_per_ch[channel] == 0) ? UIK_C_OK_TEXT : UIK_C_TEXT);
}

void ui_wscan_on_screen_ready(void) {
    if (!wscan_have_data) return;
    wscan_render_bars();
    uint8_t sel = ui_wscan_selected_channel();
    if (sel == 0) sel = wscan_last.best_ch;
    ui_wscan_select_channel(sel);
    wscan_render_detail(sel);
    ui_wscan_set_status("Cached survey" LV_SYMBOL_BULLET "tap Scan to refresh", UIK_C_TEXT_DIM);
}

} /* extern "C" */


// ============================================================================
//  继电器控制（GPIO41，屏体在 ui_test/screens/ui_relay.c）
//  ----------------------------------------------------------------------------
//  ★ 状态缓存 relay_hw_on 是"软件视角的真相"：GPIO41 是纯输出，
//    digitalRead 读回的是引脚电平不是继电器是否真的吸合，
//    所以维护一个自己的 bool，写引脚和更新缓存必须在同一个函数里完成，
//    界面同步（ui_relay_set_state）只信这个缓存。
//  ★ 上电默认 OFF：继电器控制的是实体设备，复位/重启后不应该
//    "恢复到上次状态"——没人知道上次是什么、设备现在受不受得了。
//    要记忆状态的话应该显式存 SD，目前不做。
// ============================================================================
static bool relay_hw_on = false;

static void relay_hw_init() {
    pinMode(RELAY_PIN, OUTPUT);
    // 先写电平再切 OUTPUT 也行，但 ESP32 的 pinMode(OUTPUT) 不保证初始电平，
    // 所以紧跟一次 digitalWrite 把状态钉死在 OFF。
    digitalWrite(RELAY_PIN, (RELAY_ACTIVE_LEVEL == HIGH) ? LOW : HIGH);
    relay_hw_on = false;
}

static void relay_set(bool on) {
    digitalWrite(RELAY_PIN, on ? RELAY_ACTIVE_LEVEL : ((RELAY_ACTIVE_LEVEL == HIGH) ? LOW : HIGH));
    relay_hw_on = on;
    Serial.printf("[relay] GPIO%d -> %s\n", RELAY_PIN, on ? "ON" : "OFF");
    ui_relay_set_state(on);   // 屏没建时内部判 NULL，安全
}

extern "C" {

void ui_relay_on_toggle_requested(bool on) {
    relay_set(on);
}

bool ui_relay_get_hw_state(void) {
    return relay_hw_on;
}

} /* extern "C" */


 #define WS2812_PIN        48    //这是板载ws2812
//#define _PIN        39
#define WS2812_RMT_CH     RMT_CHANNEL_1      // 通道 0 留空，避免和潜在外设撞车
#define WS2812_MAX_LEDS   UI_LED_COUNT_MAX   // 120
#define WS2812_CLK_DIV    2                  // 80MHz/2 -> 25ns/tick
#define WS2812_T0H_TICKS  (400 / 25)         // 16
#define WS2812_T0L_TICKS  (850 / 25)         // 34
#define WS2812_T1H_TICKS  (800 / 25)         // 32
#define WS2812_T1L_TICKS  (450 / 25)         // 18

static uint8_t ws_buf[WS2812_MAX_LEDS * 3];  // GRB 字节序，360B

struct LedState {
    int16_t hue;        // 0..359
    uint8_t bright;     // 0..255
    uint8_t speed;      // 0..100，0=静止
    uint8_t count;      // 1..120
    float   phase;      // 彩虹相位（度）
    bool    dirty;      // 下个 tick 需要写灯
    bool    rmt_ok;
};
static LedState led = { 200, 10, 40, 1, 0.0f, true, false };

// 样本字节 -> RMT 波形。RMT 中断里调用，必须 IRAM_ATTR。
static void IRAM_ATTR ws2812_rmt_adapter(const void* src, rmt_item32_t* dest,
                                         size_t src_size, size_t wanted_num,
                                         size_t* translated_size, size_t* item_num)
{
    if (src == NULL || dest == NULL) { *translated_size = 0; *item_num = 0; return; }
    const rmt_item32_t bit0 = {{{ WS2812_T0H_TICKS, 1, WS2812_T0L_TICKS, 0 }}};
    const rmt_item32_t bit1 = {{{ WS2812_T1H_TICKS, 1, WS2812_T1L_TICKS, 0 }}};
    size_t size = 0, num = 0;
    const uint8_t* psrc = (const uint8_t*)src;
    rmt_item32_t* pdest = dest;
    while (size < src_size && num < wanted_num) {
        uint8_t b = *psrc++;
        for (int i = 7; i >= 0; i--) {          // WS2812 每字节高位先行
            *pdest++ = (b & (1u << i)) ? bit1 : bit0;
            num++;
        }
        size++;
    }
    *translated_size = size;
    *item_num = num;
}

static void ws2812_hw_init() {
    periph_module_enable(PERIPH_RMT_MODULE);
    // 不用 RMT_DEFAULT_CONFIG_TX：那个宏是指定初始化器写法，
    // 本文件按 C++（gnu++11）编译，非 C++20 语境下属于 GNU 扩展，
    // 顺序稍有不匹配就编不过。逐字段赋值最稳。
    rmt_config_t cfg = {};
    cfg.rmt_mode = RMT_MODE_TX;
    cfg.channel = WS2812_RMT_CH;
    cfg.gpio_num = (gpio_num_t)WS2812_PIN;
    cfg.clk_div = WS2812_CLK_DIV;
    cfg.mem_block_num = 2;
    cfg.flags = 0;
    cfg.tx_config.loop_en = false;
    cfg.tx_config.carrier_en = false;
    cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    cfg.tx_config.idle_output_en = true;
    if (rmt_config(&cfg) != ESP_OK ||
        rmt_driver_install(cfg.channel, 0, 0) != ESP_OK) {
        led.rmt_ok = false;
        Serial.println("[led] RMT init FAILED, strip disabled");
        return;
    }
    rmt_translator_init(cfg.channel, ws2812_rmt_adapter);
    led.rmt_ok = true;
    Serial.printf("[led] WS2812 on GPIO%d, RMT ch%d, max %d LEDs\n",
                  WS2812_PIN, (int)WS2812_RMT_CH, WS2812_MAX_LEDS);
}

// HSV -> RGB，饱和度固定 255（见 ui_led.c 头注释：灰调用亮度压）
static void led_hsv2rgb(int h, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b) {
    h = ((h % 360) + 360) % 360;
    uint8_t region = (uint8_t)(h / 60);
    uint8_t rem = (uint8_t)((h % 60) * 255 / 60);
    uint8_t q = (uint8_t)(((uint16_t)v * (255 - rem)) / 255);
    uint8_t t = (uint8_t)(((uint16_t)v * rem) / 255);
    switch (region) {
        case 0:  *r = v; *g = t; *b = 0; break;
        case 1:  *r = q; *g = v; *b = 0; break;
        case 2:  *r = 0; *g = v; *b = t; break;
        case 3:  *r = 0; *g = q; *b = v; break;
        case 4:  *r = t; *g = 0; *b = v; break;
        default: *r = v; *g = 0; *b = q; break;
    }
}

static uint32_t led_current_rgb() {
    uint8_t r, g, b;
    led_hsv2rgb(led.hue, led.bright, &r, &g, &b);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void ws2812_push_frame() {
    if (!led.rmt_ok) return;
    // 一整条铺开一个完整彩虹；count=1 时 spacing=360，退化成"色相轮转的呼吸灯"
    float spacing = (led.count > 1) ? (360.0f / led.count) : 360.0f;
    for (int i = 0; i < led.count; i++) {
        int h = (led.speed == 0)
                    ? led.hue                                  // 静止纯色
                    : (int)(led.hue + led.phase + i * spacing); // 彩虹流动
        uint8_t r, g, b;
        led_hsv2rgb(h, led.bright, &r, &g, &b);
        ws_buf[i * 3 + 0] = g;    // WS2812 字节序：GRB
        ws_buf[i * 3 + 1] = r;
        ws_buf[i * 3 + 2] = b;
    }
    // wait=true：阻塞到发送完（最长 120*24*1.25us = 3.6ms），
    // ws_buf 是静态的，translator 在 TX_END 中断里分批取用也安全。
    rmt_write_sample(WS2812_RMT_CH, ws_buf, (size_t)led.count * 3, true);
}

// 30fps 动画 tick：彩虹模式推进相位；只有 dirty 才真正写灯
static void led_anim_timer_cb(lv_timer_t*) {
    if (led.speed > 0) {
        led.phase += led.speed * 0.12f;      // 满速 360°/s
        if (led.phase >= 360.0f) led.phase -= 360.0f;
        led.dirty = true;
    }
    if (!led.dirty) return;
    led.dirty = false;
    ws2812_push_frame();
}

// 把当前状态推到界面 + 状态条
static void led_sync_ui() {
    ui_led_set_state(led.hue, led.bright, led.speed, led.count, led_current_rgb());
    char st[72];
    snprintf(st, sizeof(st), "GPIO%d " LV_SYMBOL_BULLET " %u LED%s " LV_SYMBOL_BULLET " %s",
             WS2812_PIN, (unsigned)led.count, (led.count > 1) ? "s" : "",
             led.rmt_ok ? (led.speed ? "rainbow" : "static") : "RMT error");
    ui_led_set_status(st, led.rmt_ok ? UIK_C_TEXT_DIM : UIK_C_ERR_TEXT);
}

// 参数入口：夹紧范围 -> 置 dirty -> 同步界面。滑块和串口命令都走这里。
static void led_apply(int what, int v) {
    switch (what) {
        case UI_LED_HUE:
            led.hue = (int16_t)(v < 0 ? 0 : (v > UI_LED_HUE_MAX ? UI_LED_HUE_MAX : v));
            break;
        case UI_LED_BRIGHT:
            led.bright = (uint8_t)(v < 0 ? 0 : (v > UI_LED_BRIGHT_MAX ? UI_LED_BRIGHT_MAX : v));
            break;
        case UI_LED_SPEED:
            led.speed = (uint8_t)(v < 0 ? 0 : (v > UI_LED_SPEED_MAX ? UI_LED_SPEED_MAX : v));
            break;
        case UI_LED_COUNT:
            led.count = (uint8_t)(v < UI_LED_COUNT_MIN ? UI_LED_COUNT_MIN
                                                       : (v > UI_LED_COUNT_MAX ? UI_LED_COUNT_MAX : v));
            break;
        default:
            return;
    }
    led.dirty = true;
    led_sync_ui();
    Serial.printf("[led] hue=%d bright=%u speed=%u count=%u\n",
                  (int)led.hue, (unsigned)led.bright, (unsigned)led.speed, (unsigned)led.count);
}

extern "C" {

void ui_led_on_change(int32_t what, int32_t value) {
    led_apply((int)what, (int)value);
}

void ui_led_on_screen_ready(void) {
    led_sync_ui();
}

} /* extern "C" */


// ============================================================================
//  RF Monitor（802.11 管理帧被动监听，数据层在 src/rf_monitor.cpp）
//  ----------------------------------------------------------------------------
//  这里只把 rfmon_stats_t 快照翻译成 ui_rfmon_* 调用，不碰射频。
//  轮询节流：500ms 一次，且只在屏建好时刷新；所有 set_* 内部都是
//  "先比后写"，静止时零 SPI 流量（这条工程铁律见 dht11/wifi 屏）。
//
//  列表模式二选一：
//    · 抓到过 deauth/disassoc -> 显示"谁在踢设备"（源 MAC + 厂商 + 帧数），红标题
//    · 否则                  -> 显示被动收到的 AP 表（SSID/MAC + 信道 + 信标数）
//  deauth 模式优先：这一屏的主要用途就是排查"网络老掉线是不是被人打了"。
// ============================================================================
static unsigned long rfmon_last_poll = 0;
static bool          rfmon_logging = false;   // 日志开关，定义见下方 rfmon_log_tick 区块

// 按帧数降序排出 deauth 源，最多取 UI_RFMON_MAX_ROWS 个
static int rfmon_fill_deauth_entries(const rfmon_stats_t& st, ui_rfmon_entry_t* ent) {
    // 表很小（<=8），直接选择排序到 ent，避免改动快照
    bool used[RFMON_MAX_DEAUTH_SRC] = { false };
    int n = 0;
    while (n < UI_RFMON_MAX_ROWS) {
        int best = -1;
        for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++) {
            if (used[i] || st.deauth_srcs[i].frames == 0) continue;
            if (best < 0 || st.deauth_srcs[i].frames > st.deauth_srcs[best].frames) best = i;
        }
        if (best < 0) break;
        used[best] = true;
        char mac[18];
        rfmon_mac_str(st.deauth_srcs[best].bssid, mac);
        const char* ven = rfmon_oui_vendor(st.deauth_srcs[best].bssid);
        snprintf(ent[n].name, sizeof(ent[n].name), "%s%s%s", mac,
                 (ven[0] != '?') ? " " : "", (ven[0] != '?') ? ven : "");
        snprintf(ent[n].right, sizeof(ent[n].right), "x%u",
                 (unsigned)st.deauth_srcs[best].frames);
        n++;
    }
    return n;
}

// AP 表按信标数降序（活跃度高的排前面）
static int rfmon_fill_ap_entries(const rfmon_stats_t& st, ui_rfmon_entry_t* ent) {
    bool used[RFMON_MAX_AP] = { false };
    int n = 0;
    while (n < UI_RFMON_MAX_ROWS) {
        int best = -1;
        for (int i = 0; i < RFMON_MAX_AP; i++) {
            if (used[i] || st.aps[i].beacons == 0) continue;
            if (best < 0 || st.aps[i].beacons > st.aps[best].beacons) best = i;
        }
        if (best < 0) break;
        used[best] = true;
        const rfmon_ap_t& a = st.aps[best];
        char mac[18];
        rfmon_mac_str(a.bssid, mac);
        if (a.ssid[0] != '\0') {
            const char* ven = rfmon_oui_vendor(a.bssid);
            snprintf(ent[n].name, sizeof(ent[n].name), "%s%s%s", a.ssid,
                     (ven[0] != '?') ? " " : "", (ven[0] != '?') ? ven : "");
        } else {
            snprintf(ent[n].name, sizeof(ent[n].name), "<hidden> %s", mac);
        }
        snprintf(ent[n].right, sizeof(ent[n].right), "CH%d x%u",
                 (int)a.ch, (unsigned)a.beacons);
        n++;
    }
    return n;
}

static void rfmon_poll_ui() {
    if (ui_rfmon == NULL) return;                    // 屏没建，别浪费
    if (millis() - rfmon_last_poll < 500) return;    // 500ms 节流
    rfmon_last_poll = millis();

    rfmon_stats_t st;
    rfmon_get_stats(&st);

    ui_rfmon_set_running(st.active);
    ui_rfmon_set_counts(st.beacons, st.probes, st.deauths, st.disassoc);

    ui_rfmon_entry_t ent[UI_RFMON_MAX_ROWS];
    int n;
    bool deauth_mode = (st.deauths > 0 || st.disassoc > 0) && st.deauth_src_count > 0;
    if (deauth_mode) {
        n = rfmon_fill_deauth_entries(st, ent);
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "DEAUTH SOURCES " LV_SYMBOL_BULLET " %u",
                 (unsigned)(st.deauths + st.disassoc));
        ui_rfmon_set_header(hdr, UIK_C_ERR_TEXT);
    } else {
        n = rfmon_fill_ap_entries(st, ent);
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "BEACONS SEEN " LV_SYMBOL_BULLET " %u AP",
                 (unsigned)st.ap_count);
        ui_rfmon_set_header(hdr, UIK_C_TEXT_DIM);
    }
    ui_rfmon_set_list(st.version, deauth_mode, n, ent);

    char stat[96];
    if (st.active) {
        snprintf(stat, sizeof(stat), "Listening CH%d " LV_SYMBOL_BULLET
                 " WiFi paused " LV_SYMBOL_BULLET " log %s",
                 (int)st.channel, rfmon_logging ? "ON" : "off");
        ui_rfmon_set_status(stat, UIK_C_OK_TEXT);
    } else if (st.beacons + st.deauths + st.probes + st.disassoc > 0) {
        snprintf(stat, sizeof(stat), "Stopped " LV_SYMBOL_BULLET " %u frames " LV_SYMBOL_BULLET " /rfmon.csv",
                 (unsigned)(st.beacons + st.deauths + st.probes + st.disassoc));
        ui_rfmon_set_status(stat, UIK_C_TEXT_DIM);
    }
    // 从未启动过时保留 screen_init 里写好的那句提示，不覆盖
}

// 串口 /rfmon：不依赖屏幕也能采集/查看
static void rfmon_serial_dump() {
    rfmon_stats_t st;
    rfmon_get_stats(&st);
    Serial.printf("\n=== RF Monitor (%s) CH%d ===\n",
                  st.active ? "LIVE" : "stopped", (int)st.channel);
    Serial.printf("beacons=%u probes=%u deauth=%u disassoc=%u\n",
                  (unsigned)st.beacons, (unsigned)st.probes,
                  (unsigned)st.deauths, (unsigned)st.disassoc);
    if (st.deauth_src_count) {
        Serial.println("deauth sources:");
        for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++) {
            if (st.deauth_srcs[i].frames == 0) continue;
            char mac[18]; rfmon_mac_str(st.deauth_srcs[i].bssid, mac);
            Serial.printf("  %s %-10s x%u\n", mac,
                          rfmon_oui_vendor(st.deauth_srcs[i].bssid),
                          (unsigned)st.deauth_srcs[i].frames);
        }
    }
    if (st.ap_count) {
        Serial.println("APs seen:");
        for (int i = 0; i < RFMON_MAX_AP; i++) {
            if (st.aps[i].beacons == 0) continue;
            char mac[18]; rfmon_mac_str(st.aps[i].bssid, mac);
            Serial.printf("  CH%-2d %4ddBm %s %-9s %s x%u\n",
                          (int)st.aps[i].ch, (int)st.aps[i].rssi, mac,
                          rfmon_oui_vendor(st.aps[i].bssid),
                          st.aps[i].ssid[0] ? st.aps[i].ssid : "<hidden>",
                          (unsigned)st.aps[i].beacons);
        }
    }
    Serial.println("==========================\n");
}

// ---- RF Monitor 的 SD 日志存储 -------------------------------------------
//  监听时把"谁在发 deauth/disassoc"逐条落盘，外加每 10s 一条活动汇总，
//  这样事后能对着时间线复盘"网络几点掉线、被哪个 MAC 打的"。
//  文件 /rfmon.csv，追加模式，和 dht11.json 一样单文件累积。
//  ★ 默认跟随监听开关：一开始监听就自动记（前提是 SD 就绪），
//    串口 `rfmon log off` 可临时关掉，避免不想要时的 SD 写入。
//  ★ SD 写入走 SD_LOCK（和 TFT 共用 SPI 总线，靠这把锁串行化），
//    与混杂回调用的 s_mux 是两把不同的锁，互不干扰、无死锁风险。
//  （rfmon_logging 的声明在文件前部 rfmon_last_poll 旁边）
static unsigned long rfmon_last_log      = 0;
static unsigned long rfmon_last_summary  = 0;
static bool          rfmon_was_active    = false;
static uint16_t      rfmon_logged_frames[RFMON_MAX_DEAUTH_SRC] = { 0 };

static void rfmon_log_reset() {
    for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++) rfmon_logged_frames[i] = 0;
    rfmon_last_summary = 0;
}

static void rfmon_log_tick() {
    unsigned long now = millis();
    if (now - rfmon_last_log < 1000) return;   // 1s 节流，先判后取快照
    rfmon_last_log = now;

    rfmon_stats_t st;
    rfmon_get_stats(&st);

    // 会话边沿：从不活跃 -> 活跃时，重置增量计数并按需自动开启日志
    if (st.active && !rfmon_was_active) {
        rfmon_log_reset();
        if (sd_initialized) rfmon_logging = true;
    }
    rfmon_was_active = st.active;

    if (!rfmon_logging) return;

    SD_LOCK();
    bool isNew = !SD.exists("/rfmon.csv");
    File f = SD.open("/rfmon.csv", FILE_APPEND);
    if (!f) { SD_UNLOCK(); return; }
    if (isNew)
        f.println("ts_s,event,ch,mac,vendor,delta,ap,beacon,probe,deauth,disassoc");
    unsigned long ts = now / 1000;

    // 1) 每个源新增的 deauth/disassoc 帧（按 delta 记，避免重复）
    for (int i = 0; i < RFMON_MAX_DEAUTH_SRC; i++) {
        if (st.deauth_srcs[i].frames == 0) continue;
        if (st.deauth_srcs[i].frames > rfmon_logged_frames[i]) {
            uint16_t delta = (uint16_t)(st.deauth_srcs[i].frames - rfmon_logged_frames[i]);
            rfmon_logged_frames[i] = st.deauth_srcs[i].frames;
            char mac[18];
            rfmon_mac_str(st.deauth_srcs[i].bssid, mac);
            f.printf("%lu,DEAUTH,%d,%s,%s,%u,,,,,\n", ts, (int)st.channel, mac,
                     rfmon_oui_vendor(st.deauth_srcs[i].bssid), (unsigned)delta);
        }
    }
    // 2) 每 10s 一条汇总（时间序列：AP 数 + 四类帧累计）
    if (rfmon_last_summary == 0 || now - rfmon_last_summary >= 10000UL) {
        rfmon_last_summary = now;
        f.printf("%lu,SUMMARY,%d,,,,%u,%u,%u,%u,%u\n", ts, (int)st.channel,
                 (unsigned)st.ap_count, (unsigned)st.beacons, (unsigned)st.probes,
                 (unsigned)st.deauths, (unsigned)st.disassoc);
    }
    f.close();
    SD_UNLOCK();
}

extern "C" {

void ui_rfmon_on_toggle(void) {
    if (rfmon_is_active()) {
        rfmon_stop();
    } else {
        rfmon_start();
    }
    // 立刻刷一次，不等 500ms 节流，按钮态/状态条即时反映
    rfmon_last_poll = 0;
    rfmon_poll_ui();
}

void ui_rfmon_on_screen_ready(void) {
    rfmon_last_poll = 0;
    rfmon_poll_ui();
}

} /* extern "C" */


// ============================================================================
//  nRF24 嗅探（数据层在 src/nrf24.cpp，只收不发）
//  ----------------------------------------------------------------------------
//  同 rfmon 的规矩：500ms 节流轮询，屏没建时零开销；抓到的包逐条落盘
//  /nrf.csv（seq 增量判断，绝不重复写）。
//  握手失败时：屏显示 HW=NO、按钮置灰，串口给出引脚表 + 3.3V 提醒。
// ============================================================================
static unsigned long nrf_last_poll = 0;
static unsigned long nrf_last_log  = 0;
static uint32_t      nrf_logged_seq = 0;   // 已落盘的最大 seq

static const char* nrf_rate_name(uint8_t r) {
    static const char* t[3] = { "1M", "2M", "250K" };
    return t[r & 3];
}

static void nrf_poll_ui() {
    if (ui_nrf == NULL) return;
    if (millis() - nrf_last_poll < 500) return;
    nrf_last_poll = millis();

    nrf_stats_t st;
    nrf_get_stats(&st);

    ui_nrf_set_running(st.active, st.available);
    ui_nrf_set_stats(st.packets, st.channel, st.rate_idx, st.hopping, st.available);

    // 列表：ring [0] 最新，按顺序转行（"#123  00AA5F2E..."）
    ui_nrf_entry_t ent[UI_NRF_MAX_ROWS];
    int n = st.pkt_count < UI_NRF_MAX_ROWS ? st.pkt_count : UI_NRF_MAX_ROWS;
    for (int i = 0; i < n; i++) {
        const nrf_pkt_t& p = st.pkts[i];
        // hex 前缀最多 12 字节（24 字符），后面省略号；全量在 /nrf.csv
        char hex[32];
        int hl = p.len > 12 ? 12 : p.len;
        int pos = 0;
        for (int b = 0; b < hl && pos + 2 < (int)sizeof(hex); b++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X", p.data[b]);
        if (p.len > 12 && pos + 1 < (int)sizeof(hex)) { hex[pos++] = '.'; hex[pos] = '\0'; }
        snprintf(ent[i].name, sizeof(ent[i].name), "#%u %s", (unsigned)p.seq, hex);
        snprintf(ent[i].right, sizeof(ent[i].right), "CH%d %s", (int)p.ch, nrf_rate_name(p.rate));
    }
    ui_nrf_set_list(st.version, n, ent);

    char hdr[52];
    if (!st.available) {
        ui_nrf_set_header("NO MODULE " LV_SYMBOL_BULLET " check wiring / 3.3V", UIK_C_ERR_TEXT);
        ui_nrf_set_status("CE14 CSN15 SCK16 MISO17 MOSI18" LV_SYMBOL_BULLET "VCC=3.3V", UIK_C_WARN_TEXT);
    } else if (st.active) {
        snprintf(hdr, sizeof(hdr), "CAPTURED " LV_SYMBOL_BULLET " %u pkts",
                 (unsigned)st.packets);
        ui_nrf_set_header(hdr, st.packets > 0 ? UIK_C_OK_TEXT : UIK_C_TEXT_DIM);
        snprintf(hdr, sizeof(hdr), "Sniffing CH%d %s" LV_SYMBOL_BULLET " RX only",
                 (int)st.channel, st.hopping ? "hop" : "fix");
        ui_nrf_set_status(hdr, UIK_C_OK_TEXT);
    } else {
        snprintf(hdr, sizeof(hdr), "STOPPED " LV_SYMBOL_BULLET " %u pkts " LV_SYMBOL_BULLET " /nrf.csv",
                 (unsigned)st.packets);
        ui_nrf_set_header(hdr, UIK_C_TEXT_DIM);
        ui_nrf_set_status("Idle " LV_SYMBOL_BULLET " tap Scan to listen (passive)", UIK_C_TEXT_DIM);
    }
}

// SD 落盘：/nrf.csv，按 seq 增量（每包一行，含全量 hex）
static void nrf_log_tick() {
    unsigned long now = millis();
    if (now - nrf_last_log < 1000) return;
    nrf_last_log = now;
    if (!sd_initialized) return;

    nrf_stats_t st;
    nrf_get_stats(&st);
    if (!st.available || st.pkt_count == 0) return;
    if (st.pkts[0].seq <= nrf_logged_seq) return;      // 没有新包

    SD_LOCK();
    bool isNew = !SD.exists("/nrf.csv");
    File f = SD.open("/nrf.csv", FILE_APPEND);
    if (!f) { SD_UNLOCK(); return; }
    if (isNew) f.println("ts_s,seq,ch,rate,len,hex");
    // ring 是 [0] 最新：从旧到新遍历，seq > logged 的都补上
    for (int i = st.pkt_count - 1; i >= 0; i--) {
        const nrf_pkt_t& p = st.pkts[i];
        if (p.seq <= nrf_logged_seq) continue;
        char hex[NRF_PKT_MAX * 2 + 1];
        int pos = 0;
        for (int b = 0; b < p.len; b++) pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X", p.data[b]);
        f.printf("%lu,%u,%u,%s,%u,%s\n", now / 1000, (unsigned)p.seq,
                 (unsigned)p.ch, nrf_rate_name(p.rate), (unsigned)p.len, hex);
        if (p.seq > nrf_logged_seq) nrf_logged_seq = p.seq;
    }
    f.close();
    SD_UNLOCK();
}

static void nrf_serial_dump() {
    nrf_stats_t st;
    nrf_get_stats(&st);
    Serial.printf("\n=== nRF24 (%s) CH%d %s ===\n",
                  !st.available ? "NO MODULE" : (st.active ? "LIVE" : "stopped"),
                  (int)st.channel, nrf_rate_name(st.rate_idx));
    if (!st.available) {
        Serial.println("No nRF24L01+ detected. Check:");
        Serial.println("  CE=14 CSN=15 SCK=16 MISO=17 MOSI=18  (src/nrf24.h)");
        Serial.println("  VCC=3.3V (5V destroys the module), GND common,");
        Serial.println("  10uF + 0.1uF decoupling right at the module's VCC/GND pins.");
    }
    Serial.printf("packets=%u (logged up to seq=%u)\n",
                  (unsigned)st.packets, (unsigned)nrf_logged_seq);
    for (int i = 0; i < st.pkt_count; i++) {
        const nrf_pkt_t& p = st.pkts[i];
        Serial.printf("  #%u CH%d %s len=%u : ", (unsigned)p.seq, (int)p.ch,
                      nrf_rate_name(p.rate), (unsigned)p.len);
        for (int b = 0; b < p.len; b++) Serial.printf("%02X", p.data[b]);
        Serial.println();
    }
    Serial.println("==========================\n");
}

extern "C" {

void ui_nrf_on_toggle(void) {
    nrf_stats_t st;
    nrf_get_stats(&st);
    if (!st.available) {
        // 每次点击都重新试一遍 init（模块可能刚插上/刚上电）
        if (!nrf_sniffer_init()) {
            Serial.println("[nrf] no module detected — 'nrf dump' shows the wiring checklist");
            nrf_last_poll = 0;
            nrf_poll_ui();
            return;
        }
    }
    if (nrf_sniffer_active()) nrf_sniffer_stop();
    else                      nrf_sniffer_start(-1);   // -1 = 跳频全扫
    nrf_last_poll = 0;
    nrf_poll_ui();
}

void ui_nrf_on_screen_ready(void) {
    nrf_last_poll = 0;
    nrf_poll_ui();
}

} /* extern "C" */


// 系统信息 / 时间 / 串口命令
void print_system_info() {
    Serial.println("\n========================================");
    Serial.println("            SYSTEM INFO");
    Serial.println("========================================");
    Serial.printf("  Chip:        %s\n", ESP.getChipModel());
    Serial.printf("  Cores:       %d\n", ESP.getChipCores());
    Serial.printf("  Frequency:   %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Flash:       %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    if (psramFound()) {
        Serial.printf("  PSRAM:       %d MB (Free: %d KB)\n",
                      ESP.getPsramSize() / (1024 * 1024), ESP.getFreePsram() / 1024);
    } else {
        Serial.println("  PSRAM:       Not found");
    }
    Serial.printf("  Heap:        %d KB (Free: %d KB)\n", ESP.getHeapSize() / 1024, ESP.getFreeHeap() / 1024);
    if (sd_initialized) {
        uint64_t cardSize = SD.cardSize();
        Serial.printf("  SD Card:     Mounted, %llu MB\n", cardSize / (1024 * 1024));
    } else {
        Serial.println("  SD Card:     Not mounted");
    }
    Serial.printf("  LVGL:        v%d.%d.%d\n", lv_version_major(), lv_version_minor(), lv_version_patch());
    Serial.printf("  Build:       %s %s\n", __DATE__, __TIME__);
    Serial.println("========================================\n");
}

void get_ntp_time() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected. Use WiFi interface to connect first.");
        return;
    }
    // 与屏幕时钟共用同一套时区 / SNTP 设置（幂等）
    sntp_start_once();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        Serial.println("Failed to obtain NTP time.");
        return;
    }
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("Current time (UTC+8): %s\n", timeStr);
}

// ============================================================================
//  CLI：SD 卡文件操作（ls / cat / stat / rm / save）
//  ----------------------------------------------------------------------------
//  参数一律取【原始大小写】：cmd 已经整体小写化，但路径不该被篡改
//  （FAT 本身不区分大小写，可 quoted 文件名/混合大小写日志名保持原貌更安全）。
//  cat 复用文件屏那条分片发送链路（128B/轮，由 original_loop 的
//  pump_txt_transfer 推进），所以 256KB 大文件也不会卡住 LVGL。
// ============================================================================
static String cli_path_arg(const String& original) {
    String s = original;
    s.trim();
    if (s.startsWith("/")) s = s.substring(1);      // 和 cmd 的处理保持一致
    int sp = s.indexOf(' ');
    if (sp < 0) return String("");
    String a = s.substring(sp + 1);
    a.trim();
    // 允许 cat "my file.txt" / cat 'my file.txt'
    if (a.length() >= 2 && (a[0] == '"' || a[0] == '\'') && a[a.length() - 1] == a[0])
        a = a.substring(1, a.length() - 1);
    a.trim();
    if (a.length() == 0) return a;
    if (!a.startsWith("/")) a = "/" + a;
    return a;
}

// 目录项打印：name() 在不同 core 版本下有时带路径、有时只有基名，统一补全，
// 这样 ls 的输出可以直接复制给 cat 用。
static void cli_print_dir(const String& dir) {
    SD_LOCK();
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        SD_UNLOCK();
        Serial.printf("ls: not a directory: %s\n", dir.c_str());
        return;
    }
    Serial.printf("--- %s ---\n", dir.c_str());
    File f = root.openNextFile();
    int n = 0;
    uint64_t total = 0;
    bool truncated = false;
    while (f) {
        if (n >= 80) { truncated = true; f.close(); break; }
        String nm = f.name();
        if (!nm.startsWith("/")) {
            String base = dir;
            if (!base.endsWith("/")) base += "/";
            nm = base + nm;
        }
        if (f.isDirectory()) {
            Serial.printf("DIR       -  %s\n", nm.c_str());
        } else {
            unsigned sz = (unsigned)f.size();
            total += sz;
            Serial.printf("FILE %7u  %s\n", sz, nm.c_str());
        }
        n++;
        f.close();                                  // 不关会漏项/耗句柄
        f = root.openNextFile();
    }
    root.close();
    SD_UNLOCK();
    Serial.printf("--- %d entr%s, %llu bytes%s ---\n", n, (n == 1) ? "y" : "ies",
                  (unsigned long long)total, truncated ? " (truncated at 80)" : "");
}

static void cli_file_stat(const String& path) {
    SD_LOCK();
    bool exists = SD.exists(path);
    if (!exists) { SD_UNLOCK(); Serial.printf("stat: not found: %s\n", path.c_str()); return; }
    File f = SD.open(path);
    if (!f) { SD_UNLOCK(); Serial.printf("stat: cannot open: %s\n", path.c_str()); return; }
    bool isDir = f.isDirectory();
    unsigned sz = isDir ? 0 : (unsigned)f.size();
    f.close();
    SD_UNLOCK();
    Serial.printf("path : %s\ntype : %s\nsize : %u bytes\n",
                  path.c_str(), isDir ? "directory" : "file", sz);
}

static void cli_file_rm(const String& path) {
    if (path == "/" || path.length() < 2) {
        Serial.println("rm: refusing to delete the root directory");
        return;
    }
    SD_LOCK();
    if (!SD.exists(path)) { SD_UNLOCK(); Serial.printf("rm: not found: %s\n", path.c_str()); return; }
    File f = SD.open(path);
    bool isDir = f ? f.isDirectory() : false;
    unsigned sz = (isDir || !f) ? 0 : (unsigned)f.size();
    if (f) f.close();
    bool ok = isDir ? SD.rmdir(path) : SD.remove(path);
    SD_UNLOCK();
    Serial.printf("rm: %s %s (%s, %u bytes)\n", ok ? "deleted" : "FAILED",
                  path.c_str(), isDir ? "dir" : "file", sz);
}

void process_serial_command(const String& cmd_in) {
    String cmd = cmd_in;
    cmd.trim();
    if (cmd.startsWith("/")) cmd = cmd.substring(1);   // 兼容带斜杠的写法
    cmd.toLowerCase();

    if (cmd == "dht11 profile start") {
        start_dht11_logging();
    }
    else if (cmd == "dht11 profile stop") {
        stop_dht11_logging();
    }
    else if (cmd == "dht11 status") {
        Serial.println(dht11_logging ? "DHT11 logging is ACTIVE." : "DHT11 logging is STOPPED.");
    }
    else if (cmd == "fetch") {
        print_system_info();
    }
    else if (cmd == "reboot") {
        esp_restart();
    }
    else if (cmd == "date") {
        get_ntp_time();
    }
    // 信道占用工具也能从串口用：
    //   /chan scan  触发一次全信道扫描（结果同样会刷到屏幕上）
    //   /chan       把最近一次的占用图打到串口（不重扫，秒回）
    // 没屏幕 / 屏幕坏了的时候靠这两条核对聚合逻辑。
    else if (cmd == "chan scan") {
        wscan_request();
        Serial.println("[wscan] scan requested, send /chan again for the table");
    }
    else if (cmd == "chan") {
        wscan_serial_dump();
    }
    // 继电器：和屏幕上的开关走同一个 relay_set()，
    // 所以串口改了状态，界面（如果开着）也会同步。
    // ---- 文件系统 CLI ----------------------------------------------------
    //  ls [dir]        列目录（最多 80 项，输出可直接喂给 cat）
    //  cat <file>      把文件内容整段发到串口（复用文件屏的分片链路）
    //  stat <path>     看类型/大小
    //  rm <path>       删文件或空目录
    // 路径取原始大小写的参数，注意用 cli_path_arg() 而不是 cmd。
    else if (cmd == "ls") {
        cli_print_dir(current_path);
    }
    else if (cmd.startsWith("ls ")) {
        String p = cli_path_arg(cmd_in);
        cli_print_dir(p.length() ? p : current_path);
    }
    else if (cmd.startsWith("cat ")) {
        String p = cli_path_arg(cmd_in);
        if (p.length() == 0) {
            Serial.println("usage: cat /file.txt");
        } else {
            // 先 exists 再 open：直接 open 不存在的路径会在 VFS 里刷一行
            // "no permits for creation" 的红色错误，纯噪音。
            SD_LOCK();
            bool there = SD.exists(p);
            SD_UNLOCK();
            if (!there) {
                Serial.printf("cat: not found: %s\n", p.c_str());
            } else {
                String err;
                if (!send_file_via_serial(p.c_str(), "CAT", CAT_MAX_BYTES, &err))
                    Serial.printf("cat: %s (%s)\n", p.c_str(), err.c_str());
            }
        }
    }
    else if (cmd.startsWith("stat ")) {
        String p = cli_path_arg(cmd_in);
        if (p.length() == 0) Serial.println("usage: stat /file.txt");
        else cli_file_stat(p);
    }
    else if (cmd.startsWith("rm ")) {
        String p = cli_path_arg(cmd_in);
        if (p.length() == 0) Serial.println("usage: rm /file.txt");
        else cli_file_rm(p);
    }
    else if (cmd == "relay on") {
        relay_set(true);
    }
    else if (cmd == "relay off") {
        relay_set(false);
    }
    else if (cmd == "relay status") {
        Serial.printf("relay: %s (GPIO%d, active-%s)\n",
                      relay_hw_on ? "ON" : "OFF", RELAY_PIN,
                      (RELAY_ACTIVE_LEVEL == HIGH) ? "HIGH" : "LOW");
    }
    // 灯带：和界面滑块共用 led_apply()，串口改了值，界面（如果开着）同步刷新。
    // ★ "led status" 必须先判：它也满足 startsWith("led ")，
    //   放进通用分支会被 sscanf 全落空，只剩一句 usage。
    else if (cmd == "led status") {
        Serial.printf("led: GPIO%d hue=%d bright=%u speed=%u count=%u rmt=%s\n",
                      WS2812_PIN, (int)led.hue, (unsigned)led.bright,
                      (unsigned)led.speed, (unsigned)led.count,
                      led.rmt_ok ? "ok" : "FAILED");
    }
    else if (cmd.startsWith("led ")) {
        int v = 0;
        if      (sscanf(cmd.c_str(), "led hue %d", &v) == 1)    led_apply(UI_LED_HUE, v);
        else if (sscanf(cmd.c_str(), "led bright %d", &v) == 1) led_apply(UI_LED_BRIGHT, v);
        else if (sscanf(cmd.c_str(), "led speed %d", &v) == 1)  led_apply(UI_LED_SPEED, v);
        else if (sscanf(cmd.c_str(), "led count %d", &v) == 1)  led_apply(UI_LED_COUNT, v);
        else Serial.println("usage: led hue|bright|speed|count <n> | led status");
    }
    // RF Monitor：被动监听也能全串口驱动（无屏排障 / 远程采集）。
    // dump 输出 deauth 源和 AP 表，格式对齐 /chan 的风格。
    else if (cmd == "rfmon start") {
        rfmon_start();
        Serial.println("[rfmon] started, 'rfmon dump' for a snapshot");
    }
    else if (cmd == "rfmon stop") {
        rfmon_stop();
    }
    else if (cmd == "rfmon status") {
        rfmon_stats_t st;
        rfmon_get_stats(&st);
        Serial.printf("rfmon: %s CH%d bcn=%u prb=%u deauth=%u disas=%u aps=%u\n",
                      st.active ? "LIVE" : "stopped", (int)st.channel,
                      (unsigned)st.beacons, (unsigned)st.probes,
                      (unsigned)st.deauths, (unsigned)st.disassoc,
                      (unsigned)st.ap_count);
    }
    else if (cmd == "rfmon clear") {
        rfmon_clear();
        Serial.println("[rfmon] counters cleared");
    }
    else if (cmd == "rfmon dump") {
        rfmon_serial_dump();
    }
    // 日志开关：默认随监听自动开（见 rfmon_log_tick），这里可手动干预
    else if (cmd == "rfmon log on") {
        if (!sd_initialized) {
            Serial.println("rfmon log: SD not mounted, cannot log");
        } else {
            rfmon_logging = true;
            Serial.println("rfmon log: ON -> /rfmon.csv");
        }
    }
    else if (cmd == "rfmon log off") {
        rfmon_logging = false;
        Serial.println("rfmon log: OFF (auto-enable on next rfmon start is disabled until reboot or 'log on')");
    }
    else if (cmd == "rfmon log status") {
        Serial.printf("rfmon log: %s (SD %s, file /rfmon.csv)\n",
                      rfmon_logging ? "ON" : "OFF",
                      sd_initialized ? "mounted" : "NOT mounted");
    }
    // 打印日志尾部，不用拔卡就能核对落盘内容
    else if (cmd == "rfmon log tail") {
        if (!sd_initialized) { Serial.println("SD not mounted"); }
        else {
            SD_LOCK();
            File f = SD.open("/rfmon.csv", FILE_READ);
            if (!f) { SD_UNLOCK(); Serial.println("/rfmon.csv not found"); }
            else {
                size_t sz = f.size();
                size_t from = (sz > 768) ? (sz - 768) : 0;
                f.seek(from);
                Serial.printf("--- /rfmon.csv (%u bytes, last %u) ---\n",
                              (unsigned)sz, (unsigned)(sz - from));
                while (f.available()) Serial.write(f.read());
                Serial.println("--- end ---");
                f.close();
                SD_UNLOCK();
            }
        }
    }
    // nRF24 嗅探：和 rfmon 一样全串口可驱动。
    // `nrf sniff` 跳频全扫（2..80 × 三种速率）；`nrf sniff <ch>` 锁单信道。
    else if (cmd == "nrf start" || cmd == "nrf sniff") {
        if (!nrf_sniffer_init()) {
            Serial.println("[nrf] no module detected — 'nrf dump' shows the wiring checklist");
        } else {
            nrf_sniffer_start(-1);
            Serial.println("[nrf] started, 'nrf dump' for a snapshot");
        }
    }
    else if (cmd.startsWith("nrf sniff ")) {
        int ch = 0;
        if (sscanf(cmd.c_str(), "nrf sniff %d", &ch) == 1 && ch >= 0 && ch <= 125) {
            if (!nrf_sniffer_init()) {
                Serial.println("[nrf] no module detected — 'nrf dump' shows the wiring checklist");
            } else {
                nrf_sniffer_start(ch);
            }
        } else {
            Serial.println("usage: nrf sniff <0..125> | nrf sniff (hopping)");
        }
    }
    else if (cmd == "nrf stop") {
        nrf_sniffer_stop();
    }
    else if (cmd == "nrf status") {
        nrf_stats_t st;
        nrf_get_stats(&st);
        Serial.printf("nrf: %s CH%d %s pkts=%u logged=%u\n",
                      !st.available ? "NO MODULE" : (st.active ? "LIVE" : "stopped"),
                      (int)st.channel, nrf_rate_name(st.rate_idx),
                      (unsigned)st.packets, (unsigned)nrf_logged_seq);
    }
    else if (cmd == "nrf dump") {
        nrf_serial_dump();
    }
    else if (cmd == "nrf clear") {
        nrf_clear_stats();
        Serial.println("[nrf] counters cleared");
    }
    else if (cmd == "help") {
        Serial.println("/dht11 profile start | stop | status");
        Serial.println("/chan scan | /chan");
        Serial.println("relay on | relay off | relay status");
        Serial.println("led hue|bright|speed|count <n> | led status");
        Serial.println("rfmon start | stop | status | clear | dump");
        Serial.println("rfmon log on | off | status | tail");
        Serial.println("nrf sniff [ch] | stop | status | dump | clear");
        Serial.println("ls [dir] | cat <file> | stat <path> | rm <path>");
        Serial.println("fetch | date | reboot");
    }
    else {
        if (ui_labelrx != NULL) lv_label_set_text(ui_labelrx, cmd_in.c_str());
    }
}

void check_ui_controls() {
    Serial.println("=== UI Controls Check ===");
    Serial.printf("ui_dht11: %p\n", (void*)ui_dht11);
    Serial.printf("ui_Labeltemp: %p\n", (void*)ui_Labeltemp);
    Serial.printf("ui_Labelrh: %p\n", (void*)ui_Labelrh);
    Serial.printf("ui_file: %p\n", (void*)ui_file);
    Serial.println("Commands: /dht11 profile start|stop|status, fetch, date, reboot, help");
}

// 初始化 / 主循环
void original_setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("LVGL Version: %d.%d.%d\n",
                  lv_version_major(), lv_version_minor(), lv_version_patch());

    if (psramFound()) {
        Serial.printf("[SYS] PSRAM found: %u bytes (used for file/JPEG buffers only)\n",
                      (unsigned)ESP.getPsramSize());
    } else {
        Serial.println("[SYS] PSRAM not found, internal RAM only");
    }

#if LV_USE_LOG != 0 && LV_LOG_LEVEL < LV_LOG_LEVEL_NONE
    // 不注册回调的话 LVGL 日志会被静默丢弃；等级已在 lv_conf.h 降到 WARN。
    // 回调实体在 display_port.cpp（disp_lv_log_cb）。
    lv_log_register_print_cb(disp_lv_log_cb);
#endif

    // SD 已在 main.cpp 的 setup() 里挂载过（为了先读 wifi.cfg），这里只做兜底
    if (!sd_initialized) storage_mount();
    if (sd_initialized && file_names.empty()) read_directory("/");

    // 显示初始化（LCD + 触摸 + LVGL + 缓冲 + 驱动注册）全在移植层
    disp_port_init_all();

    // 继电器 GPIO 初始化：必须在 ui_init() 之前 ——
    // ui_relay 屏（懒创建）建屏时会读 relay_hw_on 同步界面，
    // 缓存必须先有确定值（false）。
    relay_hw_init();

    // WS2812：RMT 初始化 + 上电先推一帧默认色（1 颗、75%、蓝）。
    // 必须在 ui_init() 之前：ui_led 屏建屏时 led_sync_ui() 读这份状态。
    ws2812_hw_init();
    ws2812_push_frame();
    led.dirty = false;

    // DHT11 上电后需要约 1s 稳定
    dht.begin();
    delay(1100);
    last_dht_read_ms = 0;

    // ui_init() 内部已经调用过 ui_wifi_screen_init()
    ui_init();

    lv_obj_add_event_cb(ui_wifi_scan_btn, [](lv_event_t* e) {
        (void)e;
        wifi_scan_request();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_wifi_connect_btn, [](lv_event_t* e) {
        (void)e;
        wifi_connect_to_selected();
    }, LV_EVENT_CLICKED, NULL);

    // 蓝牙(BLE)串口屏
#if ENABLE_BLE
    // 这个屏不在 ui.c 里（避免被 SquareLine 重新生成时覆盖），这里手动建
    ui_ble_screen_init();
    if (ui_ble_send_btn != NULL) {
        lv_obj_add_event_cb(ui_ble_send_btn, [](lv_event_t* e) {
            (void)e;
            ui_ble_on_send_requested();
        }, LV_EVENT_CLICKED, NULL);
    }
#endif

    if (ui_dht11 != NULL) {
        uik_screen_load(ui_dht11);
        delay(100);
    }

    check_ui_controls();
    fix_dht11_label_display();

    // 先设好时区（即使 WiFi 没连，localtime 也已是东八区）。
    // 校时：有屏幕时钟时 WiFi 连上后自动触发；否则只在串口 date 命令时进行。
    tz_apply_utc8();

#if ENABLE_CLOCK
    // 1000ms：时间要秒级刷新（DHT 读数另有 2.5s 限速）
    lv_timer_create(dht_timer_cb, 1000, NULL);
#else
    // 2000ms 足够，只用来刷新温湿度
    lv_timer_create(dht_timer_cb, 2000, NULL);
#endif

    // WS2812 动画 tick：30fps。静止且参数没变时回调里直接 return，
    // 不产生 RMT 写，也不碰 LVGL 控件 —— 不会给刷新循环添堵。
    lv_timer_create(led_anim_timer_cb, 33, NULL);

    if (ui_file != NULL) {
        if (ui_refresh_btn != NULL) {
            lv_obj_add_event_cb(ui_refresh_btn, refresh_file_list_cb, LV_EVENT_CLICKED, NULL);
        }
        update_file_list_now();
    }

    update_dht11_display();
}

void original_loop() {
    lv_timer_handler();

    // 串口命令（由 main.cpp 的 loop 任务投递过来）
    SerialCmdMsg_t cmd;
    if (serialCmdQueue && xQueueReceive(serialCmdQueue, &cmd, 0) == pdTRUE) {
        process_serial_command(String(cmd.line));
    }

    if (wifi_scan_state != WSCAN_IDLE) wifi_scan_poll();
    if (wscan_state != WSCAN_IDLE) wscan_poll();
    rfmon_poll_ui();   // 内部自带 500ms 节流 + 屏判空，未建屏时开销≈0
    rfmon_log_tick();  // 未在监听时第一句就 return（1s 节流之后才取快照）
    nrf_poll_ui();     // nRF24 嗅探屏：同样 500ms 节流 + 屏判空
    nrf_log_tick();    // /nrf.csv 增量落盘：没新包第一句就 return

#if ENABLE_BLE
    // BLE 只在这里碰 LVGL：core1 的 BLE 任务只往缓冲里塞数据
    bleuart_ui_service();
#endif

    if (tx_buffer != nullptr) pump_txt_transfer();

    if (dht11_logging && sd_initialized) write_dht11_data_to_sd();

    delay(3);
}
