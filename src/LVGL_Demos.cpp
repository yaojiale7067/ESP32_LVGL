// ============================================================================
//  LVGL_Demos.cpp  —  UI / 文件管理 / DHT11 / WiFi 业务逻辑
//  本文件只被 LVGL 任务（core0）执行，唯一的例外是 main.cpp 的 loop 任务通过
//  serialCmdQueue 投递命令过来，同样由 LVGL 任务消费。
// ============================================================================

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

#include "touch.h"
#include "app_shared.h"
#include "ui_test/ui.h"
#include "ui_test/screens/ui_wifi.h"
#if ENABLE_BLE
#include "ui_test/screens/ui_ble.h"
#endif

#define DHTPIN  7
#define DHTTYPE DHT11
#define SD_CS   4

static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

#define FULL_BUF_SIZE     (screenWidth * screenHeight)
#define FALLBACK_BUF_SIZE (screenWidth * screenHeight / 10)

static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;
static lv_disp_draw_buf_t disp_draw_buf;
static lv_disp_drv_t disp_drv;

TFT_eSPI my_lcd = TFT_eSPI();
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

// --- [FIX 6] DHT11 读取限速 -------------------------------------------------
// DHT11 单次读取要关中断 1~2s，而且两次读取之间必须间隔 >=1s，
// 以前 UI 定时器(1.5s)和 SD 写入(3s)在同一任务里各自读温度+湿度，
// 会出现“背靠背读两次”，第二次必然 NaN，还会打断 WiFi 协议栈。
static unsigned long last_dht_read_ms  = 0;
static float         last_dht_temp     = NAN;
static float         last_dht_humi     = NAN;
static const unsigned long DHT_MIN_INTERVAL = 2500;

static String selected_ssid = "";

// --- [FIX 4] WiFi 扫描状态机（不阻塞 LVGL） --------------------------------
enum WifiScanState { WSCAN_IDLE = 0, WSCAN_WAIT_RESULT };
static WifiScanState  wifi_scan_state = WSCAN_IDLE;
static unsigned long  wifi_scan_start = 0;
static const unsigned long WIFI_SCAN_TIMEOUT_MS = 25000;

// --- [FIX 12] TXT 串口发送改为分片，避免整段阻塞 UI ------------------------
static char*  tx_buffer   = nullptr;
static size_t tx_len      = 0;
static size_t tx_sent     = 0;
static String tx_file     = "";

// --- [FIX 3] 列表延迟重建（禁止在按键自己的回调里销毁它） -------------------
static lv_timer_t* file_list_rebuild_timer = NULL;

// --- 前向声明 ---------------------------------------------------------------
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
void create_new_text_file();
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
void ui_set_status_ex(const char* text, uint32_t color);   // WiFi 屏状态标签
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

// ============================================================================
//  LVGL 移植层
// ============================================================================
#if LV_USE_LOG != 0 && LV_LOG_LEVEL < LV_LOG_LEVEL_NONE
// 只有在日志等级不是 NONE 时才注册。
// 警告：若把 lv_conf.h 的 LV_LOG_LEVEL 调成 LV_LOG_LEVEL_TRACE，
// 这里会把 LVGL 内部每次内存分配/事件都打到串口（就是之前刷屏的原因），
// 而且 "%pV" 格式符一旦没被 LVGL 自己的 lv_snprintf 处理就会输出乱码。
// 排查完请调回 LV_LOG_LEVEL_WARN。
void my_print(const char* buf) {
    Serial.printf("%s", buf);
}
#endif

void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // 【关键】TFT 与 SD 卡共用 SCLK/MOSI/MISO（12/11/13），只有 CS 不同。
    // 这里刷新屏幕的同时，core1 可能在读 SD 卡 —— 两者会同时操作同一组
    // HSPI 寄存器，表现就是花屏。所以显存写入必须和 SD 操作互斥。
    SPI_BUS_LOCK();
    my_lcd.startWrite();
    my_lcd.setAddrWindow(area->x1, area->y1, w, h);
    my_lcd.pushColors((uint16_t*)&color_p->full, w * h, true);
    my_lcd.endWrite();          // endWrite() 会把最后一段 SPI 事务等完，
                                // 保证 lv_disp_flush_ready() 之后缓冲可安全复用
    SPI_BUS_UNLOCK();

    lv_disp_flush_ready(drv);
}

