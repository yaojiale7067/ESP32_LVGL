#ifndef CC1101_H
#define CC1101_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 接线：CC1101 与 nRF24 共用 FSPI（SCK/MISO/MOSI），各自一根 CSN。
//   CC1101 VCC  --- 3V3（模块 1.8~3.6V，千万别接 5V）
//   CC1101 GND  --- GND
//   CC1101 SCK  --- GPIO16
//   CC1101 MISO --- GPIO17
//   CC1101 MOSI --- GPIO18
//   CC1101 CSN  --- GPIO5
//   CC1101 GDO0 --- GPIO6（可选，本驱动未使用；不接也能扫）
// ★ 两个模块共用一条 SPI，因此【一次只跑一个】：启动 CC1101 扫描会自动
//   停掉 nRF24 嗅探，反之亦然（共用总线最省事的正确做法）。
#define CC_PIN_CSN   5
#define CC_PIN_GDO0  6

#define CC_STEPS        128      // 一次扫频的点数
#define CC_DEFAULT_MIN  387.0f   // MHz，CC1101 支持 387~464 / 300~348 / 779~928
#define CC_DEFAULT_MAX  464.0f

#define CC_MODE_SWEEP   0        // RSSI 扫频
#define CC_MODE_PKTS    1        // 定频收包（SYNC + CRC）

#define CC_MAX_PKTS     8
#define CC_PKT_MAX      61       // FIFO 64B - 长度字节 - 2 状态字节

typedef struct {
    uint32_t seq;
    int8_t   rssi;               // 包尾附加的 RSSI(dBm)
    uint8_t  lqi;
    uint8_t  len;
    uint8_t  data[CC_PKT_MAX];
} cc_pkt_t;

bool cc1101_init(void);
bool cc1101_available(void);

bool cc1101_set_range(float min_mhz, float max_mhz);
void cc1101_get_config(float * min_mhz, float * max_mhz);

void cc1101_start(void);
void cc1101_stop(void);
bool cc1101_active(void);

// 模式：扫频 / 收包。切换时任务会重启；收包用"当前频率"（默认区间中点）
void cc1101_set_mode(uint8_t mode);
uint8_t cc1101_mode(void);
bool cc1101_set_rx_mhz(float mhz);      // 收包频率，返回是否在合法频段
float cc1101_rx_mhz(void);

// 上一轮扫频结果（CC_MODE_SWEEP 时有效）
void cc1101_get_scan(int8_t * dbm, uint32_t * pass, uint8_t * cur_step,
                     int8_t * cur_dbm, float * min_mhz, float * max_mhz);

// 已收包（CC_MODE_PKTS 时有效）：out 至少 CC_MAX_PKTS 项，[0] 最新
void cc1101_get_pkts(cc_pkt_t * out, uint8_t * count, uint32_t * version, uint32_t * total);

#ifdef __cplusplus
}
#endif

#endif