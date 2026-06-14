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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "touch.h"
#include "ui_test/ui.h"
#include "ui_test/screens/ui_wifi.h"
#include <esp_system.h>
#define DHTPIN  7
#define DHTTYPE DHT11
#define SD_CS   4

static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

#define FULL_BUF_SIZE (screenWidth * screenHeight)
#define FALLBACK_BUF_SIZE (screenWidth * screenHeight / 6)
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;
static lv_disp_draw_buf_t disp_draw_buf;
static lv_disp_drv_t disp_drv;

TFT_eSPI my_lcd = TFT_eSPI();
DHT dht(DHTPIN, DHTTYPE);
SPIClass sdSPI = SPIClass(HSPI);

static String current_path = "/";
static std::vector<String> file_names;
static std::vector<bool> file_is_dir;
static bool sd_initialized = false;
static bool showing_image = false;
static bool showing_editor = false;
static bool copy_mode = false;
static bool delete_mode = false;

static String copy_source_path = "";
static bool has_copied = false;

static lv_obj_t* newfile_container = NULL;
static lv_obj_t* newfile_textarea = NULL;
static lv_obj_t* newfile_keyboard = NULL;
static bool creating_new_file = false;

static lv_obj_t* editor_container = NULL;
static lv_obj_t* editor_textarea = NULL;
static lv_obj_t* editor_keyboard = NULL;
static String current_edit_path = "";

static bool dht11_logging = false;
static unsigned long last_log_time = 0;
static const unsigned long LOG_INTERVAL = 3000;

// 队列句柄声明
extern QueueHandle_t wifiConfigQueue;
extern QueueHandle_t wifiScanRequestQueue;
extern QueueHandle_t wifiScanResultQueue;

// 消息结构
typedef struct {
    char ssid[64];
    char password[64];
} WifiConfigMsg_t;

typedef struct {
    bool request;
} ScanRequestMsg_t;

typedef struct {
    int count;
    char ssids[50][64];
} ScanResultMsg_t;

static String selected_ssid = "";

void update_file_list();
void read_directory(const char* path);
void show_jpeg_image(const char* path);
void show_json_editor(const char* path);
void save_json_file(const char* path, const String& content);
void close_editor();
void send_txt_via_serial(const char* path);
void copy_file(const char* src_path);
void paste_file(const char* dest_dir);
void delete_file_or_folder(const char* path);
void start_dht11_logging();
void stop_dht11_logging();
void process_serial_command(String cmd);
void create_new_text_file();
void show_newfile_editor(const String& filename);
void close_newfile_editor();
void save_newfile_and_close(const String& content);
void print_system_info();
void get_ntp_time();

void wifi_save_config_to_sd(const char* ssid, const char* password);
bool wifi_load_config_from_sd(char* ssid, char* password, size_t ssid_size, size_t pwd_size);
void wifi_scan_networks();
void wifi_connect_to_selected();

#if USE_LV_LOG != 0
void my_print(const char* buf) {
    Serial.printf(buf);
    Serial.flush();
}
#endif

void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    my_lcd.startWrite();
    my_lcd.setAddrWindow(area->x1, area->y1, w, h);
    my_lcd.pushColors((uint16_t*)&color_p->full, w * h, true);
    my_lcd.endWrite();
    lv_disp_flush_ready(drv);
}

void my_touchpad_read(lv_indev_drv_t* indev_driver, lv_indev_data_t* data) {
    if (touch_has_signal()) {
        if (touch_touched()) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = touch_last_x;
            data->point.y = touch_last_y;
        } else if (touch_released()) {
            data->state = LV_INDEV_STATE_REL;
        }
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

void update_dht11_display() {
    static unsigned long last_read_time = 0;
    unsigned long now = millis();
    if (now - last_read_time < 2000) return;
    last_read_time = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, "--");
        if (ui_Labelrh != NULL) lv_label_set_text(ui_Labelrh, "--");
        return;
    }

    char temp_str[16], humi_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f", temperature);
    snprintf(humi_str, sizeof(humi_str), "%.1f", humidity);

    if (ui_Labeltemp != NULL) lv_label_set_text(ui_Labeltemp, temp_str);
    if (ui_Labelrh != NULL) lv_label_set_text(ui_Labelrh, humi_str);
}