void my_touchpad_read(lv_indev_drv_t* indev_driver, lv_indev_data_t* data) {
    (void)indev_driver;
    if (touch_has_signal() && touch_touched()) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = touch_last_x;
        data->point.y = touch_last_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

extern "C" {
    void serial_send(const char* buf) {
        if (buf == nullptr || *buf == '\0') return;
        Serial.println(buf);
    }
}

// ============================================================================
//  小工具
// ============================================================================
static void ui_set_status(const char* text, uint32_t color) {
    if (ui_status_label == NULL) return;
    lv_label_set_text(ui_status_label, text);
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
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

// ============================================================================
//  DHT11（统一入口 + 限速 + 缓存）
// ============================================================================
static bool dht_read_cached(float* temp, float* humi, bool force) {
    unsigned long now = millis();
    if (!force && last_dht_read_ms != 0 && (now - last_dht_read_ms) < DHT_MIN_INTERVAL) {
        // 间隔不足：复用上一次“成功”的结果；若从未成功过则返回失败
        if (isnan(last_dht_temp) || isnan(last_dht_humi)) return false;
        *temp = last_dht_temp;
        *humi = last_dht_humi;
        return true;
    }
    last_dht_read_ms = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
        // 注意：失败时只更新时间戳，不清掉上一次的有效值，
        // 否则一次抖动会让界面显示 "--" 直到下一个限速窗口
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
        if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, "--");
        if (ui_Labelrh   != NULL) lv_label_set_text(ui_Labelrh,   "--");
        return;
    }
    char temp_str[16], humi_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f", t);
    snprintf(humi_str, sizeof(humi_str), "%.1f", h);
    if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, temp_str);
    if (ui_Labelrh   != NULL) lv_label_set_text(ui_Labelrh,   humi_str);
}

// 定时器只负责“显示”，读不读由 dht_read_cached 的限速决定
void dht_timer_cb(lv_timer_t* timer) {
    (void)timer;
    update_dht11_display();
}

// 这个函数原来是在运行时把温湿度数值标签挪到正确位置、换大字号、清掉占位文案。
// 现在 ui_dht11.c 已经把布局、字号、初值都按统一规范建好了，
// 这里只需要保证文案是干净的占位符；保留函数名是为了不动 original_setup() 的调用点。
void fix_dht11_label_display() {
    if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, "--");
    if (ui_Labelrh   != NULL) lv_label_set_text(ui_Labelrh,   "--");
}

