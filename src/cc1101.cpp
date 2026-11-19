// CC1101 Sub-GHz 扫频（只收不发）
//
// 与 nRF24 的区别很重要：CC1101 有真正的 RSSI 寄存器（0x34，0.5dB 步进，
// 可读 -110~0 dBm），所以这里得到的是"功率"，不是 nRF24 那种二值 RPD。
// 扫频做法：每个点 SIDLE → 改 FREQ → SRX → 等 2ms → 读 RSSI → 下一点。
//
// SPI 与 nRF24 共用 FSPI（16/17/18），CSN 各自独立；两个任务同时跑会互相踩
// 总线，所以约定一次只开一个（互斥在业务层强制）。

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "cc1101.h"
#include "nrf24.h"

// ---------------- 寄存器 ----------------
#define R_IOCFG2      0x00
#define R_IOCFG0      0x02
#define R_FIFOTHR     0x03
#define R_SYNC1       0x04
#define R_SYNC0       0x05
#define R_PKTLEN      0x06
#define R_PKTCTRL1    0x07
#define R_PKTCTRL0    0x08
#define R_ADDR        0x09
#define R_CHANNR      0x0A
#define R_FSCTRL1     0x0B
#define R_FSCTRL0     0x0C
#define R_FREQ2       0x0D
#define R_FREQ1       0x0E
#define R_FREQ0       0x0F
#define R_MDMCFG4     0x10
#define R_MDMCFG3     0x11
#define R_MDMCFG2     0x12
#define R_MDMCFG1     0x13
#define R_MDMCFG0     0x14
#define R_DEVIATN     0x15
#define R_MCSM1       0x17
#define R_MCSM0       0x18
#define R_FOCCFG      0x19
#define R_BSCFG       0x1A
#define R_AGCCTRL2    0x1B
#define R_AGCCTRL1    0x1C
#define R_AGCCTRL0    0x1D
#define R_FREND1      0x21
#define R_FREND0      0x22
#define R_FSCAL3      0x23
#define R_FSCAL2      0x24
#define R_FSCAL1      0x25
#define R_FSCAL0      0x26
#define R_TEST2       0x2C
#define R_TEST1       0x2D
#define R_TEST0       0x2E

#define S_PARTNUM     0x30
#define S_VERSION     0x31
#define S_RSSI        0x34
#define S_RXBYTES     0x3B
#define R_RXFIFO      0x3F
#define R_PATABLE     0x3E

#define ST_SRES       0x30
#define ST_SCAL       0x33
#define ST_SRX        0x34
#define ST_SIDLE      0x36
#define ST_SFRX       0x3A
#define ST_SNOP       0x3D

#define CC_XOSC_HZ    26000000.0f

static SPIClass      s_spi(FSPI);          // 与 nRF24 同一条总线
static TaskHandle_t  s_task = NULL;
static volatile bool s_active = false;
static bool          s_ok = false;

static float    s_min_mhz = CC_DEFAULT_MIN;
static float    s_max_mhz = CC_DEFAULT_MAX;
static int8_t   s_scan[CC_STEPS];
static uint32_t s_pass = 0;
static int8_t   s_cur_dbm = -127;
static uint8_t  s_cur_step = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t  s_mode = CC_MODE_SWEEP;
static float    s_rx_mhz = 433.92f;
static cc_pkt_t s_pkts[CC_MAX_PKTS];
static uint8_t  s_pkt_count = 0;
static uint32_t s_pkt_total = 0;
static uint32_t s_pkt_version = 1;

// ---------------- 底层访问 ----------------
static void cc_csn(int level) { digitalWrite(CC_PIN_CSN, level ? HIGH : LOW); }

static void cc_wreg(uint8_t addr, uint8_t val) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc_csn(0);
    s_spi.transfer(addr);
    s_spi.transfer(val);
    cc_csn(1);
    s_spi.endTransaction();
}

static uint8_t cc_rreg(uint8_t addr) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc_csn(0);
    s_spi.transfer(addr | 0x80);
    uint8_t v = s_spi.transfer(0);
    cc_csn(1);
    s_spi.endTransaction();
    return v;
}

// 状态寄存器需要 burst 读（单字节读拿到的是 chip status）
static uint8_t cc_rstatus(uint8_t addr) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc_csn(0);
    s_spi.transfer(addr | 0xC0);
    uint8_t v = s_spi.transfer(0);
    cc_csn(1);
    s_spi.endTransaction();
    return v;
}

