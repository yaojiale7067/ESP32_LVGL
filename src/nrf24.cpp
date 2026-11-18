// ============================================================================
//  nrf24.cpp - nRF24L01+ 被动嗅探器（只收不发）
//
//  接线表与设计说明见 nrf24.h 文件头。要点：
//    · 独占 FSPI（SPI2），TFT/SD 在 HSPI（SPI3），物理隔离不抢总线
//    · 自动应答关闭 → 全程只收，绝不发射
//    · 握手：写 RF_CH=76 读回校验，验证 SPI 通不通
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "nrf24.h"

// ---------------- nRF24L01+ 寄存器与命令 ----------------
#define REG_CONFIG      0x00
#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_SETUP_AW    0x03
#define REG_RF_CH       0x05
#define REG_RF_SETUP    0x06
#define REG_STATUS      0x07
#define REG_RPD         0x09
#define REG_RX_ADDR_P0  0x0A
#define REG_RX_PW_P0    0x11
#define CMD_R_REGISTER  0x00
#define CMD_W_REGISTER  0x20
#define CMD_R_RX_PAYLOAD 0x61
#define CMD_FLUSH_RX    0xE2

#define STATUS_RX_DR    0x40
#define STATUS_RX_P_NO  0x0E    // 收到数据的 pipe 号（3..7 表示 RX FIFO 空）

// RF_SETUP 速率档（0dBm）：1M / 2M / 250K
static const uint8_t kRateSetup[3] = { 0x06, 0x0E, 0x26 };
static const char *  kRateName[3]  = { "1M", "2M", "250K" };

// ---------------- 状态（NRF 任务写，LVGL/串口读，spinlock 保护）----------------
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static nrf_stats_t   s_stats;
static SPIClass      s_spi(FSPI);          // 独占 SPI2，与 TFT/SD(SPI3) 不冲突
static TaskHandle_t  s_task = NULL;
static volatile bool s_active = false;
static bool          s_hw_ok = false;
static int           s_fixed_ch = -1;      // <0 = 跳频

// 扫描窗口 / 停留 / 速率档：运行中可改，跳频任务每圈重新读取
static volatile uint8_t  s_min_ch    = NRF_CH_MIN;
static volatile uint8_t  s_max_ch    = NRF_CH_MAX;
static volatile uint16_t s_dwell_ms  = NRF_DWELL_MS;
static volatile uint8_t  s_rate_mode = NRF_RATE_ALL;

const char * nrf_rate_name(uint8_t rate) {
    return kRateName[rate > 2 ? 2 : rate];
}

bool nrf_set_range(uint8_t min_ch, uint8_t max_ch) {
    if (min_ch > max_ch || max_ch > NRF_CH_MAX) return false;
    s_min_ch = min_ch;
    s_max_ch = max_ch;
    portENTER_CRITICAL(&s_mux);
    s_stats.min_ch = min_ch;
    s_stats.max_ch = max_ch;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool nrf_set_dwell(uint16_t ms) {
    if (ms < 5 || ms > 2000) return false;
    s_dwell_ms = ms;
    portENTER_CRITICAL(&s_mux);
    s_stats.dwell_ms = ms;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void nrf_set_rate_mode(uint8_t mode) {
    if (mode > NRF_RATE_ALL) mode = NRF_RATE_ALL;
    s_rate_mode = mode;
    portENTER_CRITICAL(&s_mux);
    s_stats.rate_mode = mode;
    portEXIT_CRITICAL(&s_mux);
}

void nrf_get_config(uint8_t * min_ch, uint8_t * max_ch, uint16_t * dwell_ms, uint8_t * rate_mode) {
    if (min_ch)    *min_ch = s_min_ch;
    if (max_ch)    *max_ch = s_max_ch;
    if (dwell_ms)  *dwell_ms = s_dwell_ms;
    if (rate_mode) *rate_mode = s_rate_mode;
}

// ---------------- 频谱数据（RPD 采样）----------------
// nRF24L01+ 没有 RSSI 寄存器，只有 RPD（Received Power Detector）一位：
// 接收时若检测到 > -64dBm 的信号它就置 1。所以这里把它当"有/无能量"采样，
// 在每个信道停留期间按 1ms 打点，命中率 ×255 就是该信道的活动度（0..255），
// 和 SDR 频谱图里的功率色阶是一个意思（只是二值量化的）。
static uint8_t  s_act[NRF_CH_NUM];       // 上一轮完成的活动度
static uint8_t  s_act_live[NRF_CH_NUM];  // 本轮已扫到的部分
static uint32_t s_pass_seq = 0;          // 每完成一轮 +1

void nrf_get_spectrum(uint8_t * out, uint8_t * live, uint32_t * pass_seq, uint8_t * cur_ch) {
    portENTER_CRITICAL(&s_mux);
    if (out)      memcpy(out, s_act, NRF_CH_NUM);
    if (live)     memcpy(live, s_act_live, NRF_CH_NUM);
    if (pass_seq) *pass_seq = s_pass_seq;
    if (cur_ch)   *cur_ch = s_stats.channel;
    portEXIT_CRITICAL(&s_mux);
}

static bool pins_configured(void) {
    return NRF_PIN_CE >= 0 && NRF_PIN_CSN >= 0 && NRF_PIN_SCK >= 0 &&
           NRF_PIN_MISO >= 0 && NRF_PIN_MOSI >= 0;
}

// ---------------- 底层 SPI 读写（CSN 手动拉低）----------------
static uint8_t nrf_xfer(uint8_t b) { return s_spi.transfer(b); }

static uint8_t nrf_read_reg(uint8_t reg) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NRF_PIN_CSN, LOW);
    nrf_xfer(CMD_R_REGISTER | (reg & 0x1F));
    uint8_t v = nrf_xfer(0xFF);
    digitalWrite(NRF_PIN_CSN, HIGH);
    s_spi.endTransaction();
    return v;
}

