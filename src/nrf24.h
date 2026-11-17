// nrf24.h - nRF24L01+ 被动嗅探器（只收不发）

#ifndef NRF24_H
#define NRF24_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 接线：nRF24 -> ESP32-S3
// GND  -> GND
// VCC  -> 3V3（仅 3.3V）
// CE   -> GPIO14
// CSN  -> GPIO15
// SCK  -> GPIO16
// MOSI -> GPIO18
// MISO -> GPIO17
// IRQ  -> 不接
#define NRF_PIN_CE    14
#define NRF_PIN_CSN   15
#define NRF_PIN_SCK   16
#define NRF_PIN_MISO  17
#define NRF_PIN_MOSI  18

// nRF24L01+ 的 RF_CH 是 7 位寄存器，合法 0..125 → 2400..2525MHz。
// 全扫 126 个信道；一次完整扫频 = 126 × dwell。默认 200ms → 约 25s 一圈，
// 三档速率轮换时一圈 76s。嫌慢用 `nrf dwell <50..2000>` 调。
#define NRF_CH_MIN        0
#define NRF_CH_MAX        125
#define NRF_DWELL_MS      200
#define NRF_KEEP_LAST     8
#define NRF_PKT_MAX       32

#define NRF_RATE_1M       0
#define NRF_RATE_2M       1
#define NRF_RATE_250K     2
#define NRF_RATE_ALL      3          // 每扫完一圈换一档速率

#define NRF_CH_NUM        126        // RF_CH 0..125 全量（频谱数组按这个开）

typedef struct {
    uint32_t seq;
    uint8_t ch;
    uint8_t rate;                     // 0=1M, 1=2M, 2=250K
    uint8_t len;
    uint8_t data[NRF_PKT_MAX];
} nrf_pkt_t;

typedef struct {
    bool     available;
    bool     active;
    uint8_t  channel;
    uint8_t  rate_idx;
    uint8_t  rate_mode;               // 0/1/2 固定，3 = 轮换
    uint8_t  min_ch;                  // 当前扫描窗口
    uint8_t  max_ch;
    uint16_t dwell_ms;
    bool     hopping;
    uint32_t packets;
    uint32_t next_seq;
    uint32_t version;
    uint8_t  pkt_count;
    nrf_pkt_t pkts[NRF_KEEP_LAST];    // [0] 最新
} nrf_stats_t;

bool nrf_sniffer_init(void);
bool nrf_sniffer_available(void);
void nrf_sniffer_start(int fixed_ch); // fixed_ch < 0 时在窗口内跳频
void nrf_sniffer_stop(void);
bool nrf_sniffer_active(void);
void nrf_get_stats(nrf_stats_t *out);
void nrf_clear_stats(void);

// 扫描参数（运行中修改立即生效，无需重启嗅探）
bool nrf_set_range(uint8_t min_ch, uint8_t max_ch);   // 参数非法返回 false
bool nrf_set_dwell(uint16_t ms);                      // 50..2000
void nrf_set_rate_mode(uint8_t mode);                 // 0/1/2 固定，3 轮换
const char * nrf_rate_name(uint8_t rate);
void nrf_get_config(uint8_t * min_ch, uint8_t * max_ch, uint16_t * dwell_ms, uint8_t * rate_mode);

// RPD 频谱：out/live 都是长度 NRF_CH_NUM 的数组，值 0..255（越大越"热闹"）
// out = 上一轮完成的扫描结果，live = 本轮已扫到的部分（用于边扫边画）
void nrf_get_spectrum(uint8_t * out, uint8_t * live, uint32_t * pass_seq, uint8_t * cur_ch);

#ifdef __cplusplus
}
#endif

#endif