void dht_timer_cb(lv_timer_t* timer) {
    update_dht11_display();
}

void fix_dht11_label_display() {
    if (ui_Labeltemp != NULL) {
        lv_obj_set_width(ui_Labeltemp, 100);
        lv_obj_set_height(ui_Labeltemp, 50);
        lv_obj_set_style_text_font(ui_Labeltemp, &lv_font_montserrat_28, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_Labeltemp, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        lv_obj_align(ui_Labeltemp, LV_ALIGN_CENTER, -30, -47);
        lv_label_set_text(ui_Labeltemp, "25.5");
    }
    if (ui_Labelrh != NULL) {
        lv_obj_set_width(ui_Labelrh, 100);
        lv_obj_set_height(ui_Labelrh, 50);
        lv_obj_set_style_text_font(ui_Labelrh, &lv_font_montserrat_28, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_Labelrh, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        lv_obj_align(ui_Labelrh, LV_ALIGN_CENTER, 105, -47);
        lv_label_set_text(ui_Labelrh, "60.0");
    }
}

bool init_dht11_log_file() {
    if (!SD.exists("/dht11.json")) {
        File file = SD.open("/dht11.json", FILE_WRITE);
        if (!file) {
            Serial.println("Failed to create dht11.json");
            return false;
        }
        file.println("[");
        file.close();
        Serial.println("Created dht11.json");
    }
    return true;
}

void write_dht11_data_to_sd() {
    if (!dht11_logging) return;
    unsigned long now = millis();
    if (now - last_log_time < LOG_INTERVAL) return;
    last_log_time = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("DHT11 read failed, cannot log!");
        return;
    }

    File file = SD.open("/dht11.json", FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open dht11.json");
        return;
    }

    unsigned long timestamp = now / 1000;
    file.printf("  {\"timestamp\":%lu,\"temp\":%.1f,\"humi\":%.1f},\n", timestamp, temperature, humidity);
    file.close();
    Serial.printf("Logged: %.1fC, %.1f%%\n", temperature, humidity);
}

void stop_dht11_logging() {
    if (!dht11_logging) return;
    dht11_logging = false;

    File file = SD.open("/dht11.json", FILE_READ);
    if (!file) {
        Serial.println("Failed to open dht11.json");
        return;
    }
    String content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();

    if (content.endsWith(",\n")) {
        content = content.substring(0, content.length() - 2);
        content += "\n";
    } else if (content.endsWith(",")) {
        content = content.substring(0, content.length() - 1);
    }
    content += "]";

    file = SD.open("/dht11.json", FILE_WRITE);
    if (file) {
        file.print(content);
        file.close();
    }
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

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= my_lcd.height()) return 0;
    my_lcd.pushImage(x, y, w, h, bitmap);
    return 1;
}

bool is_jpeg_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

bool is_json_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".json");
}

bool is_txt_file(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".txt");
}

void delete_file_or_folder(const char* path) {
    String path_str = String(path);
    File f = SD.open(path);
    bool is_directory = f.isDirectory();
    f.close();

    if (is_directory) {
        File dir = SD.open(path);
        if (!dir) {
            Serial.println("Failed to open directory: " + path_str);
            return;
        }
        File file = dir.openNextFile();
        while (file) {
            String child_path;
            if (path_str == "/") child_path = "/" + String(file.name());
            else child_path = path_str + "/" + String(file.name());

            if (file.isDirectory()) delete_file_or_folder(child_path.c_str());
            else {
                SD.remove(child_path);
                Serial.println("Deleted file: " + child_path);
            }
            file = dir.openNextFile();
        }
        dir.close();
        SD.rmdir(path);
        Serial.println("Deleted directory: " + path_str);
    } else {
        if (SD.remove(path)) Serial.println("Deleted file: " + path_str);
        else Serial.println("Failed to delete: " + path_str);
    }

    read_directory(current_path.c_str());
    update_file_list();

    if (ui_status_label != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Deleted: %s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
        lv_label_set_text(ui_status_label, msg);
        lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
    }
}