static void nrf_write_reg(uint8_t reg, uint8_t val) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NRF_PIN_CSN, LOW);
    nrf_xfer(CMD_W_REGISTER | (reg & 0x1F));
    nrf_xfer(val);
    digitalWrite(NRF_PIN_CSN, HIGH);
    s_spi.endTransaction();
}

static void nrf_write_multi(uint8_t reg, const uint8_t * buf, uint8_t len) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NRF_PIN_CSN, LOW);
    nrf_xfer(CMD_W_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++) nrf_xfer(buf[i]);
    digitalWrite(NRF_PIN_CSN, HIGH);
    s_spi.endTransaction();
}

static void nrf_flush_rx(void) {
    s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NRF_PIN_CSN, LOW);
    nrf_xfer(CMD_FLUSH_RX);
    digitalWrite(NRF_PIN_CSN, HIGH);
    s_spi.endTransaction();
}

// ---------------- 芯片配置 ----------------
static void nrf_set_rate(uint8_t rate_idx) {
    nrf_write_reg(REG_RF_SETUP, kRateSetup[rate_idx & 0x03]);
}

static void nrf_set_channel(uint8_t ch) {
    if (ch > 125) ch = 125;                 // RF_CH 有效 0..125
    nrf_write_reg(REG_RF_CH, ch);
}

// 进入被动接收：关 auto-ACK / 关 CRC / 2 字节地址 0x00AA / pipe0 收 32 字节
static void nrf_config_promiscuous(void) {
    nrf_write_reg(REG_EN_AA,     0x00);      // 禁自动应答 -> 不发射
    nrf_write_reg(REG_EN_RXADDR, 0x01);      // 只开 pipe0
    nrf_write_reg(REG_SETUP_AW,  0x01);      // 2 字节地址宽度
    nrf_write_reg(REG_RX_PW_P0,  NRF_PKT_MAX);
    uint8_t addr[2] = { 0xAA, 0x00 };        // 小端：0x00AA
    nrf_write_multi(REG_RX_ADDR_P0, addr, 2);
    // CONFIG: PWR_UP(1) | PRIM_RX(1)，EN_CRC 清零 -> 前缀匹配即收
    nrf_write_reg(REG_CONFIG, 0x03);
}

// ---------------- 初始化 + 握手探测 ----------------
bool nrf_sniffer_init(void) {
    if (!pins_configured()) {
        Serial.println("[nrf] pins not configured (NRF_PIN_* is -1) — sniffer disabled");
        return false;
    }
    if (s_hw_ok) return true;

    pinMode(NRF_PIN_CE,  OUTPUT);
    pinMode(NRF_PIN_CSN, OUTPUT);
    digitalWrite(NRF_PIN_CE, LOW);
    digitalWrite(NRF_PIN_CSN, HIGH);
    // FSPI 独占总线；SS 传 -1，CSN 由软件手动拉（交给 SPI 外设接管会盖掉 digitalWrite）
    s_spi.begin(NRF_PIN_SCK, NRF_PIN_MISO, NRF_PIN_MOSI, -1);
    delay(150);                              // nRF24 上电稳定需要 ~100ms

    // 握手：写已知值到 RF_CH 再读回
    nrf_write_reg(REG_RF_CH, 76);
    uint8_t back = nrf_read_reg(REG_RF_CH);
    if (back != 76) {
        Serial.printf("[nrf] chip handshake FAILED (RF_CH readback 0x%02X, want 0x4C) — check wiring\n", back);
        Serial.printf("[nrf] expected: CE=%d CSN=%d SCK=%d MISO=%d MOSI=%d, VCC=3.3V\n",
                      NRF_PIN_CE, NRF_PIN_CSN, NRF_PIN_SCK, NRF_PIN_MISO, NRF_PIN_MOSI);
        s_hw_ok = false;
        return false;
    }
    nrf_config_promiscuous();
    s_hw_ok = true;

    portENTER_CRITICAL(&s_mux);
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.available = true;
    s_stats.version = 1;
    s_stats.next_seq = 1;
    s_stats.min_ch = s_min_ch;
    s_stats.max_ch = s_max_ch;
    s_stats.dwell_ms = s_dwell_ms;
    s_stats.rate_mode = s_rate_mode;
    portEXIT_CRITICAL(&s_mux);

    Serial.println("[nrf] nRF24L01+ handshake OK, promiscuous RX ready (passive, no TX)");
    return true;
}

