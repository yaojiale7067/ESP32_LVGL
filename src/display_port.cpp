// display_port.cpp - 显示驱动移植层实现
// 改这个文件之前，先读 display_port.h 顶部的约束（PSRAM / 条带 / full_refresh）。

#include "display_port.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

#include "touch.h"      // touch_init / touch_touched / touch_has_signal / touch_last_x/y

// TFT 与 SD 共用 HSPI（只有 CS 不同：TFT=10、SD=4），两份独立的 SPIClass(HSPI)
// 不会互相保护，必须靠这把递归锁串行化（SD 路径存在嵌套加锁）。
// 锁在静态初始化阶段创建，保证任务启动时一定存在。
SemaphoreHandle_t g_spiBusMutex = NULL;
namespace { struct SpiBusMutexInit {
    SpiBusMutexInit() { g_spiBusMutex = xSemaphoreCreateRecursiveMutex(); }
} spiBusMutexInit; }

static TFT_eSPI my_lcd = TFT_eSPI();

// 缓冲高度 = DISP_H / DISP_BUF_DIV。实测 1/4 屏撕裂反而比整屏更严重，保持 1。
#define DISP_BUF_DIV       1
#define DISP_FULL_BUF_SIZE (DISP_W * (DISP_H / DISP_BUF_DIV))
#define DISP_MIN_BUF_SIZE  (DISP_W * (DISP_H / 10))     // 无 PSRAM 时的退路

static lv_color_t*        s_buf1 = nullptr;
static lv_color_t*        s_buf2 = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;

// 若把 LV_LOG_LEVEL 调成 TRACE，这里会把 LVGL 内部的每次内存分配都打到串口。
#if LV_USE_LOG != 0 && LV_LOG_LEVEL < LV_LOG_LEVEL_NONE
void disp_lv_log_cb(const char* buf) { Serial.printf("%s", buf); }
#endif

// 刷屏回调：把 LVGL 渲染好的一块区域推进 LCD
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    // 必须和 SD 操作互斥，否则两个核会同时操作同一组 HSPI 寄存器
    DISP_BUS_LOCK();
    my_lcd.startWrite();
    my_lcd.setAddrWindow(area->x1, area->y1, w, h);
    my_lcd.pushColors((uint16_t*)&color_p->full, w * h, true);
    // endWrite() 会等完最后一段 SPI 事务，之后缓冲可安全复用
    my_lcd.endWrite();
    DISP_BUS_UNLOCK();

    lv_disp_flush_ready(drv);
}

static void disp_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
    (void)drv;
    if (touch_has_signal() && touch_touched()) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = touch_last_x;
        data->point.y = touch_last_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// 必须 PSRAM 优先：内部 RAM 要留给 BLE/WiFi
static bool disp_alloc_buffers(size_t* out_size)
{
    if (psramFound()) {
        s_buf1 = (lv_color_t*)heap_caps_malloc(DISP_FULL_BUF_SIZE * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM);
        s_buf2 = (lv_color_t*)heap_caps_malloc(DISP_FULL_BUF_SIZE * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM);
        if (s_buf1 && s_buf2) {
            *out_size = DISP_FULL_BUF_SIZE;
            Serial.printf("[LCD] full-screen double buffer in PSRAM, %u px each "
                          "(free heap %u, free psram %u)\n",
                          (unsigned)*out_size,
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
            return true;
        }
        if (s_buf1) { free(s_buf1); s_buf1 = nullptr; }
        if (s_buf2) { free(s_buf2); s_buf2 = nullptr; }
        Serial.println("[LCD] PSRAM alloc failed, falling back to internal RAM");
    } else {
        Serial.println("[LCD] no PSRAM found");
    }

    // 退路：仅在没有 PSRAM 时使用，且必须小块，否则会饿死 BLE/WiFi
    *out_size = DISP_MIN_BUF_SIZE;
    s_buf1 = (lv_color_t*)heap_caps_malloc(DISP_MIN_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_buf2 = (lv_color_t*)heap_caps_malloc(DISP_MIN_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    Serial.printf("[LCD] WARN internal DMA buffer %u px each (free heap %u) —— "
                  "条带变多，画面会有横向撕裂\n",
                  (unsigned)*out_size, (unsigned)ESP.getFreeHeap());
    return (s_buf1 != nullptr);
}

void disp_port_init_all(void)
{
    // 1. LCD 硬件
    my_lcd.init();
    my_lcd.setRotation(1);              // 横屏 320x240
    my_lcd.fillScreen(TFT_BLACK);
    my_lcd.setSwapBytes(true);

    // 2. 触摸（引脚定义在 touch.h，不要在这里改）
    touch_init(my_lcd.width(), my_lcd.height(), my_lcd.getRotation());

    // 3. LVGL 内核
    lv_init();
    delay(5);

    // 4. 显示缓冲
    size_t buf_size = 0;
    if (!disp_alloc_buffers(&buf_size)) {
        Serial.println("[LCD] FATAL: 1/10-screen buffer also failed, halting");
        while (1) delay(10);
    }
    if (s_buf2 == nullptr) {
        Serial.println("[LCD] WARN: only one buffer, running single-buffered");
        lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, buf_size);
    } else {
        lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_size);
    }

    // 5. 注册显示驱动
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = DISP_W;
    s_disp_drv.ver_res  = DISP_H;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    // 必须保持 0，否则撕裂更严重
    s_disp_drv.full_refresh = 0;
    lv_disp_drv_register(&s_disp_drv);

    // 6. 注册输入设备
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = disp_touch_read_cb;
    lv_indev_drv_register(&indev_drv);
}

void disp_fill_screen(uint16_t color)
{
    DISP_BUS_LOCK();
    my_lcd.fillScreen(color);
    DISP_BUS_UNLOCK();
}

void disp_push_image(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t* data)
{
    DISP_BUS_LOCK();
    my_lcd.pushImage(x, y, w, h, (uint16_t*)data);
    DISP_BUS_UNLOCK();
}

uint16_t disp_width(void)  { return (uint16_t)my_lcd.width(); }
uint16_t disp_height(void) { return (uint16_t)my_lcd.height(); }

// 业务层用这个判断触摸，不要直接 include touch.h
bool disp_touch_pressed(void)
{
    return (touch_has_signal() && touch_touched());
}

// TJpg_Decoder 输出回调：一次解码一行块就往屏幕推
bool disp_tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
    if (y >= my_lcd.height()) return false;
    DISP_BUS_LOCK();
    my_lcd.pushImage(x, y, w, h, bitmap);
    DISP_BUS_UNLOCK();
    return true;
}