int get_next_file_number() {
    int max_num = 0;
    for (size_t i = 0; i < file_names.size(); i++) {
        String name = file_names[i];
        if (name.startsWith("newtext") && name.endsWith(".txt")) {
            String numStr = name.substring(7, name.length() - 4);
            int num = numStr.toInt();
            if (num > max_num) max_num = num;
        }
    }
    return max_num + 1;
}

void create_new_text_file() {
    if (creating_new_file) return;
    int nextNum = get_next_file_number();
    String filename = "newtext" + String(nextNum) + ".txt";
    String full_path;
    if (current_path == "/") full_path = "/" + filename;
    else full_path = current_path + "/" + filename;

    File file = SD.open(full_path.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("Failed to create file: " + full_path);
        if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "Failed to create file!");
        return;
    }
    file.close();
    Serial.println("Created file: " + full_path);
    read_directory(current_path.c_str());
    update_file_list();
    show_newfile_editor(full_path);
}

void close_newfile_editor() {
    if (newfile_keyboard != NULL) lv_obj_del(newfile_keyboard);
    if (newfile_container != NULL) lv_obj_del(newfile_container);
    newfile_textarea = NULL;
    creating_new_file = false;
    lv_refr_now(NULL);
    read_directory(current_path.c_str());
    update_file_list();
}

void save_newfile_and_close(const String& content) {
    if (newfile_textarea != NULL) {
        const char* text = lv_textarea_get_text(newfile_textarea);
        if (strlen(text) > 0) {
            File file = SD.open(current_edit_path.c_str(), FILE_WRITE);
            if (file) {
                file.print(text);
                file.close();
                Serial.println("Saved new file: " + current_edit_path);
                if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "File saved!");
            } else {
                Serial.println("Failed to save: " + current_edit_path);
            }
        }
    }
    close_newfile_editor();
}

void newfile_kb_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        if (newfile_textarea != NULL) {
            const char* text = lv_textarea_get_text(newfile_textarea);
            File file = SD.open(current_edit_path.c_str(), FILE_WRITE);
            if (file) {
                file.print(text);
                file.close();
                Serial.println("Saved: " + current_edit_path);
                if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "File saved!");
            }
        }
        close_newfile_editor();
    }
}

