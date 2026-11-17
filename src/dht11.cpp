// DHT11 读取（自己实现，替代 Adafruit DHT 库）
//
// 为什么要换掉那个库：它的 read() 会 noInterrupts()，然后 expectPulse() 用
// digitalRead 死循环等电平，超时值 microsecondsToClockCycles(1000) 在 240MHz
// 上是 24 万次循环；传感器没应答时一次就占掉几十毫秒，连续几次就把中断关了
// 几百毫秒 → Interrupt wdt timeout on CPU0（实测栈：DHT::expectPulse ←
// read ← readTemperature ← LVGL 定时器）。nRF24 扫描时系统中断变密，位时序
// 被打断，正好触发这条路径。
//
// 这里的实现：
//   · 关中断的时间是有硬上限的：正常一帧约 5ms，最坏（传感器无应答）<8ms，
//     远低于 300ms 的看门狗；
//   · 每个等待循环都用 micros() 封顶，任何一步超时就立刻退出，不会挂死；
//   · 校验和不对直接算失败，不返回垃圾数据。

#include <Arduino.h>

#include "dht11.h"

#define DHT_WAIT_RESPONSE_US  120   // 传感器拉起低电平的最大等待
#define DHT_BIT_LOW_US         90   // 每 bit 的 50µs 低电平上限
#define DHT_BIT_HIGH_US       100   // 26µs(0) / 70µs(1) 的高电平上限
#define DHT_ONE_THRESHOLD_US   45   // 高电平 > 45µs 判为 1

static int s_pin = -1;

bool dht11_begin(int pin) {
    if (pin < 0) return false;
    s_pin = pin;
    pinMode(pin, INPUT_PULLUP);
    return true;
}

bool dht11_read(float * temp_c, float * humi_pct) {
    if (s_pin < 0 || temp_c == NULL || humi_pct == NULL) return false;

    uint8_t data[5] = { 0, 0, 0, 0, 0 };
    bool ok = false;

    // 起始信号：拉低 ≥18ms（delay 会让出 CPU，不关中断）
    pinMode(s_pin, OUTPUT);
    digitalWrite(s_pin, LOW);
    delay(18);

    noInterrupts();
    pinMode(s_pin, INPUT_PULLUP);

    uint32_t t0;
    int      i;

    // 传感器应答：20~40µs 后拉低
    t0 = micros();
    while (digitalRead(s_pin) == HIGH) {
        if (micros() - t0 > DHT_WAIT_RESPONSE_US) goto done;
    }
    // 80µs 低
    t0 = micros();
    while (digitalRead(s_pin) == LOW) {
        if (micros() - t0 > DHT_WAIT_RESPONSE_US) goto done;
    }
    // 80µs 高
    t0 = micros();
    while (digitalRead(s_pin) == HIGH) {
        if (micros() - t0 > DHT_WAIT_RESPONSE_US) goto done;
    }

    for (i = 0; i < 40; i++) {
        t0 = micros();
        while (digitalRead(s_pin) == LOW) {                 // 每位前面的 50µs 低
            if (micros() - t0 > DHT_BIT_LOW_US) goto done;
        }
        t0 = micros();
        while (digitalRead(s_pin) == HIGH) {                // 高电平宽度决定 0/1
            if (micros() - t0 > DHT_BIT_HIGH_US) goto done;
        }
        data[i >> 3] = (uint8_t)(data[i >> 3] << 1);
        if ((micros() - t0) > DHT_ONE_THRESHOLD_US) data[i >> 3] |= 1;
    }
    ok = true;

done:
    interrupts();

    if (!ok) return false;

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) return false;

    *humi_pct = (float)data[0] + (float)data[1] * 0.1f;
    *temp_c  = (float)data[2] + (float)data[3] * 0.1f;
    return true;
}