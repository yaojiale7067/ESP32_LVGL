# ESP32-S3 LVGL 多功能面板

作者：YJL_SAMA

一块 320×240 的触摸小面板，上面跑 FreeRTOS + LVGL，把温湿度、文件管理、串口调试、WiFi/BLE、继电器、灯带、以及三套无线观察工具塞进了同一个界面。全部代码跑在 ESP32-S3(N16R8) 上，屏幕刷新 35Hz，无撕裂。

> 原有 README 已备份为 `README.orig.md`。

---

## 一、功能

| 屏幕 | 说明 |
|---|---|
| **温湿度** | DHT11 实时读数 + 片内温度；可把采样写成 `/dht11.json` |
| **文件管理** | 浏览 SD 卡、查看/编辑 txt 与 json、删除、发送文本到串口 |
| **串口工具** | 屏幕键盘输入 → UART0；收到的数据回显在屏上 |
| **WiFi** | 扫描周围 AP、连接、显示 IP；另有一屏 13 信道的占用分析与最优信道推荐 |
| **蓝牙** | BLE UART(NUS)，手机可连；收发都显示在屏上 |
| **继电器** | GPIO41 通断控制，带状态缓存 |
| **灯带** | WS2812 色相/亮度/速度/灯数，屏幕数字键盘输入 |
| **RF Monitor** | 802.11 管理帧被动监听：deauth 溯源、AP 表、克隆 SSID 检测 |
| **nRF24** | 2.4GHz 私有协议嗅探 + RPD 频谱 |
| **CC1101** | Sub-GHz 扫频（真实 RSSI）+ 定频收包 |

三个无线工具**全部只接收、不发射**——它们不会干扰或欺骗任何第三方设备，这是本项目的硬边界。

---

## 二、硬件与引脚

主控：**ESP32-S3-DevKitC-1（N16R8：16MB Flash + 8MB Octal PSRAM）**

### 显示与触摸

| 信号 | GPIO |
|---|---|
| TFT RST / DC / CS | 8 / 9 / 10 |
| TFT MOSI / SCK / MISO | 11 / 12 / 13 |
| 触摸 SDA / SCL（I2C，FT6336） | 2 / 1 |
| 触摸 RST | 42 |

### 外设

| 设备 | GPIO | 备注 |
|---|---|---|
| SD 卡 CS | 4 | 与 TFT 共用 HSPI，靠互斥锁串行化 |
| DHT11 数据 | 7 | 单总线，软件位时序 |
| 继电器 | 41 | 高电平吸合 |
| WS2812 灯带 | 48 | RMT 通道 1（板载灯珠） |
| nRF24 CE / CSN | 14 / 15 | |
| nRF24 & CC1101 共用 SPI | SCK 16 / MISO 17 / MOSI 18 | FSPI（SPI2），与 TFT 的 HSPI 物理隔离 |
| CC1101 CSN | 5 | |
| CC1101 GDO0（可选） | 6 | 当前驱动未使用 |
| UART0 TX / RX | 43 / 44 | 通过板载 USB 转串口 |

### 不可占用的引脚

`19/20`（原生 USB）、`26–32`（Flash）、`33–37`（Octal PSRAM）、`0/3/45/46`（strapping，上电瞬间会被采样）。

---

## 三、编译与烧录

```bash
# 编译
pio run

# 烧录（串口按实际情况改）
pio run -t upload --upload-port COM5

# 串口监视
pio device monitor -b 115200
```

关键配置（`platformio.ini`）：

```
board = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_opi    # QIO Flash + Octal PSRAM，写错会起不来
board_build.psram = enable
board_build.partitions = huge_app.csv        # 3MB 应用分区
-DENABLE_BLE=1  -DENABLE_CLOCK=0
```

LVGL 版本：**8.3.11**（源码在 `lib/lvgl`，`lv_conf.h` 已按本工程调整过）。

---

## 四、技术架构

**任务划分**

| 任务 | 核心 | 职责 |
|---|---|---|
| `LVGL_Task` | core0 | 所有界面操作、定时器、串口命令分发 |
| `WiFi_Task` | core1 | WiFi 连接与保持 |
| `BLE_Task` | core1 | BLE UART，等 WiFi 初始化完成后再启动（避免抢占内部 DMA 内存） |
| `nrf` / `cc1101` | core1 | 无线嗅探/扫频，独立任务 |
| `SD_Task` | core1 | 空闲 |

**显示策略（无撕裂的关键）**

