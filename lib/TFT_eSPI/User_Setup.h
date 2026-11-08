//                            USER DEFINED SETTINGS
//   Set driver type, fonts to be loaded, pins used and SPI control method etc.
//
//   See the User_Setup_Select.h file if you wish to be able to define multiple
//   setups and then easily select which setup file is used by the compiler.
//
//   If this file is edited correctly then all the library example sketches should
//   run without the need to make any more changes for a particular hardware setup!
//   Note that some sketches are designed for a particular TFT pixel width/height

// User defined information reported by "Read_User_Setup" test & diagnostics example
#define USER_SETUP_INFO "User_Setup"
#define ILI9341_DRIVER       
#define TFT_BACKLIGHT_ON HIGH
#define TFT_BL   21
#define TFT_MISO 13   // 原 ESP32 为 12，ESP32-S3 可用 13
#define TFT_MOSI 11   // 原 ESP32 为 13，ESP32-S3 可用 11
#define TFT_SCLK 12   // 原 ESP32 为 14，ESP32-S3 可用 12
#define TFT_CS   10   // 原 ESP32 为 15，ESP32-S3 可用 10
#define TFT_DC    9   // 原 ESP32 为 2， ESP32-S3 可用 9
#define TFT_RST   8   // 原 ESP32 为 27，ESP32-S3 可用 8
#define USE_DMA_TRANSFERS   1   // 启用 DMA
#define USE_HSPI_PORT
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
// ★ 60MHz，不是 80MHz。
//   症状：切屏/返回时，屏幕上会**随机位置**出现一小块错位的画面（比如某行的
//   图标旁边多出一条同色的竖条），有时正常。稳态时几乎不出现。
//   原因：那是一条被偏移复制的画面碎片 —— SPI 长突发传输中途丢了/错了位。
//   稳态每次只推几百~几千字节，出错概率低；切屏要一口气推 153600 字节
//   （整屏 320x240x16bpp），字节越多越容易在中途出错，错在哪随机。
//   80MHz 走排线/面包板本来就贴着临界点，降到 60MHz 换取时序余量。
//
//   注意这和"撕裂"不冲突：整屏推送时间 = 153600*8/60e6 ≈ 20.5ms，
//   仍小于面板帧周期 28.5ms（DISP_FRAME_DIV=1, RTNA=0x1B），
//   所以之前定好的防撕裂参数依然成立，不用动 display_port.cpp。
#define SPI_FREQUENCY   60000000
#define SPI_READ_FREQUENCY  60000000
#define SPI_TOUCH_FREQUENCY  2500000

