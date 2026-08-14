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

/* ADC 配置
   注意: ADC_CHANNEL 是 adc_channel_t 枚举, 不是 GPIO 编号, 二者不能互换。
   ESP32-C6 上 ADC_CHANNEL_1 = GPIO1, 电池即接在 GPIO1。 */
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_1
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

esp_err_t battery_init(void);

#endif