- 整屏双缓冲放在 PSRAM，每帧 76800 像素
- `full_refresh = 0`，脏区由驱动合并成**单次全帧推送**
- 所有标签"先比后写"：文本/颜色不变就不碰 LVGL，避免周期性小重绘在 ILI9341 上留下接缝
- 屏幕键盘输入时暂停光标闪烁并合并整帧，否则打字会闪

**内存**

- LVGL 的 128KB 内存池放在 PSRAM（`LV_MEM_POOL_ALLOC`），静态 RAM 从 63% 降到 24%
- 频谱画布 62KB、数据面 31KB 也在 PSRAM

**中文字体**

`ui_font_cjk12.c`（GB2312 一级字库 3755 字，12px/4bpp）由 `tools/gen_cjk_font.py` 生成，挂在正文与标题字体的 `fallback` 上：拉丁字符仍用 Montserrat，中文自动落到这套字库，行高不变所以排版不受影响。

---

## 五、串口命令

`help` 打印全部命令，下面是分类速查：

**系统**

```
fetch                系统信息（内存/PSRAM/运行时间）
date                 校时（NTP）
reboot
```

**SD 卡**

```
ls [dir]             列目录
cat <file>           输出文件内容（最大 1MB）
stat <path>          文件/目录信息
rm <path>            删除
```

**温湿度**

```
dht11 profile start | stop
dht11 status         当前读数 + 失败计数
```

**继电器 / 灯带**

```
relay on | off | status
led hue|bright|speed|count <n> | led status
```

**RF Monitor（802.11 被动）**

```
rfmon start | stop | status | clear | dump
rfmon log on | off | status | tail      # 落盘 /rfmon.csv
```

**nRF24（2.4GHz 被动）**

```
nrf sniff [ch]       跳频或锁定单信道
nrf range <min> <max>   扫描窗口（0..125）
nrf dwell <5..2000>     每信道停留毫秒
nrf rate 1m|2m|250k|all 速率档
nrf spectrum         ASCII 频谱
nrf status | dump | clear | stop
nrf screen | spec on|off   远程操作界面
```

**CC1101（Sub-GHz）**

```
cc scan              扫频
cc range <min> <max>   387 464 / 300 348 / 779 928
cc spectrum          ASCII 功率谱
cc pkts              切换收包模式
cc rx <MHz>          收包频率
cc dump              打印已收数据包
cc status | stop | screen
```

---

## 六、SD 卡文件

| 文件 | 内容 |
|---|---|
| `/wifi.cfg` | WiFi SSID / 密码 |
| `/dht11.json` | 温湿度记录（`dht11 profile start` 时追加） |
| `/rfmon.csv` | deauth/AP 事件日志 |
| `/nrf.csv` | nRF24 抓到的包（`ts,seq,ch,rate,len,hex`） |
| `/cc.csv` | CC1101 收到的包（`ts_s,seq,mhz,rssi,lqi,len,hex`） |

---

## 七、目录结构

```
src/
  main.cpp           任务创建、全局状态
  LVGL_Demos.cpp     业务层（界面胶水、串口命令、定时器）
  display_port.cpp   TFT 驱动与 LVGL 显示接口
  dht11.cpp          DHT11 位时序（自实现，不用第三方库）
  rf_monitor.cpp     802.11 被动监听
  nrf24.cpp          nRF24L01+ 嗅探 + RPD 频谱
  cc1101.cpp         CC1101 扫频 + 收包
lib/
  lvgl/              LVGL 8.3.11（lv_conf.h 已改）
    src/ui_test/     界面层：screens/、ui_kit、字体、图片
  TFT_eSPI/ FT6336-arduino/
tools/
  gen_cjk_font.py    生成中文字库
  serial_cli.py      串口命令行工具（抓日志/下发命令）
  strip_comments.py  批量清理注释（保留引脚标注）
```

---

## 八、已知限制

- **nRF24 没有 RSSI**：只有 RPD 位（> -64dBm 置 1），所以它的"频谱"是命中率热图，不是真正的功率谱；要看真实功率用 CC1101
- **CC1101 收包**当前配置为 GFSK 38.4kbps + 同步字 0xD391，面向气象站/传感器的标准帧；便宜的 OOK 遥控器（EV1527/PT2262）需要异步模式 + GDO0 才能解
- nRF24 与 CC1101 **共用一条 SPI**，一次只能跑一个，启动其一会自动停掉另一个
- WiFi 的 Channel Analyzer 是主动扫描，会占用射频；RF Monitor 监听期间 WiFi 断开，停止后自动重连