void show_newfile_editor(const String& path) {
    if (creating_new_file) return;
    creating_new_file = true;
    current_edit_path = path;

    newfile_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(newfile_container, screenWidth, screenHeight);
    lv_obj_set_pos(newfile_container, 0, 0);
    lv_obj_set_style_bg_color(newfile_container, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(newfile_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(newfile_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(newfile_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(newfile_container);
    lv_obj_set_size(title_bar, screenWidth, 35);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
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

    newfile_textarea = lv_textarea_create(newfile_container);
    lv_obj_set_size(newfile_textarea, screenWidth - 10, screenHeight - 50);
    lv_obj_set_pos(newfile_textarea, 5, 40);
    lv_textarea_set_placeholder_text(newfile_textarea, "Enter text here... Press Enter to save");
    lv_textarea_set_cursor_click_pos(newfile_textarea, true);
    lv_obj_set_style_bg_color(newfile_textarea, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(newfile_textarea, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(newfile_textarea, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(newfile_textarea, lv_color_hex(0x555555), LV_STATE_DEFAULT);

    newfile_keyboard = lv_keyboard_create(newfile_container);
    lv_keyboard_set_textarea(newfile_keyboard, newfile_textarea);
    lv_obj_set_size(newfile_keyboard, screenWidth, 100);
    lv_obj_align(newfile_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(newfile_keyboard, newfile_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) { close_newfile_editor(); }, LV_EVENT_CLICKED, NULL);
    lv_refr_now(NULL);
}

void copy_file(const char* src_path) {
    copy_source_path = String(src_path);
    has_copied = true;
    Serial.printf("Copied: %s\n", src_path);
    if (ui_status_label != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Copied: %s", strrchr(src_path, '/') ? strrchr(src_path, '/') + 1 : src_path);
        lv_label_set_text(ui_status_label, msg);
        lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0xFFFF88), LV_STATE_DEFAULT);
    }
}

void paste_file(const char* dest_dir) {
    if (!has_copied) {
        Serial.println("Nothing to paste. Copy a file first.");
        if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "Nothing to paste. Copy a file first.");
        return;
    }

    String filename = copy_source_path.substring(copy_source_path.lastIndexOf('/') + 1);
    String dest_path;
    if (String(dest_dir) == "/") dest_path = "/" + filename;
    else dest_path = String(dest_dir) + "/" + filename;

    if (SD.exists(dest_path)) {
        Serial.printf("Target file already exists: %s\n", dest_path.c_str());
        if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "File exists! Copy cancelled.");
        has_copied = false;
        return;
    }

    File src = SD.open(copy_source_path);
    if (!src) {
        Serial.printf("Failed to open source: %s\n", copy_source_path.c_str());
        if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "Failed to open source file");
        has_copied = false;
        return;
    }

    File dest = SD.open(dest_path, FILE_WRITE);
    if (!dest) {
        Serial.printf("Failed to create destination: %s\n", dest_path.c_str());
        src.close();
        if (ui_status_label != NULL) lv_label_set_text(ui_status_label, "Failed to create destination file");
        has_copied = false;
        return;
    }

    uint8_t buffer[512];
    size_t total = 0;
    while (src.available()) {
        size_t bytes = src.read(buffer, sizeof(buffer));
        dest.write(buffer, bytes);
        total += bytes;
    }
    src.close();
    dest.close();

    Serial.printf("Copied '%s' to '%s' (%d bytes)\n", copy_source_path.c_str(), dest_path.c_str(), total);
    if (ui_status_label != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Pasted: %s (%d bytes)", filename.c_str(), total);
        lv_label_set_text(ui_status_label, msg);
        lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    }

    has_copied = false;
    copy_source_path = "";
    read_directory(current_path.c_str());
    update_file_list();
}

void send_txt_via_serial(const char* path) {
    Serial.println("\n========================================");
    Serial.println("SENDING TXT FILE VIA SERIAL");
    Serial.println("========================================");
    Serial.printf("File: %s\n", path);
    Serial.println("----------------------------------------");

    File file = SD.open(path);
    if (!file) {
        Serial.println("ERROR: Cannot open file!");
        return;
    }

    size_t total_bytes = 0;
    int line_count = 0;
    String line;
    while (file.available()) {
        line = file.readStringUntil('\n');
        total_bytes += line.length() + 1;
        line_count++;
        Serial.printf("[%04d] %s\n", line_count, line.c_str());
        delay(2);
    }
    file.close();

    Serial.println("----------------------------------------");
    Serial.printf("Total lines: %d\n", line_count);
    Serial.printf("Total bytes: %d\n", total_bytes);
    Serial.println("========================================");
    Serial.println("TXT file sent successfully!");
    Serial.println("========================================\n");

    if (ui_status_label != NULL) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Sent: %s (%d bytes)", path, total_bytes);
        lv_label_set_text(ui_status_label, msg);
    }
}

void show_jpeg_image(const char* path) {
    showing_image = true;
    my_lcd.fillScreen(TFT_BLACK);
    uint16_t w = 0, h = 0;
    TJpgDec.getSdJpgSize(&w, &h, path);
    int16_t x = (screenWidth - w) / 2;
    int16_t y = (screenHeight - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    TJpgDec.setCallback(tft_output);
    TJpgDec.setJpgScale(1);
    my_lcd.setSwapBytes(true);
    TJpgDec.drawSdJpg(x, y, path);
    while (true) {
        if (touch_has_signal() && touch_touched()) break;
        delay(50);
    }
    my_lcd.fillScreen(TFT_BLACK);
    showing_image = false;
    lv_refr_now(NULL);
}

String read_json_file(const char* path) {
    File file = SD.open(path);
    if (!file) return "Error: Cannot open file";
    String content = "";
    while (file.available()) {
        content += (char)file.read();
        if (content.length() > 4000) {
            content += "\n... (truncated)";
            break;
        }
    }
    file.close();
    if (content.length() == 0) return "(Empty file)";
    return content;
}

void save_json_file(const char* path, const String& content) {
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to save: " + String(path));
        return;
    }
    file.print(content);
    file.close();
    Serial.println("Saved: " + String(path));
}

void close_editor() {
    if (editor_keyboard != NULL) lv_obj_del(editor_keyboard);
    if (editor_container != NULL) lv_obj_del(editor_container);
    editor_textarea = NULL;
    showing_editor = false;
    lv_refr_now(NULL);
}

void save_and_close_cb(lv_event_t* e) {
    if (editor_textarea != NULL && current_edit_path.length() > 0) {
        const char* content = lv_textarea_get_text(editor_textarea);
        save_json_file(current_edit_path.c_str(), String(content));
    }
    close_editor();
}

void show_json_editor(const char* path) {
    if (showing_editor) return;
    showing_editor = true;
    current_edit_path = String(path);
    String content = read_json_file(path);

    editor_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(editor_container, screenWidth, screenHeight);
    lv_obj_set_pos(editor_container, 0, 0);
    lv_obj_set_style_bg_color(editor_container, lv_color_hex(0x1E1E1E), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(editor_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor_container, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(editor_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(editor_container);
    lv_obj_set_size(title_bar, screenWidth, 35);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "JSON Editor");
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t* save_btn = lv_btn_create(title_bar);
    lv_obj_set_size(save_btn, 50, 28);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -65, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_set_style_text_color(save_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(save_btn, save_and_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 50, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) { close_editor(); }, LV_EVENT_CLICKED, NULL);

    editor_textarea = lv_textarea_create(editor_container);
    lv_obj_set_size(editor_textarea, screenWidth - 10, 100);
    lv_obj_set_pos(editor_textarea, 5, 45);
    lv_textarea_set_text(editor_textarea, content.c_str());
    lv_textarea_set_placeholder_text(editor_textarea, "Edit JSON content...");
    lv_obj_set_style_bg_color(editor_textarea, lv_color_hex(0x2D2D2D), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editor_textarea, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(editor_textarea, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(editor_textarea, lv_color_hex(0x555555), LV_STATE_DEFAULT);

    editor_keyboard = lv_keyboard_create(editor_container);
    lv_keyboard_set_textarea(editor_keyboard, editor_textarea);
    lv_obj_set_size(editor_keyboard, screenWidth, 100);
    lv_obj_align(editor_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_refr_now(NULL);
}

bool init_sd_card() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    sdSPI.begin(12, 13, 11, SD_CS);
    delay(10);
    if (!SD.begin(SD_CS, sdSPI)) return false;
    return true;
}

void read_directory(const char* path) {
    if (!sd_initialized) return;
    file_names.clear();
    file_is_dir.clear();
    File root = SD.open(path);
    if (!root) return;
    if (!root.isDirectory()) {
        root.close();
        return;
    }
    File file = root.openNextFile();
    while (file) {
        file_names.push_back(String(file.name()));
        file_is_dir.push_back(file.isDirectory());
        file = root.openNextFile();
    }
    root.close();
}

void update_file_list() {
    if (ui_file_list == NULL) return;
    lv_obj_clean(ui_file_list);
    if (!sd_initialized) {
        lv_obj_t* label = lv_label_create(ui_file_list);
        lv_label_set_text(label, "SD Card not found");
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
    lv_obj_add_event_cb(new_btn, [](lv_event_t* e) { create_new_text_file(); }, LV_EVENT_CLICKED, NULL);

    const char* mode_text;
    if (copy_mode) mode_text = " Mode: Copy (Tap file to copy)";
    else if (delete_mode) mode_text = " Mode: Delete (Tap file/folder to delete)";
    else mode_text = "Mode: Normal (Tap to open)";

    lv_obj_t* mode_btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, mode_text);
    if (copy_mode) lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
    else if (delete_mode) lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
    else lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x3D3D3D), LV_STATE_DEFAULT);

    lv_obj_add_event_cb(mode_btn, [](lv_event_t* e) {
        if (!copy_mode && !delete_mode) { copy_mode = true; delete_mode = false; }
        else if (copy_mode && !delete_mode) { copy_mode = false; delete_mode = true; }
        else if (!copy_mode && delete_mode) { copy_mode = false; delete_mode = false; }
        update_file_list();
    }, LV_EVENT_CLICKED, NULL);

    if (has_copied) {
        lv_obj_t* paste_btn = lv_list_add_btn(list, LV_SYMBOL_DOWN, "📋 Paste Here");
        lv_obj_set_style_bg_color(paste_btn, lv_color_hex(0x44AA44), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(paste_btn, [](lv_event_t* e) { paste_file(current_path.c_str()); }, LV_EVENT_CLICKED, NULL);

        lv_obj_t* clear_btn = lv_list_add_btn(list, LV_SYMBOL_CLOSE, "✖ Clear Copy");
        lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xFF5555), LV_STATE_DEFAULT);
        lv_obj_add_event_cb(clear_btn, [](lv_event_t* e) { has_copied = false; copy_source_path = ""; update_file_list(); }, LV_EVENT_CLICKED, NULL);
    }

    if (current_path != "/") {
        lv_obj_t* back_btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, ".. (Parent)");
        lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
            if (current_path == "/") return;
            int last_slash = current_path.lastIndexOf('/');
            if (last_slash == 0) current_path = "/";
            else current_path = current_path.substring(0, last_slash);
            read_directory(current_path.c_str());
            update_file_list();
        }, LV_EVENT_CLICKED, NULL);
    }

    for (size_t i = 0; i < file_names.size(); i++) {
        String name = file_names[i];
        bool is_dir_item = file_is_dir[i];
        const char* symbol = is_dir_item ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        String display_name = name;
        if (!is_dir_item) {
            if (is_jpeg_file(name)) display_name = name + " [IMG]";
            else if (is_json_file(name)) display_name = name + " [EDIT]";
            else if (is_txt_file(name)) display_name = name + " [SEND]";
        }
        lv_obj_t* item_btn = lv_list_add_btn(list, symbol, display_name.c_str());
        lv_obj_set_user_data(item_btn, (void*)(intptr_t)i);
        if (copy_mode && !is_dir_item) lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x336633), LV_STATE_DEFAULT);
        else if (delete_mode) lv_obj_set_style_bg_color(item_btn, lv_color_hex(0x663333), LV_STATE_DEFAULT);

        lv_obj_add_event_cb(item_btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx < 0 || idx >= (int)file_names.size()) return;
            if (delete_mode) {
                String full_path;
                if (current_path == "/") full_path = "/" + file_names[idx];
                else full_path = current_path + "/" + file_names[idx];
                delete_file_or_folder(full_path.c_str());
                delete_mode = false;
                return;
            }
            if (copy_mode && !file_is_dir[idx]) {
                String full_path;
                if (current_path == "/") full_path = "/" + file_names[idx];
                else full_path = current_path + "/" + file_names[idx];
                copy_file(full_path.c_str());
                copy_mode = false;
                update_file_list();
                return;
            }
            if (file_is_dir[idx]) {
                if (current_path == "/") current_path = "/" + file_names[idx];
                else current_path = current_path + "/" + file_names[idx];
                read_directory(current_path.c_str());
                update_file_list();
            } else {
                String full_path;
                if (current_path == "/") full_path = "/" + file_names[idx];
                else full_path = current_path + "/" + file_names[idx];
                if (is_jpeg_file(file_names[idx])) {
                    show_jpeg_image(full_path.c_str());
                    update_file_list();
                } else if (is_json_file(file_names[idx])) {
                    show_json_editor(full_path.c_str());
                } else if (is_txt_file(file_names[idx])) {
                    send_txt_via_serial(full_path.c_str());
                }
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    if (file_names.size() == 0 && current_path == "/") {
        lv_obj_t* hint_label = lv_label_create(list);
        lv_label_set_text(hint_label, "No files found\nTap 'New Text File' to create\nTap 'Mode' to switch modes");
        lv_obj_center(hint_label);
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0xAAAAAA), LV_STATE_DEFAULT);
    } else if (file_names.size() == 0) {
        lv_obj_t* empty_label = lv_label_create(list);
        lv_label_set_text(empty_label, "Empty directory");
        lv_obj_center(empty_label);
    }
}

