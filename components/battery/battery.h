#ifndef BATTERY_H
#define BATTERY_H

#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "oled.h"
#include "oled_fonts.h"

extern uint8_t g_card_count;

/* 分压电阻(单位 Ω) */
#define R_UPPER 470000.0f
#define R_LOWER 470000.0f

/* 电池电压阈值(mV) */
#define BATTERY_FULL_MV 4100.0f       /* ≈90~100% */
#define BATTERY_TWO_THIRD_MV 3800.0f  /* ≈60% */
#define BATTERY_ONE_THIRD_MV 3500.0f  /* ≈30% */

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_1
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

esp_err_t battery_init(void);

#endif
