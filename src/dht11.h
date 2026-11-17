#ifndef DHT11_H
#define DHT11_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool dht11_begin(int pin);
bool dht11_read(float * temp_c, float * humi_pct);

#ifdef __cplusplus
}
#endif

#endif