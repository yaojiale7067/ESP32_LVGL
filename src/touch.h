#include <FT6336.h>

#define TOUCH_FT6336

// ========== I2C 引脚（适配 ESP32-S3 N16R8） ==========
// 原 ESP32 使用 SCL=25, SDA=32
// ESP32-S3 建议使用 SCL=1, SDA=2 （或其他空闲 I2C 引脚）
#define TOUCH_FT6336_SCL 1
#define TOUCH_FT6336_SDA 2

#define TOUCH_FT6336_INT -1
#define TOUCH_FT6336_RST 42   // 复位引脚可保留 33（若冲突可改为 42 或 5）

// 触摸映射范围（根据屏幕分辨率，保持不变）
#define TOUCH_MAP_X1 0
#define TOUCH_MAP_X2 240
#define TOUCH_MAP_Y1 0
#define TOUCH_MAP_Y2 320

int touch_last_x = 0, touch_last_y = 0;
unsigned short int width=0, height=0, rotation,min_x=0,max_x=0,min_y=0,max_y=0;

FT6336 ts = FT6336(TOUCH_FT6336_SDA, TOUCH_FT6336_SCL, TOUCH_FT6336_INT, TOUCH_FT6336_RST, max(TOUCH_MAP_X1, TOUCH_MAP_X2), max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

void touch_init(unsigned short int w, unsigned short int h,unsigned char r)
{
  width = w; 
  height = h;
  switch (r){
    case ROTATION_NORMAL:
    case ROTATION_INVERTED:
      min_x = TOUCH_MAP_X1;
      max_x = TOUCH_MAP_X2;
      min_y = TOUCH_MAP_Y1;
      max_y = TOUCH_MAP_Y2;
      break;
    case ROTATION_LEFT:
    case ROTATION_RIGHT:
      min_x = TOUCH_MAP_Y1;
      max_x = TOUCH_MAP_Y2;
      min_y = TOUCH_MAP_X1;
      max_y = TOUCH_MAP_X2;
      break;
    default:
      break;
  }
  ts.begin();
  ts.setRotation(r);
}

bool touch_touched(void)
{
   ts.read();
  if (ts.isTouched)
  {
    touch_last_x = map(ts.points[0].x, min_x, max_x, 0, width - 1);
    touch_last_y = map(ts.points[0].y, min_y, max_y, 0, height - 1);
    return true;
  }
  else
  {
    return false;
  }
}

bool touch_has_signal(void)
{
  return true;
}

bool touch_released(void)
{
  return true;
}