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
#define ILI9341_DRIVER       // 使用 ILI9341 驱动（根据您的屏幕实际修改）
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
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
#define SPI_FREQUENCY   80000000   // 40 MHz（如果稳定可使用 60/80 MHz）
#define SPI_READ_FREQUENCY  80000000
#define SPI_TOUCH_FREQUENCY  2500000

