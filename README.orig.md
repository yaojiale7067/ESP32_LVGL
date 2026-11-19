# ESP32 FreeRTOS + LVGL 测试
作者：YJL_SAMA

## 项目简介
基于 ESP32 开发，结合 FreeRTOS 实时操作系统与 LVGL 图形库，集成文件管理、传感器采集、串口调试、SD 卡文件读写等多项功能，用于嵌入式外设与界面功能测试。

## 主要功能
1.  **SD 卡文件资源管理器**
   支持基础文件浏览与管理操作。

2.  **TXT 文件串口传输**
   可读取 SD 卡内 `.txt` 文件，并通过串口输出内容。

3.  **JSON 文件编辑**
   支持对 SD 卡内 JSON 配置文件进行修改。

4.  **DHT11 温湿度采集与存储**
   - 控制指令：
     - `dht11 profile start`：开启采集，每 3 秒记录温湿度，数据保存至 SD 卡根目录 `dht11.json`
     - `dht11 profile stop`：停止温湿度记录
   - LVGL 界面实时展示温湿度数据

5.  **简易串口调试工具**
   实现串口数据收发，方便设备调试。

## 技术架构
- 主控：ESP32S3
- 系统：FreeRTOS（LVGL 任务在 core0；WiFi / BLE / SD 任务在 core1）
- 图形库：LVGL
- 外设：SD 卡、DHT11 温湿度传感器、串口、BLE


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
-  BLE 与 WiFi 共用同一个 2.4G 射频，两者同时大流量传输时会互相抢时间片

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


## 串口命令
| 命令 | 作用 |
| --- | --- |
| `dht11 profile start` / `stop` / `status` | 温湿度记录开关与状态 |
| `fetch` | 打印芯片/内存/SD 卡信息 |
| `date` | 联网后获取 NTP 时间 |
| `reboot` | 重启 |
| `help` | 命令列表 |