// ============================================================================
//  SD 卡：挂载 + 加锁的公共入口
// ============================================================================
bool storage_mount() {
    if (sd_initialized) return true;             // 幂等：重复挂载会二次初始化 HSPI
    SD_LOCK();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    sdSPI.begin(12, 13, 11, SD_CS);
    delay(10);
    // 显式给 20MHz，默认 4MHz 太慢；之前 TFT 用 80MHz、SD 用默认值，
    // 两边共用一个 HSPI host，频率靠 beginTransaction 各自设置。
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
            // ESP32 的 File::name() 返回的是 basename，所以这里拼路径是对的
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

// ============================================================================
//  [FIX 3] 列表重建：绝不在按键回调里销毁按键
// ============================================================================
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

void update_file_list_now() {
    if (ui_file_list == NULL) return;
    lv_obj_clean(ui_file_list);

    if (!sd_initialized) {
        lv_obj_t* label = lv_label_create(ui_file_list);
        lv_label_set_text(label, "SD Card not found\n(tap R to retry)");
        lv_obj_center(label);
        return;
    }

    lv_obj_t* scroll_panel = lv_obj_create(ui_file_list);
    lv_obj_set_size(scroll_panel, 280, 140);
    lv_obj_set_pos(scroll_panel, 5, 5);
    lv_obj_set_style_bg_color(scroll_panel, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scroll_panel, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(scroll_panel, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* list = lv_list_create(scroll_panel);
    lv_obj_set_size(list, 270, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);

    if (ui_path_label != NULL) {
        char path_str[128];
        snprintf(path_str, sizeof(path_str), "Path: %s", current_path.c_str());
        lv_label_set_text(ui_path_label, path_str);
    }

    lv_obj_t* new_btn = lv_list_add_btn(list, LV_SYMBOL_PLUS, " New Text File");
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(new_btn, [](lv_event_t* e) { (void)e; create_new_text_file(); },
                        LV_EVENT_CLICKED, NULL);

    const char* mode_text;
    if (copy_mode)        mode_text = " Mode: Copy (Tap file to copy)";
    else if (delete_mode) mode_text = " Mode: Delete (Tap file/folder to delete)";
    else                  mode_text = "Mode: Normal (Tap to open)";

    lv_obj_t* mode_btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, mode_text);
    if (copy_mode)        lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
    else if (delete_mode) lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
    else                  lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x3D3D3D), LV_STATE_DEFAULT);

    lv_obj_add_event_cb(mode_btn, [](lv_event_t* e) {
        (void)e;
        if (!copy_mode && !delete_mode)      { copy_mode = true;  delete_mode = false; }
        else if (copy_mode && !delete_mode)  { copy_mode = false; delete_mode = true;  }
        else                                 { copy_mode = false; delete_mode = false; }
        schedule_file_list_update(1);
    }, LV_EVENT_CLICKED, NULL);

    if (has_copied) {
        lv_obj_t* paste_btn = lv_list_add_btn(list, LV_SYMBOL_DOWN, LV_SYMBOL_PASTE " Paste Here");
        lv_obj_set_style_bg_color(paste_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(paste_btn, [](lv_event_t* e) {
            (void)e;
            paste_file(current_path.c_str());
        }, LV_EVENT_CLICKED, NULL);

        lv_obj_t* clear_btn = lv_list_add_btn(list, LV_SYMBOL_CLOSE, LV_SYMBOL_CLOSE " Clear Copy");
        lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(clear_btn, [](lv_event_t* e) {
            (void)e;
            has_copied = false;
            copy_source_path = "";
            schedule_file_list_update(1);
        }, LV_EVENT_CLICKED, NULL);
    }

    if (current_path != "/") {
        lv_obj_t* back_btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, ".. (Parent)");
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
            if (is_jpeg_file(name))      display_name = name + " [IMG]";
            else if (is_json_file(name)) display_name = name + " [EDIT]";
            else if (is_txt_file(name))  display_name = name + " [SEND]";
        }
        lv_obj_t* item_btn = lv_list_add_btn(list, symbol, display_name.c_str());
        lv_obj_set_user_data(item_btn, (void*)(intptr_t)i);
        if (copy_mode && !is_dir_item) lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x336633), LV_STATE_DEFAULT);
        else if (delete_mode)          lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x663333), LV_STATE_DEFAULT);

        lv_obj_add_event_cb(item_btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx < 0 || idx >= (int)file_names.size()) return;

            // 先取出所有需要的数据，后面一旦重建列表，btn 就失效了
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
            else if (is_txt_file(name))  { send_txt_via_serial(full_path.c_str()); }
        }, LV_EVENT_CLICKED, NULL);
    }

    if (file_names.empty()) {
        lv_obj_t* hint = lv_label_create(list);
        lv_label_set_text(hint, current_path == "/"
                                ? "No files found\nTap 'New Text File' to create\nTap 'Mode' to switch modes"
                                : "Empty directory");
        lv_obj_center(hint);
        lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), LV_STATE_DEFAULT);
    }
}

// 兼容旧调用点：立即重建（只在“不是在按键回调里”的时候用）
void update_file_list() {
    update_file_list_now();
}

void refresh_file_list_cb(lv_event_t* e) {
    (void)e;
    if (!sd_initialized) {
        // [FIX] SD 没挂上时按 R 可以重试挂载，以前这里什么都不做
        if (!storage_mount()) {
            ui_set_status("SD mount failed", 0xFF8888);
            update_file_list_now();
            return;
        }
    }
    read_directory(current_path.c_str());
    schedule_file_list_update(1);
    ui_set_status("Refreshed", 0x88FF88);
}

// ============================================================================
//  [FIX 2] 新建文件：生命周期理顺，不再“只能用一次”
// ============================================================================
void close_newfile_editor() {
    // 子对象先删，再删容器；删完立刻把所有指针置空，避免野指针
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
        ui_set_status("File saved!", 0x88FF88);
    } else {
        Serial.printf("Failed to save: %s\n", current_edit_path.c_str());
        ui_set_status("Save failed!", 0xFF8888);
    }
    SD_UNLOCK();
}

void newfile_kb_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {          // 键盘上的“回车/√”
        newfile_save_from_textarea();
        close_newfile_editor();
    } else if (code == LV_EVENT_CANCEL) {  // 键盘上的“×”
        close_newfile_editor();
    }
}