static void cc_strobe(uint8_t cmd) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc_csn(0);
    s_spi.transfer(cmd);
    cc_csn(1);
    s_spi.endTransaction();
}

// f_carrier = (26MHz / 2^16) * FREQ24  → FREQ24 = f * 2^16 / 26MHz
static void cc_set_freq(float mhz) {
    uint32_t f = (uint32_t)(mhz * 1000000.0f * 65536.0f / CC_XOSC_HZ);
    if (f > 0xFFFFFF) f = 0xFFFFFF;
    cc_wreg(R_FREQ2, (uint8_t)(f >> 16));
    cc_wreg(R_FREQ1, (uint8_t)(f >> 8));
    cc_wreg(R_FREQ0, (uint8_t)f);
}

static int8_t cc_raw_to_dbm(uint8_t raw) {
    int r = raw;
    if (r >= 128) return (int8_t)(((r - 256) / 2) - 74);
    return (int8_t)((r / 2) - 74);
}

static int8_t cc_read_rssi(void) { return cc_raw_to_dbm(cc_rstatus(S_RSSI)); }

// ---------------- 初始化 ----------------
bool cc1101_init(void) {
    if (s_ok) return true;

    pinMode(CC_PIN_CSN, OUTPUT);
    cc_csn(1);

    // 与 nRF24 共用 16/17/18；这里再 begin 一次是幂等的（同参数）
    s_spi.begin(NRF_PIN_SCK, NRF_PIN_MISO, NRF_PIN_MOSI, -1);
    delay(10);

    uint8_t part = cc_rstatus(S_PARTNUM);
    uint8_t ver  = cc_rstatus(S_VERSION);
    if (ver == 0x00 || ver == 0xFF) {
        Serial.printf("[cc1101] not found (PARTNUM=0x%02X VERSION=0x%02X) — check CSN=5, SCK16/MISO17/MOSI18, VCC=3.3V\n",
                      part, ver);
        s_ok = false;
        return false;
    }

    cc_strobe(ST_SRES);
    delay(5);

    static const uint8_t init_seq[][2] = {
        { R_IOCFG2,   0x2E },   // GDO2 = CHIP_RDYn
        { R_IOCFG0,   0x2D },   // GDO0 = 高阻
        { R_FIFOTHR,  0x47 },
        { R_SYNC1,    0xD3 },
        { R_SYNC0,    0x91 },
        { R_PKTLEN,   0x3C },
        { R_PKTCTRL1, 0x04 },
        { R_PKTCTRL0, 0x32 },   // 异步串行模式，便于纯 RSSI 使用
        { R_ADDR,     0x00 },
        { R_CHANNR,   0x00 },
        { R_FSCTRL1,  0x06 },
        { R_FSCTRL0,  0x00 },
        { R_MDMCFG4,  0xF5 },   // 38.4 kBaud
        { R_MDMCFG3,  0x83 },
        { R_MDMCFG2,  0x13 },   // GFSK, 30/32 sync
        { R_MDMCFG1,  0x22 },
        { R_MDMCFG0,  0xF8 },
        { R_DEVIATN,  0x15 },
        { R_MCSM1,    0x30 },
        { R_MCSM0,    0x18 },
        { R_FOCCFG,   0x16 },
        { R_BSCFG,    0x6C },
        { R_AGCCTRL2, 0x03 },
        { R_AGCCTRL1, 0x40 },
        { R_AGCCTRL0, 0x91 },
        { R_FREND1,   0x56 },
        { R_FREND0,   0x10 },
        { R_FSCAL3,   0xE9 },
        { R_FSCAL2,   0x2A },
        { R_FSCAL1,   0x00 },
        { R_FSCAL0,   0x1F },
        { R_TEST2,    0x81 },
        { R_TEST1,    0x35 },
        { R_TEST0,    0x09 },
    };
    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); i++)
        cc_wreg(init_seq[i][0], init_seq[i][1]);

    cc_set_freq(CC_DEFAULT_MIN);
    cc_strobe(ST_SCAL);
    delay(3);
    cc_strobe(ST_SIDLE);

    s_ok = true;
    Serial.printf("[cc1101] ready, VERSION=0x%02X, span %.1f..%.1f MHz\n",
                  ver, CC_DEFAULT_MIN, CC_DEFAULT_MAX);
    return true;
}

bool cc1101_available(void) { return s_ok; }
bool cc1101_active(void)    { return s_active; }