bool nrf_sniffer_available(void) { return s_hw_ok; }
bool nrf_sniffer_active(void)    { return s_active; }

// ---------------- 存包（NRF 任务内，短临界区）----------------
static void nrf_store_packet(uint8_t ch, uint8_t rate, const uint8_t * data, uint8_t len) {
    portENTER_CRITICAL(&s_mux);
    // 环形缓冲：新包顶到 [0]，其余后移（<=8 个小结构，手写循环避免 memmove）
    for (int i = NRF_KEEP_LAST - 1; i > 0; i--) s_stats.pkts[i] = s_stats.pkts[i - 1];
    s_stats.pkts[0].seq  = s_stats.next_seq++;
    s_stats.pkts[0].ch   = ch;
    s_stats.pkts[0].rate = rate;
    s_stats.pkts[0].len  = len;
    for (uint8_t j = 0; j < len && j < NRF_PKT_MAX; j++) s_stats.pkts[0].data[j] = data[j];
    if (s_stats.pkt_count < NRF_KEEP_LAST) s_stats.pkt_count++;
    s_stats.packets++;
    s_stats.version++;
    portEXIT_CRITICAL(&s_mux);
}

// ---------------- 主循环任务 ----------------
static void nrf_task(void * arg) {
    (void)arg;
    uint8_t ch = (s_fixed_ch >= 0) ? (uint8_t)s_fixed_ch : s_min_ch;
    uint8_t rate_idx = (s_rate_mode == NRF_RATE_ALL) ? 0 : (uint8_t)s_rate_mode;
    nrf_set_rate(rate_idx);
    nrf_set_channel(ch);
    digitalWrite(NRF_PIN_CE, HIGH);          // 进 RX
    delay(2);
    nrf_flush_rx();

    uint32_t dwell_start = millis();
    uint16_t act_hits = 0, act_samples = 0;   // 当前信道的 RPD 命中/采样数
    while (s_active) {
        // RPD 采样：每个 1ms 片内连读 4 次，把量化台阶从 1/20 细化到 1/80
        for (int k = 0; k < 4; k++) {
            if ((nrf_read_reg(REG_RPD) & 0x01) != 0) act_hits++;
            act_samples++;
        }

        uint8_t status = nrf_read_reg(REG_STATUS);
        // RX FIFO 非空：pipe 号 3..7 表示空（nRF24L01+ 数据手册）
        if ((status & STATUS_RX_DR) || ((status & STATUS_RX_P_NO) <= 5)) {
            nrf_write_reg(REG_STATUS, STATUS_RX_DR);   // 写 1 清 RX_DR
            uint8_t buf[NRF_PKT_MAX];
            s_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
            digitalWrite(NRF_PIN_CSN, LOW);
            nrf_xfer(CMD_R_RX_PAYLOAD);
            for (uint8_t i = 0; i < NRF_PKT_MAX; i++) buf[i] = nrf_xfer(0xFF);
            digitalWrite(NRF_PIN_CSN, HIGH);
            s_spi.endTransaction();
            nrf_store_packet(ch, rate_idx, buf, NRF_PKT_MAX);
            nrf_flush_rx();                            // 丢掉可能残留的后续包
        }

        // 跳频模式下到点换信道/速率（窗口和停留时间每圈重读，运行中可改）
        if (s_fixed_ch < 0 && (millis() - dwell_start) >= s_dwell_ms) {
            dwell_start = millis();

            // 本信道活动度入库（0..255），供频谱图取用
            uint8_t lvl = (act_samples > 0)
                        ? (uint8_t)((uint32_t)act_hits * 255u / act_samples) : 0;
            act_hits = act_samples = 0;
            portENTER_CRITICAL(&s_mux);
            if (ch < NRF_CH_NUM) {
                // nRF24 的 RPD 只在"真的收到包"时置位，拿不到连续底噪，
                // 所以单次命中会表现为孤立亮点。这里做峰值保持 + 衰减：
                // 新值取 max，旧值每轮掉 1/8，命中就会留下一条会淡出的痕迹，
                // 视觉上才连成"瀑布"，而不是密密麻麻的小点。
                uint8_t prev = s_act_live[ch];
                uint8_t decayed = (prev > 6) ? (uint8_t)(prev - prev / 8u) : 0;
                s_act_live[ch] = (lvl > decayed) ? lvl : decayed;
            }
            portEXIT_CRITICAL(&s_mux);

            ch++;
            if (ch > s_max_ch || ch < s_min_ch) {
                ch = s_min_ch;
                // 一轮扫完：把 live 提升为 out，通知 UI 可以翻新一列
                portENTER_CRITICAL(&s_mux);
                memcpy(s_act, s_act_live, sizeof(s_act));
                s_pass_seq++;
                portEXIT_CRITICAL(&s_mux);
                if (s_rate_mode == NRF_RATE_ALL) {     // 扫完一圈换速率
                    rate_idx = (uint8_t)((rate_idx + 1) % 3);
                    nrf_set_rate(rate_idx);
                }
            }
            nrf_set_channel(ch);
            nrf_flush_rx();
            portENTER_CRITICAL(&s_mux);
            s_stats.channel = ch;
            s_stats.rate_idx = rate_idx;
            portEXIT_CRITICAL(&s_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(1));                  // 1ms 轮询：dwell 5ms 时也不会漏掉切换点
    }

    digitalWrite(NRF_PIN_CE, LOW);                     // 退出 RX
    s_task = NULL;                                     // 先清句柄再自删，stop() 靠它等退出
    vTaskDelete(NULL);
}

void nrf_sniffer_start(int fixed_ch) {
    if (s_active) return;
    if (!nrf_sniffer_init()) return;                   // 引脚没配 / 握手失败 -> 不启动

    s_fixed_ch = fixed_ch;
    portENTER_CRITICAL(&s_mux);
    s_stats.active = true;
    s_stats.hopping = (fixed_ch < 0);
    s_stats.channel = (fixed_ch >= 0) ? (uint8_t)fixed_ch : s_min_ch;
    s_stats.rate_idx = (s_rate_mode == NRF_RATE_ALL) ? 0 : (uint8_t)s_rate_mode;
    s_stats.rate_mode = s_rate_mode;
    s_stats.min_ch = s_min_ch;
    s_stats.max_ch = s_max_ch;
    s_stats.dwell_ms = s_dwell_ms;
    portEXIT_CRITICAL(&s_mux);

    s_active = true;
    if (xTaskCreatePinnedToCore(nrf_task, "nrf", 4096, NULL, 1, &s_task, 1) != pdPASS) {
        Serial.println("[nrf] task create FAILED");
        s_active = false;
        portENTER_CRITICAL(&s_mux);
        s_stats.active = false;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    if (fixed_ch >= 0)
        Serial.printf("[nrf] sniffing fixed CH%d %s\n", fixed_ch,
                      (s_rate_mode == NRF_RATE_ALL) ? "(rates cycling)" : nrf_rate_name(s_rate_mode));
    else
        Serial.printf("[nrf] sniffing CH%d..%d, dwell %ums, %s\n",
                      (int)s_min_ch, (int)s_max_ch, (unsigned)s_dwell_ms,
                      (s_rate_mode == NRF_RATE_ALL) ? "rates 1M/2M/250K cycling"
                                                    : nrf_rate_name(s_rate_mode));
}

void nrf_sniffer_stop(void) {
    if (!s_active) return;
    s_active = false;
    for (int i = 0; i < 60 && s_task != NULL; i++) delay(5);   // 等任务退出（最长 ~300ms）
    s_task = NULL;
    portENTER_CRITICAL(&s_mux);
    s_stats.active = false;
    portEXIT_CRITICAL(&s_mux);
    Serial.printf("[nrf] stopped, %u packets captured\n", (unsigned)s_stats.packets);
}

void nrf_clear_stats(void) {
    portENTER_CRITICAL(&s_mux);
    bool avail = s_stats.available, act = s_stats.active;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_act, 0, sizeof(s_act));
    memset(s_act_live, 0, sizeof(s_act_live));
    s_stats.available = avail;
    s_stats.active = act;
    s_stats.version = 1;
    s_stats.next_seq = 1;
    s_stats.channel = (s_fixed_ch >= 0) ? (uint8_t)s_fixed_ch : s_min_ch;
    s_stats.rate_mode = s_rate_mode;
    s_stats.min_ch = s_min_ch;
    s_stats.max_ch = s_max_ch;
    s_stats.dwell_ms = s_dwell_ms;
    portEXIT_CRITICAL(&s_mux);
}

void nrf_get_stats(nrf_stats_t * out) {
    if (out == NULL) return;
    portENTER_CRITICAL(&s_mux);
    memcpy(out, &s_stats, sizeof(*out));     // ≈330B，微秒级
    portEXIT_CRITICAL(&s_mux);
}