void show_newfile_editor(const String& path) {
    // [FIX 2] 这里以前是 if (creating_new_file) return;，会把整个功能永久锁死
    if (newfile_container != NULL) return;      // 已经在编辑了，别叠第二层

    creating_new_file = true;
    current_edit_path = path;

    newfile_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(newfile_container, screenWidth, screenHeight);
    lv_obj_set_pos(newfile_container, 0, 0);
    lv_obj_set_style_bg_color(newfile_container, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(newfile_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(newfile_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(newfile_container);
    lv_obj_set_size(title_bar, screenWidth, 35);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    String title_text = "New File: " + path.substring(path.lastIndexOf('/') + 1);
    lv_label_set_text(title_label, title_text.c_str());
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 50, 30);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(close_btn, 5, LV_STATE_DEFAULT);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    // 先保存再关闭，避免用户以为点了 X 内容还在
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        (void)e;
        newfile_save_from_textarea();
        close_newfile_editor();
    }, LV_EVENT_CLICKED, NULL);

    // [FIX] 高度从 screenHeight-50 改成 100：以前 190 高的编辑区会被
    //       贴底 100 高的键盘盖住下面约 60px
    newfile_textarea = lv_textarea_create(newfile_container);
    lv_obj_set_size(newfile_textarea, screenWidth - 10, 100);
    lv_obj_set_pos(newfile_textarea, 5, 40);
    lv_textarea_set_placeholder_text(newfile_textarea, "Enter text here...");
    lv_textarea_set_cursor_click_pos(newfile_textarea, true);
    lv_obj_set_style_bg_color(newfile_textarea, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(newfile_textarea, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(newfile_textarea, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(newfile_textarea, lv_color_hex(0x555555), LV_STATE_DEFAULT);

    newfile_keyboard = lv_keyboard_create(newfile_container);
    lv_keyboard_set_textarea(newfile_keyboard, newfile_textarea);
    lv_obj_set_size(newfile_keyboard, screenWidth, 100);
    lv_obj_align(newfile_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(newfile_keyboard, newfile_kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(newfile_keyboard, newfile_kb_event_cb, LV_EVENT_CANCEL, NULL);
}

void create_new_text_file() {
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
        ui_set_status("Failed to create file!", 0xFF8888);
        return;
    }
    Serial.printf("Created file: %s\n", full_path.c_str());
    show_newfile_editor(full_path);
}

// ============================================================================
//  JSON 编辑器
// ============================================================================
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
        content.concat((const char*)buf, got);   // 按块拼接，避免逐字节 +=
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
        ui_set_status("Saved", 0x88FF88);
    }
    close_editor();
}

void show_json_editor(const char* path) {
    if (showing_editor) return;

    bool truncated = false;
    String content = read_text_file(path, JSON_EDIT_MAX_BYTES, &truncated);

    // [FIX 9] 以前超 4000 字节会截断后仍允许保存 → 一按 Save 就把原文件写坏。
    //         现在超限直接只读打开，绝不提供保存入口。
    if (truncated) {
        ui_set_status("File too big (>32KB), read-only", 0xFFAA44);
    }
    showing_editor = true;
    current_edit_path = String(path);
    if (content.length() == 0) content = "";

    editor_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(editor_container, screenWidth, screenHeight);
    lv_obj_set_pos(editor_container, 0, 0);
    lv_obj_set_style_bg_color(editor_container, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(editor_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(editor_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(editor_container);
    lv_obj_set_size(title_bar, screenWidth, 35);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, truncated ? "JSON (READ-ONLY)" : "JSON Editor");
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(truncated ? 0xFFAA44 : 0xFFFFFF), LV_STATE_DEFAULT);

    if (!truncated) {
        lv_obj_t* save_btn = lv_btn_create(title_bar);
        lv_obj_set_size(save_btn, 50, 28);
        lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -65, 0);
        lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
        lv_obj_t* save_label = lv_label_create(save_btn);
        lv_label_set_text(save_label, "Save");
        lv_obj_center(save_label);
        lv_obj_set_style_text_color(save_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(save_btn, save_and_close_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 50, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) { (void)e; close_editor(); },
                        LV_EVENT_CLICKED, NULL);

    editor_textarea = lv_textarea_create(editor_container);
    lv_obj_set_size(editor_textarea, screenWidth - 10, 100);
    lv_obj_set_pos(editor_textarea, 5, 45);
    lv_textarea_set_text(editor_textarea, content.c_str());
    lv_textarea_set_placeholder_text(editor_textarea, "Edit JSON content...");
    lv_obj_set_style_bg_color(editor_textarea, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editor_textarea, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor_textarea, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(editor_textarea, lv_color_hex(0x555555), LV_STATE_DEFAULT);

    // 只读模式下不给键盘，物理上就没法改
    if (!truncated) {
        editor_keyboard = lv_keyboard_create(editor_container);
        lv_keyboard_set_textarea(editor_keyboard, editor_textarea);
        lv_obj_set_size(editor_keyboard, screenWidth, 100);
        lv_obj_align(editor_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
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

// ============================================================================
//  JPEG 显示（[FIX 5] 不再死循环、不再绕过 LVGL 渲染）
// ============================================================================
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= my_lcd.height()) return 0;
    // TJpgDec 一边读 SD 卡一边往屏幕写，两条路径都碰 HSPI
    SPI_BUS_LOCK();
    my_lcd.pushImage(x, y, w, h, bitmap);
    SPI_BUS_UNLOCK();
    return 1;
}

void show_jpeg_image(const char* path) {
    // 清屏后立即刷一帧，保证 LVGL 的缓冲不会在下面直接画图时被改写
    my_lcd.fillScreen(TFT_BLACK);

    uint16_t w = 0, h = 0;
    JRESULT jr = TJpgDec.getSdJpgSize(&w, &h, path);
    if (jr != JDR_OK || w == 0 || h == 0) {
        Serial.printf("JPEG header failed (%d): %s\n", (int)jr, path);
        ui_set_status("Not a valid JPEG", 0xFF8888);
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        return;
    }

    int16_t x = (screenWidth  > (int)w) ? (screenWidth  - w) / 2 : 0;
    int16_t y = (screenHeight > (int)h) ? (screenHeight - h) / 2 : 0;

    TJpgDec.setCallback(tft_output);
    TJpgDec.setJpgScale(1);
    my_lcd.setSwapBytes(true);
    jr = TJpgDec.drawSdJpg(x, y, path);
    if (jr != JDR_OK) {
        Serial.printf("JPEG decode failed (%d): %s\n", (int)jr, path);
        ui_set_status("JPEG decode failed", 0xFF8888);
    } else {
        // 轮询等待“抬手”，但保留 LVGL 刷新 → 界面不会假死，也不会占着
        // lv_timer_handler 不跑（以前是 while(true) + delay(50) 死等）
        bool was_down = false;
        unsigned long t0 = millis();
        while (millis() - t0 < 30000UL) {
            bool down = (touch_has_signal() && touch_touched());
            if (was_down && !down) break;        // 抬手才算确认
            was_down = down;
            lv_timer_handler();
            delay(10);
        }
    }

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    ui_set_status("Image closed", 0x88FF88);
}

// ============================================================================
//  文件操作
// ============================================================================
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
    schedule_file_list_update(1);            // [FIX 3] 不能在这里直接销毁按钮

    char msg[128];
    const char* base = strrchr(path, '/');
    snprintf(msg, sizeof(msg), "Deleted: %s", base ? base + 1 : path);
    ui_set_status(msg, 0xFF8888);
}

void copy_file(const char* src_path) {
    copy_source_path = String(src_path);
    has_copied = true;
    Serial.printf("Copied: %s\n", src_path);
    char msg[128];
    const char* base = strrchr(src_path, '/');
    snprintf(msg, sizeof(msg), "Copied: %s", base ? base + 1 : src_path);
    ui_set_status(msg, 0xFFFF88);
}

void paste_file(const char* dest_dir) {
    if (!has_copied) {
        ui_set_status("Nothing to paste. Copy a file first.", 0xFF8888);
        return;
    }
    String filename  = copy_source_path.substring(copy_source_path.lastIndexOf('/') + 1);
    String dest_path = (String(dest_dir) == "/") ? ("/" + filename)
                                                 : (String(dest_dir) + "/" + filename);
    if (dest_path == copy_source_path) {
        ui_set_status("Source and destination are the same", 0xFF8888);
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
        ui_set_status(msg, 0x88FF88);
    } else if (exists) {
        ui_set_status("File exists! Copy cancelled.", 0xFF8888);
    } else {
        ui_set_status("Copy failed (open source/dest)", 0xFF8888);
    }
    if (src)  src.close();
    if (dest) dest.close();
    SD_UNLOCK();

    has_copied = false;
    copy_source_path = "";
    read_directory(current_path.c_str());
    schedule_file_list_update(1);
}

// ============================================================================
//  [FIX 12] TXT 串口发送：分片推进，不再在按键回调里 delay(2)*N
// ============================================================================
#define TX_MAX_BYTES   (256 * 1024)
#define TX_CHUNK_BYTES 128      // 每轮循环最多写 128B（115200 下约 11ms），
                                // 保证 LVGL 仍然能刷到 ~30fps

void send_txt_via_serial(const char* path) {
    if (tx_buffer != nullptr) {
        ui_set_status("A transfer is already running", 0xFFAA44);
        return;
    }

    SD_LOCK();
    File file = SD.open(path);
    if (!file) {
        SD_UNLOCK();
        ui_set_status("Cannot open file!", 0xFF8888);
        return;
    }
    size_t fsize = file.size();
    if (fsize == 0 || fsize > TX_MAX_BYTES) {
        file.close();
        SD_UNLOCK();
        ui_set_status(fsize == 0 ? "Empty file" : "File too big (>256KB)", 0xFFAA44);
        return;
    }
    char* buffer = (char*)heap_caps_malloc(fsize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) buffer = (char*)malloc(fsize + 1);
    if (buffer == nullptr) {
        file.close();
        SD_UNLOCK();
        ui_set_status("Out of memory for TXT buffer", 0xFF8888);
        return;
    }
    size_t got = file.read((uint8_t*)buffer, fsize);
    file.close();
    SD_UNLOCK();

    buffer[got] = '\0';
    tx_buffer = buffer;
    tx_len    = got;
    tx_sent   = 0;
    tx_file   = String(path);

    Serial.println("\n========================================");
    Serial.println("SENDING TXT FILE VIA SERIAL");
    Serial.println("========================================");
    Serial.printf("File: %s (%u bytes)\n", path, (unsigned)got);
    Serial.println("----------------------------------------");
    ui_set_status("Sending over serial...", 0xFFFF00);
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
        ui_set_status(msg, 0xFFFF00);
    }

    if (tx_sent >= tx_len) {
        Serial.println();
        Serial.println("----------------------------------------");
        Serial.printf("TXT file sent: %s (%u bytes)\n", tx_file.c_str(), (unsigned)tx_len);
        Serial.println("========================================\n");
        char msg[96];
        snprintf(msg, sizeof(msg), "Sent: %s (%u bytes)",
                 tx_file.substring(tx_file.lastIndexOf('/') + 1).c_str(), (unsigned)tx_len);
        ui_set_status(msg, 0x88FF88);
        free(tx_buffer);
        tx_buffer  = nullptr;
        tx_len     = 0;
        tx_sent    = 0;
        last_report = 0;
        tx_file    = "";
    }
}

// ============================================================================
//  DHT11 记录到 SD
// ============================================================================
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

// ============================================================================
//  WiFi：配置持久化 + 异步扫描
// ============================================================================
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
            free(data);                      // [FIX] 以前扫描结果是 malloc 后永不释放
            lv_obj_set_user_data(child, NULL);
        }
    }
}