bool cc1101_set_range(float min_mhz, float max_mhz) {
    // 合法频段（数据手册 Table）：300–348 / 387–464 / 779–928 MHz
    if (!(min_mhz >= 300.0f && max_mhz <= 928.0f && max_mhz > min_mhz)) return false;
    if (!((min_mhz >= 300.0f && max_mhz <= 348.0f) ||
          (min_mhz >= 387.0f && max_mhz <= 464.0f) ||
          (min_mhz >= 779.0f && max_mhz <= 928.0f))) return false;
    s_min_mhz = min_mhz;
    s_max_mhz = max_mhz;
    return true;
}

void cc1101_get_config(float * min_mhz, float * max_mhz) {
    if (min_mhz) *min_mhz = s_min_mhz;
    if (max_mhz) *max_mhz = s_max_mhz;
}

// ---------------- 扫频任务 ----------------
static void cc_config_packet_rx(void);
static bool cc_fetch_packet(uint8_t * buf, uint8_t * len, int8_t * rssi, uint8_t * lqi);
static void cc_store_pkt(const uint8_t * data, uint8_t len, int8_t rssi, uint8_t lqi);

static void cc_task(void * arg) {
    (void)arg;
    if (s_mode == CC_MODE_PKTS) {
        cc_config_packet_rx();
        uint32_t last_rx = millis();
        while (s_active) {
            uint8_t buf[CC_PKT_MAX], len = 0, lqi = 0;
            int8_t rssi = 0;
            if (cc_fetch_packet(buf, &len, &rssi, &lqi)) {
                cc_store_pkt(buf, len, rssi, lqi);
                last_rx = millis();
            } else if (millis() - last_rx > 2000) {
                // 长时间没有包：重新武装，避免漏掉 MCSM1 自动回 IDLE 的情况
                last_rx = millis();
                cc_strobe(ST_SIDLE);
                cc_strobe(ST_SFRX);
                cc_strobe(ST_SRX);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        cc_strobe(ST_SIDLE);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (s_active) {
        for (int i = 0; i < CC_STEPS && s_active; i++) {
            float mhz = s_min_mhz + (s_max_mhz - s_min_mhz) * i / (CC_STEPS - 1);
            cc_strobe(ST_SIDLE);
            cc_set_freq(mhz);
            cc_strobe(ST_SRX);
            vTaskDelay(pdMS_TO_TICKS(2));        // 等 AGC 稳定
            int8_t dbm = cc_read_rssi();

            portENTER_CRITICAL(&s_mux);
            s_scan[i] = dbm;
            s_cur_step = (uint8_t)i;
            s_cur_dbm = dbm;
            portEXIT_CRITICAL(&s_mux);
        }
        portENTER_CRITICAL(&s_mux);
        s_pass++;
        portEXIT_CRITICAL(&s_mux);
    }
    cc_strobe(ST_SIDLE);
    s_task = NULL;
    vTaskDelete(NULL);
}

void cc1101_start(void) {
    if (s_active) return;
    if (!cc1101_init()) return;

    // 共用 SPI：CC1101 一动，nRF24 必须停
    if (nrf_sniffer_active()) {
        nrf_sniffer_stop();
        Serial.println("[cc1101] nRF24 sniffing stopped (shared SPI bus)");
    }

    s_active = true;
    if (xTaskCreatePinnedToCore(cc_task, "cc1101", 4096, NULL, 1, &s_task, 1) != pdPASS) {
        Serial.println("[cc1101] task create FAILED");
        s_active = false;
        return;
    }
    Serial.printf("[cc1101] started: %s\n",
                  s_mode == CC_MODE_PKTS ? "packet RX" : "RSSI sweep");
    if (s_mode == CC_MODE_PKTS)
        Serial.printf("[cc1101] listening %.3f MHz, sync 0xD391, CRC on\n", (double)s_rx_mhz);
}

void cc1101_stop(void) {
    if (!s_active) return;
    s_active = false;
    for (int i = 0; i < 60 && s_task != NULL; i++) delay(5);
    s_task = NULL;
    Serial.println("[cc1101] stopped");
}

void cc1101_get_scan(int8_t * dbm, uint32_t * pass, uint8_t * cur_step,
                     int8_t * cur_dbm, float * min_mhz, float * max_mhz) {
    portENTER_CRITICAL(&s_mux);
    if (dbm)      memcpy(dbm, s_scan, sizeof(s_scan));
    if (pass)     *pass = s_pass;
    if (cur_step) *cur_step = s_cur_step;
    if (cur_dbm)  *cur_dbm = s_cur_dbm;
    portEXIT_CRITICAL(&s_mux);
    if (min_mhz) *min_mhz = s_min_mhz;
    if (max_mhz) *max_mhz = s_max_mhz;
}

// ---------------- 收包模式 ----------------
uint8_t cc1101_mode(void) { return s_mode; }

bool cc1101_set_rx_mhz(float mhz) {
    if (!((mhz >= 300.0f && mhz <= 348.0f) ||
          (mhz >= 387.0f && mhz <= 464.0f) ||
          (mhz >= 779.0f && mhz <= 928.0f))) return false;
    s_rx_mhz = mhz;
    return true;
}

float cc1101_rx_mhz(void) { return s_rx_mhz; }

void cc1101_set_mode(uint8_t mode) {
    if (mode > CC_MODE_PKTS) mode = CC_MODE_PKTS;
    s_mode = mode;
    if (s_active) {          // 重新起任务，按新模式配置射频
        cc1101_stop();
        cc1101_start();
    }
}

void cc1101_get_pkts(cc_pkt_t * out, uint8_t * count, uint32_t * version, uint32_t * total) {
    portENTER_CRITICAL(&s_mux);
    if (out)     memcpy(out, s_pkts, sizeof(s_pkts));
    if (count)   *count = s_pkt_count;
    if (version) *version = s_pkt_version;
    if (total)   *total = s_pkt_total;
    portEXIT_CRITICAL(&s_mux);
}

// 收包模式寄存器设置：
//   PKTCTRL0 = 0x05  变长 + CRC 开 + 白化关
//   PKTCTRL1 = 0x04  包尾附加 RSSI/LQI/CRC_OK 两个状态字节
//   PKTLEN   = 61
//   MDMCFG2  = 0x13  已含 30/32 同步字（SYNC = D3 91）
static void cc_config_packet_rx(void) {
    cc_wreg(R_PKTCTRL1, 0x04);
    cc_wreg(R_PKTCTRL0, 0x05);
    cc_wreg(R_PKTLEN,   CC_PKT_MAX);
    cc_set_freq(s_rx_mhz);
    cc_strobe(ST_SIDLE);
    cc_strobe(ST_SFRX);      // 清 RX FIFO
    cc_strobe(ST_SRX);
}

static void cc_store_pkt(const uint8_t * data, uint8_t len, int8_t rssi, uint8_t lqi) {
    portENTER_CRITICAL(&s_mux);
    for (int i = CC_MAX_PKTS - 1; i > 0; i--) s_pkts[i] = s_pkts[i - 1];
    s_pkts[0].seq  = s_pkt_total + 1;
    s_pkts[0].len  = len;
    s_pkts[0].rssi = rssi;
    s_pkts[0].lqi  = lqi;
    for (uint8_t i = 0; i < len && i < CC_PKT_MAX; i++) s_pkts[0].data[i] = data[i];
    if (s_pkt_count < CC_MAX_PKTS) s_pkt_count++;
    s_pkt_total++;
    s_pkt_version++;
    portEXIT_CRITICAL(&s_mux);
}

// 从 RX FIFO 取一包：变长模式下首个字节是长度，最后两个是 RSSI/LQI
static bool cc_fetch_packet(uint8_t * buf, uint8_t * len, int8_t * rssi, uint8_t * lqi) {
    uint8_t nb = cc_rstatus(S_RXBYTES) & 0x7F;
    if (nb < 2) return false;

    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc_csn(0);
    s_spi.transfer(R_RXFIFO | 0xC0);
    uint8_t n = s_spi.transfer(0);
    if (n == 0 || n > CC_PKT_MAX) {           // 长度非法：清 FIFO 重来
        cc_csn(1);
        s_spi.endTransaction();
        cc_strobe(ST_SIDLE);
        cc_strobe(ST_SFRX);
        cc_strobe(ST_SRX);
        return false;
    }
    for (uint8_t i = 0; i < n; i++) buf[i] = s_spi.transfer(0);
    uint8_t r = s_spi.transfer(0);
    uint8_t q = s_spi.transfer(0);
    cc_csn(1);
    s_spi.endTransaction();

    *len  = n;
    *rssi = cc_raw_to_dbm(r);
    *lqi  = q & 0x7F;
    return true;
}