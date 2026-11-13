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

#define NRF_CH_MIN        2
#define NRF_CH_MAX        80
#define NRF_DWELL_MS      400
#define NRF_KEEP_LAST     8
#define NRF_PKT_MAX       32

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
    bool     hopping;
    uint32_t packets;
    uint32_t next_seq;
    uint32_t version;
    uint8_t  pkt_count;
    nrf_pkt_t pkts[NRF_KEEP_LAST];    // [0] 最新
} nrf_stats_t;

bool nrf_sniffer_init(void);
bool nrf_sniffer_available(void);
void nrf_sniffer_start(int fixed_ch); // fixed_ch < 0 时跳频
void nrf_sniffer_stop(void);
bool nrf_sniffer_active(void);
void nrf_get_stats(nrf_stats_t *out);
void nrf_clear_stats(void);

#ifdef __cplusplus
}
#endif

#endif