void wifi_scan_request() {
    if (wifi_scan_state != WSCAN_IDLE) {
        ui_set_status_ex("Scan already running...", 0xFFFF00);
        return;
    }
    if (!wifiScanRequestQueue || !wifiScanResultQueue) {
        ui_set_status_ex("WiFi queues not ready", 0xFF8888);
        return;
    }

    // 先排空结果队列，避免把上一次的扫描结果显示出来（串台）
    ScanResultMsg_t stale;
    while (xQueueReceive(wifiScanResultQueue, &stale, 0) == pdTRUE) { }
    ScanRequestMsg_t old;
    while (xQueueReceive(wifiScanRequestQueue, &old, 0) == pdTRUE) { }

    ScanRequestMsg_t req = { true };
    if (xQueueSend(wifiScanRequestQueue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        ui_set_status_ex("Scan request failed", 0xFF8888);
        return;
    }
    wifi_scan_state = WSCAN_WAIT_RESULT;
    wifi_scan_start = millis();
    ui_set_status_ex("Scanning...", 0xFFFF00);   // 只发请求，立刻返回，不再阻塞 50s
}

void wifi_scan_poll() {
    if (wifi_scan_state != WSCAN_WAIT_RESULT) return;

    if (millis() - wifi_scan_start > WIFI_SCAN_TIMEOUT_MS) {
        wifi_scan_state = WSCAN_IDLE;
        ui_set_status_ex("Scan timeout", 0xFF8888);
        return;
    }

    ScanResultMsg_t result;
    if (xQueueReceive(wifiScanResultQueue, &result, 0) != pdTRUE) return;   // 还没好
    wifi_scan_state = WSCAN_IDLE;

    if (result.count <= 0) {
        ui_set_status_ex("No networks found", 0xFF8888);
        return;
    }

    if (ui_wifi_list) {
        wifi_free_list_user_data();
        lv_obj_clean(ui_wifi_list);
        for (int i = 0; i < result.count; i++) {
            if (result.ssids[i][0] == '\0') continue;
            lv_obj_t* btn = lv_list_add_btn(ui_wifi_list, LV_SYMBOL_WIFI, result.ssids[i]);
            char* ssid_copy = (char*)malloc(strlen(result.ssids[i]) + 1);
            if (ssid_copy) strcpy(ssid_copy, result.ssids[i]);
            lv_obj_set_user_data(btn, ssid_copy);
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                lv_obj_t* b = lv_event_get_target(e);
                const char* ssid = (const char*)lv_obj_get_user_data(b);
                if (ssid == NULL) return;
                selected_ssid = String(ssid);
                ui_set_status_ex(ssid, 0xFFFFFF);
            }, LV_EVENT_CLICKED, NULL);
        }
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "Found %d networks. Select one.", result.count);
    ui_set_status_ex(msg, 0x88FF88);
}

