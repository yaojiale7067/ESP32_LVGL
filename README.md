# ESP32 FreeRTOS + LVGL 综合测试项目
作者：YJL_SAMA

## 项目简介
基于 ESP32 开发，结合 FreeRTOS 实时操作系统与 LVGL 图形库，集成文件管理、传感器采集、串口调试、SD 卡文件读写等多项功能，用于嵌入式外设与界面功能综合测试。

## 主要功能
1. 📁 **SD 卡文件资源管理器**
   支持基础文件浏览与管理操作。

2. 📤 **TXT 文件串口传输**
   可读取 SD 卡内 `.txt` 文件，并通过串口输出内容。

3. ✏️ **JSON 文件编辑**
   支持对 SD 卡内 JSON 配置文件进行修改。

4. 🌡️ **DHT11 温湿度采集与存储**
   - 控制指令：
     - `dht11 profile start`：开启采集，每 3 秒记录温湿度，数据保存至 SD 卡根目录 `dht11.json`
     - `dht11 profile stop`：停止温湿度记录
   - LVGL 界面实时展示温湿度数据

5. 📡 **简易串口调试工具**
   实现串口数据收发，方便设备调试。

6. 🔵 **蓝牙(BLE)串口 —— 连接 / 数据接收 / 数据发送**
   使用 BLE 的 **Nordic UART Service (NUS)**，手机端各类"BLE 串口助手"与
   nRF Connect 均可直接识别；详见下方"蓝牙(BLE)串口说明"。

## 技术架构
- 主控：ESP32S3
- 系统：FreeRTOS（LVGL 任务在 core0；WiFi / BLE / SD 任务在 core1）
- 图形库：LVGL
- 外设：SD 卡、DHT11 温湿度传感器、串口、BLE

## 蓝牙(BLE)串口说明
> ESP32-S3 **只有 BLE，没有经典蓝牙(BR/EDR)**，所以不存在 SPP 串口。
> 本功能走 BLE GATT，使用最常见的 NUS 服务。

- 设备名：`ESP32S3-BLE-UART`
- UUID：
  - 服务 `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - 手机→设备 `6E400002-…`（Write / Write Without Response）
  - 设备→手机 `6E400003-…`（Notify）
- 入口：主菜单里的 `Bluetooth`（蓝色那一行）
- 界面：
  - 顶栏 `Clear`：清空接收区
  - 中间绿色区域：显示收到的数据，自动滚到底部；超过 400 字符只保留最新部分
  - 底部输入框 + `Send`：发送数据；键盘按 √ 等同于点 Send
  - 状态行：`● Advertising as …` / `● Connected <MAC> RX n / TX n`
- 编译开关：`platformio.ini` 里的 `-DENABLE_BLE=1`。改成 `0` 可把 BLE 屏幕、
  任务、菜单项全部裁掉（WiFi 与 BLE 同时运行导致内存紧张时很有用）
- ⚠️ BLE 与 WiFi 共用同一个 2.4G 射频，两者同时大流量传输时会互相抢时间片

## 界面规范（所有屏统一）
所有屏共用一套视觉规范，实现在 `lib/lvgl/src/ui_test/ui_kit.{h,c}`：
深色背景 `0x1E1E1E` / 面板 `0x2D2D2D` / 边框 `0x555555`，主色蓝 `0x2D6CB5`、
成功绿 `0x2E8B57`、次要灰 `0x3D3D3D`、危险红 `0xFF5555`，圆角 5px，左右留白 10px。

每屏结构一致：

| 区域 | 位置 | 内容 |
| --- | --- | --- |
| 顶栏 | y 2~32 | `← Back`、居中标题、可选右侧按钮 |
| 内容区 | y 38 起 | 面板 / 列表 / 大号数值 |
| 状态条 | y 212 | 横向滚动的一行提示 |
| 键盘 | y 120~239 | 需要输入时弹出，隐藏时不占位 |

主菜单是 5 行整行可点的菜单项：Temp/RH、File Manager、Serial Tool、WiFi、Bluetooth。

> ⚠️ **`lib/lvgl/src/ui_test/` 下的文件已经全部改为手写**，不再依赖 SquareLine：
> `ui.c` 里仍保留 `ui_init()`（会依次建 5 个屏），但各 `*_screen_init()` 的
> 内容是本项目的实现。**如果用 SquareLine Studio 重新导出，会把这些屏覆盖掉**，
> 需要重新套用 `ui_kit`。`ui_ble.c` / `ui_kit.c` 不在 SquareLine 工程里，不受影响。

### 维护提醒：BLE 的两把锁必须用临界区，不能用互斥量
`ble_ring_push()` 的执行者是 Bluedroid 的 BTC 任务（优先级 19），
而消费者是 LVGL 任务（优先级 3）。FreeRTOS 互斥量只适合"高优先级等低优先级"
这一种方向，反方向使用会在 `xTaskPriorityDisinherit` 里断言失败，
直接在蓝牙协议栈里 panic。因此环形缓冲 `g_bleRxMux` 和接收显示缓冲
`rx_disp_mux` 都用 `portMUX_TYPE` + `portENTER_CRITICAL()`，
里面只做几微秒的内存拷贝，**不要**在临界区里调用 `Serial.print` / LVGL API。

## 串口命令
| 命令 | 作用 |
| --- | --- |
| `dht11 profile start` / `stop` / `status` | 温湿度记录开关与状态 |
| `fetch` | 打印芯片/内存/SD 卡信息 |
| `date` | 联网后获取 NTP 时间 |
| `reboot` | 重启 |
| `help` | 命令列表 |

（命令不区分大小写，带不带前导 `/` 都可以。）
