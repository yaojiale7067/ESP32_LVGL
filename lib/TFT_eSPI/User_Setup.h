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
//                            USER DEFINED SETTINGS
//   Set driver type, fonts to be loaded, pins used and SPI control method etc.

// ##################################################################################
// Section 1. 驱动选择（保持不变）
// ##################################################################################

#define ILI9341_DRIVER       // 使用 ILI9341 驱动（根据您的屏幕实际修改）

// ##################################################################################
// Section 2. 引脚定义（适配 ESP32-S3 N16R8）
// ##################################################################################

// 背光控制引脚（保持不变，通用 GPIO）
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// 硬件 SPI 引脚映射（使用 SPI2，原 HSPI 对应 SPI2）
#define TFT_MISO 13   // 原 ESP32 为 12，ESP32-S3 可用 13
#define TFT_MOSI 11   // 原 ESP32 为 13，ESP32-S3 可用 11
#define TFT_SCLK 12   // 原 ESP32 为 14，ESP32-S3 可用 12
#define TFT_CS   10   // 原 ESP32 为 15，ESP32-S3 可用 10
#define TFT_DC    9   // 原 ESP32 为 2， ESP32-S3 可用 9
#define TFT_RST   8   // 原 ESP32 为 27，ESP32-S3 可用 8

// 注意：如果您的开发板将某些引脚用于其他功能（如 PSRAM、USB），请根据实际调整。
// 建议使用以下引脚组合作为备用：
//   TFT_MISO 13, TFT_MOSI 11, TFT_SCLK 12, TFT_CS 10, TFT_DC 9, TFT_RST 8, TFT_BL 21

// 启用 HSPI 端口（在 ESP32-S3 中对应 SPI2）
#define USE_HSPI_PORT

// ##################################################################################
// Section 3. 字体（保持不变）
// ##################################################################################

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

// ##################################################################################
// Section 4. 其他选项（保持不变）
// ##################################################################################

#define SPI_FREQUENCY   8000000
#define SPI_READ_FREQUENCY  80000000
#define SPI_TOUCH_FREQUENCY  2500000

// 保持原有注释风格，只改了引脚定义