void refresh_file_list_cb(lv_event_t* e) {
    if (sd_initialized) {
        read_directory(current_path.c_str());
        update_file_list();
    }
}
void wifi_save_config_to_sd(const char* ssid, const char* password) {
    File f = SD.open("/wifi.cfg", FILE_WRITE);
    if (!f) {
        Serial.println("Failed to save wifi.cfg");
        return;
    }
    f.println(ssid);
    f.println(password);
    f.close();
    Serial.println("WiFi config saved to /wifi.cfg");
}

bool wifi_load_config_from_sd(char* ssid, char* password, size_t ssid_size, size_t pwd_size) {
    if (!SD.exists("/wifi.cfg")) {
        Serial.println("wifi.cfg not found");
        return false;
    }
    File f = SD.open("/wifi.cfg", FILE_READ);
    if (!f) return false;
    String line1 = f.readStringUntil('\n');
    String line2 = f.readStringUntil('\n');
    f.close();
    line1.trim();
    line2.trim();
    if (line1.length() == 0 || line2.length() == 0) return false;
    strncpy(ssid, line1.c_str(), ssid_size - 1);
    strncpy(password, line2.c_str(), pwd_size - 1);
    return true;
}

void wifi_scan_networks() {
    if (!wifiScanRequestQueue || !wifiScanResultQueue) {
        Serial.println("WiFi queues not ready");
        return;
    }
    ScanRequestMsg_t req = { true };
    if (xQueueSend(wifiScanRequestQueue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        lv_label_set_text(ui_wifi_status_label, "Scan request failed");
        return;
    }
    lv_label_set_text(ui_wifi_status_label, "Scanning...");
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFFFF00), LV_STATE_DEFAULT);

    ScanResultMsg_t result;
    if (xQueueReceive(wifiScanResultQueue, &result, pdMS_TO_TICKS(50000)) != pdTRUE) {
        lv_label_set_text(ui_wifi_status_label, "Scan timeout");
        lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    if (result.count == 0) {
        lv_label_set_text(ui_wifi_status_label, "No networks found");
        lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }

    if (ui_wifi_list) lv_obj_clean(ui_wifi_list);
    for (int i = 0; i < result.count; i++) {
        lv_obj_t* btn = lv_list_add_btn(ui_wifi_list, LV_SYMBOL_WIFI, result.ssids[i]);
        char* ssid_copy = (char*)malloc(strlen(result.ssids[i]) + 1);
        strcpy(ssid_copy, result.ssids[i]);
        lv_obj_set_user_data(btn, ssid_copy);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            const char* ssid = (const char*)lv_obj_get_user_data(btn);
            selected_ssid = String(ssid);
            lv_label_set_text(ui_wifi_status_label, ssid);
            lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        }, LV_EVENT_CLICKED, NULL);
    }
    lv_label_set_text(ui_wifi_status_label, "Scan complete. Select network, enter password.");
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
}