void wifi_connect_to_selected() {
    if (selected_ssid.length() == 0) {
        ui_set_status_ex("No network selected", 0xFF8888);
        return;
    }
    const char* password = ui_wifi_password_ta ? lv_textarea_get_text(ui_wifi_password_ta) : "";
    if (password == NULL || strlen(password) == 0) {
        ui_set_status_ex("Password cannot be empty", 0xFF8888);
        return;
    }

    WifiConfigMsg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, selected_ssid.c_str(), sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, password, sizeof(cfg.password) - 1);

    if (xQueueSend(wifiConfigQueue, &cfg, pdMS_TO_TICKS(500)) != pdTRUE) {
        ui_set_status_ex("Failed to send config", 0xFF8888);
        return;
    }
    wifi_save_config_to_sd(cfg.ssid, cfg.password);
    ui_set_status_ex("Config saved, connecting in background...", 0x88FF88);
    if (ui_wifi_password_ta) lv_textarea_set_text(ui_wifi_password_ta, "");
}

// ui_wifi 的标签包装（ui_wifi_status_label 可能为 NULL）
void ui_set_status_ex(const char* text, uint32_t color) {
    if (ui_wifi_status_label == NULL) return;
    lv_label_set_text(ui_wifi_status_label, text);
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(color), LV_STATE_DEFAULT);
}

