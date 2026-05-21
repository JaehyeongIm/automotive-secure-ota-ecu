#pragma once
#include <stdint.h>

/* 1 = 15cm threshold,  2 = 25cm threshold (OTA v2) */
#define APP_VERSION 1

uint16_t hcsr04_measure_cm(void);