void wifi_connect_to_selected() {
    if (selected_ssid.length() == 0) {
        lv_label_set_text(ui_wifi_status_label, "No network selected");
        lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    const char* password = ui_wifi_password_ta ? lv_textarea_get_text(ui_wifi_password_ta) : "";
    if (strlen(password) == 0) {
        lv_label_set_text(ui_wifi_status_label, "Password cannot be empty");
        lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    lv_label_set_text(ui_wifi_status_label, "Connecting...");
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFFFF00), LV_STATE_DEFAULT);

    WifiConfigMsg_t cfg;
    strncpy(cfg.ssid, selected_ssid.c_str(), sizeof(cfg.ssid)-1);
    strncpy(cfg.password, password, sizeof(cfg.password)-1);
    if (xQueueSend(wifiConfigQueue, &cfg, pdMS_TO_TICKS(500)) != pdTRUE) {
        lv_label_set_text(ui_wifi_status_label, "Failed to send config");
        lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0xFF8888), LV_STATE_DEFAULT);
        return;
    }
    wifi_save_config_to_sd(cfg.ssid, cfg.password);
    lv_label_set_text(ui_wifi_status_label, "Config saved, connecting in background...");
    lv_obj_set_style_text_color(ui_wifi_status_label, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    if (ui_wifi_password_ta) lv_textarea_set_text(ui_wifi_password_ta, "");
}