// ============================================================================
//  系统信息 / 时间 / 串口命令
// ============================================================================
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
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        Serial.println("Failed to obtain NTP time.");
        return;
    }
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("Current time: %s\n", timeStr);
}

void process_serial_command(const String& cmd_in) {
    String cmd = cmd_in;
    cmd.trim();
    if (cmd.startsWith("/")) cmd = cmd.substring(1);   // [FIX] 兼容带斜杠的写法
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
    else if (cmd == "help") {
        Serial.println("/dht11 profile start | stop | status");
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

// ============================================================================
//  初始化 / 主循环
// ============================================================================
void original_setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("LVGL Version: %d.%d.%d\n",
                  lv_version_major(), lv_version_minor(), lv_version_patch());

    bool psram_available = psramFound();
    if (psram_available) Serial.printf("PSRAM found, size: %d bytes\n", ESP.getPsramSize());
    else                 Serial.println("PSRAM not found, using internal RAM");

#if LV_USE_LOG != 0 && LV_LOG_LEVEL < LV_LOG_LEVEL_NONE
    // 只输出 LV_LOG_LEVEL 及以上等级的日志（默认 WARN）。
    // 以前 LVGL 的日志全被静默丢弃是因为没人注册回调；
    // 但注册之后若等级仍是 TRACE，就会把串口刷爆——等级已在 lv_conf.h 降到 WARN。
    lv_log_register_print_cb(my_print);
#endif

    // SD 已经在 main.cpp 的 setup() 里挂载过（为了先读 wifi.cfg），
    // 这里只做兜底，重复调用 SD.begin() 也不会炸。
    if (!sd_initialized) storage_mount();
    if (sd_initialized && file_names.empty()) read_directory("/");

    my_lcd.init();
    my_lcd.setRotation(1);
    my_lcd.fillScreen(TFT_BLACK);
    my_lcd.setSwapBytes(true);
    touch_init(my_lcd.width(), my_lcd.height(), my_lcd.getRotation());
    dht.begin();
    // DHT11 上电后需要约 1s 稳定
    delay(1100);
    last_dht_read_ms = 0;

    lv_init();
    delay(5);

    size_t buf_size;
    if (psram_available) {
        buf1 = (lv_color_t*)ps_malloc(FULL_BUF_SIZE * sizeof(lv_color_t));
        buf2 = (lv_color_t*)ps_malloc(FULL_BUF_SIZE * sizeof(lv_color_t));
        if (buf1 && buf2) {
            buf_size = FULL_BUF_SIZE;
            Serial.println("Using full-screen double buffer in PSRAM");
        } else {
            if (buf1) { free(buf1); buf1 = nullptr; }
            if (buf2) { free(buf2); buf2 = nullptr; }
            buf1 = (lv_color_t*)heap_caps_malloc(FALLBACK_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
            buf2 = (lv_color_t*)heap_caps_malloc(FALLBACK_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
            buf_size = FALLBACK_BUF_SIZE;
            Serial.println("PSRAM allocation failed, using fallback DMA buffer");
        }
    } else {
        buf1 = (lv_color_t*)heap_caps_malloc(FALLBACK_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
        buf2 = (lv_color_t*)heap_caps_malloc(FALLBACK_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
        buf_size = FALLBACK_BUF_SIZE;
        Serial.println("Using fallback DMA buffer (no PSRAM)");
    }

    // [FIX] 以前只检查 buf1，buf2 为 NULL 时照样往下走
    if (buf1 == nullptr || buf2 == nullptr) {
        Serial.println("WARN: second buffer missing, running single-buffered");
        if (buf2 == nullptr && buf1 != nullptr) { free(buf1); buf1 = nullptr; }
        buf1 = (lv_color_t*)heap_caps_malloc(FALLBACK_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
        if (buf1 == nullptr) {
            Serial.println("FATAL: Failed to allocate display buffer!");
            while (1) delay(10);
        }
        buf_size = FALLBACK_BUF_SIZE;
        lv_disp_draw_buf_init(&disp_draw_buf, buf1, NULL, buf_size);
    } else {
        lv_disp_draw_buf_init(&disp_draw_buf, buf1, buf2, buf_size);
    }

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ui_init() 内部已经调用过 ui_wifi_screen_init()，
    // [FIX 1] 这里以前又调了一次 → 整个 WiFi 屏被创建两遍、内存白扔
    ui_init();

    lv_obj_add_event_cb(ui_wifi_scan_btn, [](lv_event_t* e) {
        (void)e;
        wifi_scan_request();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_wifi_connect_btn, [](lv_event_t* e) {
        (void)e;
        wifi_connect_to_selected();
    }, LV_EVENT_CLICKED, NULL);

    // ---------------- 蓝牙(BLE)串口屏 ----------------
#if ENABLE_BLE
    // 这个屏不在 ui.c 里（避免被 SquareLine 重新生成时覆盖），这里手动建。
    ui_ble_screen_init();
    if (ui_ble_send_btn != NULL) {
        lv_obj_add_event_cb(ui_ble_send_btn, [](lv_event_t* e) {
            (void)e;
            ui_ble_on_send_requested();
        }, LV_EVENT_CLICKED, NULL);
    }
    // 【注意】这里以前还在 ui_Screen1 上额外建了一个浮动 "BLE" 按钮，
    // 但主菜单 (ui_Screen1.c) 已经有 "Bluetooth" 那一行菜单项了 ——
    // 两处入口重复，且浮动按钮还要跟别的控件抢位置。现已删除，
    // 统一从主菜单的 Bluetooth 行进入（那里会调用 ble_set_prev_screen()）。
#endif

    if (ui_dht11 != NULL) {
        lv_scr_load(ui_dht11);
        delay(100);
    }

    check_ui_controls();
    fix_dht11_label_display();
    lv_timer_create(dht_timer_cb, 2000, NULL);

    if (ui_file != NULL) {
        if (ui_refresh_btn != NULL) {
            lv_obj_add_event_cb(ui_refresh_btn, refresh_file_list_cb, LV_EVENT_CLICKED, NULL);
        }
        if (sd_initialized) {
            update_file_list_now();
        } else {
            update_file_list_now();      // 会显示 "SD Card not found"
        }
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

#if ENABLE_BLE
    // BLE 只在这里碰 LVGL：core1 的 BLE 任务只往缓冲里塞数据
    bleuart_ui_service();
#endif

    if (tx_buffer != nullptr) pump_txt_transfer();

    if (dht11_logging && sd_initialized) write_dht11_data_to_sd();

    delay(3);
}