void print_system_info() {
    Serial.println("\n========================================");
    Serial.println("            SYSTEM INFO");
    Serial.println("========================================");
    Serial.printf("  Chip:        %s\n", ESP.getChipModel());
    Serial.printf("  Cores:       %d\n", ESP.getChipCores());
    Serial.printf("  Frequency:   %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Flash:       %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    if (psramFound()) {
        Serial.printf("  PSRAM:       %d MB (Free: %d KB)\n", ESP.getPsramSize() / (1024 * 1024), ESP.getFreePsram() / 1024);
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

void process_serial_command(String cmd) {
    cmd.trim();
    if (cmd == "/dht11 profile start") {
        start_dht11_logging();
    }
    else if (cmd == "/dht11 profile stop") {
        stop_dht11_logging();
    }
    else if (cmd == "/dht11 status") {
        if (dht11_logging) Serial.println("DHT11 logging is ACTIVE.");
        else Serial.println("DHT11 logging is STOPPED.");
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
    else {
        if (ui_labelrx != NULL) lv_label_set_text(ui_labelrx, cmd.c_str());
    }
}

void check_ui_controls() {
    Serial.println("=== UI Controls Check ===");
    Serial.printf("ui_dht11: %p\n", ui_dht11);
    Serial.printf("ui_Labeltemp: %p\n", ui_Labeltemp);
    Serial.printf("ui_Labelrh: %p\n", ui_Labelrh);
    Serial.printf("ui_file: %p\n", ui_file);
    Serial.println("Commands:");
    Serial.println("  /dht11 profile start $ stop $ status");
    Serial.println("  fetch   - Print system info");
    Serial.println("  date    - Get NTP time (if WiFi connected)");
    Serial.println("  reboot  - Reboot the FreeRTOS");
}

void original_setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("LVGL Version: %d.%d.%d\n", lv_version_major(), lv_version_minor(), lv_version_patch());

    bool psram_available = psramFound();
    if (psram_available) {
        Serial.printf("PSRAM found, size: %d bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("PSRAM not found, using internal RAM");
    }

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    sdSPI.begin(12, 13, 11, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) {
        sd_initialized = false;
        Serial.println("SD Card Mount Failed!");
    } else {
        sd_initialized = true;
        read_directory("/");
        TJpgDec.setCallback(tft_output);
        TJpgDec.setJpgScale(1);
    }

    my_lcd.init();
    my_lcd.setRotation(1);
    my_lcd.fillScreen(TFT_BLACK);
    my_lcd.setSwapBytes(true);
    touch_init(my_lcd.width(), my_lcd.height(), my_lcd.getRotation());
    dht.begin();

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
            if (buf1) free(buf1);
            if (buf2) free(buf2);
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
    if (!buf1 || !buf2) {
        Serial.println("FATAL: Failed to allocate display buffers!");
        while (1) delay(10);
    }
    lv_disp_draw_buf_init(&disp_draw_buf, buf1, buf2, buf_size);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();

    ui_wifi_screen_init();

    
    lv_obj_add_event_cb(ui_wifi_scan_btn, [](lv_event_t* e) { wifi_scan_networks(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_wifi_connect_btn, [](lv_event_t* e) { wifi_connect_to_selected(); }, LV_EVENT_CLICKED, NULL);

    if (ui_dht11 != NULL) {
        lv_scr_load(ui_dht11);
        delay(100);
    }

    check_ui_controls();
    fix_dht11_label_display();
    lv_timer_create(dht_timer_cb, 1500, NULL);

    if (ui_file != NULL && sd_initialized) {
        update_file_list();
        if (ui_refresh_btn != NULL) {
            lv_obj_add_event_cb(ui_refresh_btn, refresh_file_list_cb, LV_EVENT_CLICKED, NULL);
        }
    } else if (ui_file != NULL && !sd_initialized) {
        if (ui_file_list != NULL) {
            lv_obj_clean(ui_file_list);
            lv_obj_t* label = lv_label_create(ui_file_list);
            lv_label_set_text(label, "No SD Card");
            lv_obj_center(label);
        }
    }

    delay(100);
    update_dht11_display();
}

void original_loop() {
    lv_timer_handler();
    delay(3);
    if (Serial.available() > 0) {
        String rx = Serial.readStringUntil('\n');
        process_serial_command(rx);
    }
    if (dht11_logging && sd_initialized) {
        write_dht11_data_to_sd();
    }
}