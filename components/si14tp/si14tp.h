#ifndef SI14TP_H
#define SI14TP_H

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "nvs_custom.h"
#include "app_config.h"

// -------------------------- register definitions --------------------------
#define SI14_REG_SENSE1 0x02   // ch1-ch2 sensitivity
#define SI14_REG_SENSE2 0x03   // ch3-ch4 sensitivity
#define SI14_REG_SENSE3 0x04   // ch5-ch6 sensitivity
#define SI14_REG_SENSE4 0x05   // ch7-ch8 sensitivity
#define SI14_REG_SENSE5 0x06   // ch9-ch10 sensitivity
#define SI14_REG_SENSE6 0x07   // ch11-ch12 sensitivity
#define SI14_REG_SENSE7 0x22   // ch13-ch14 sensitivity
#define SI14_REG_CFIG 0x08     // general config register
#define SI14_REG_CTRL 0x09     // general control register
#define SI14_REG_REFRST1 0x0A  // channel reference reset 1
#define SI14_REG_REFRST2 0x0B  // channel reference reset 2
#define SI14_REG_CHHOLD1 0x0C  // channel hold control 1
#define SI14_REG_CHHOLD2 0x0D  // channel hold control 2
#define SI14_REG_CALHOLD1 0x0E // channel calibration control 1
#define SI14_REG_CALHOLD2 0x0F // channel calibration control 2
#define SI14_REG_OUT1 0x10     // ch1-ch4 output
#define SI14_REG_OUT2 0x11     // ch5-ch8 output
#define SI14_REG_OUT3 0x12     // ch9-ch12 output
#define SI14_REG_OUT4 0x13     // ch13-ch14 output

#define SI14_REG_UNLOCK 0x3B // ctrl2 unlock register
#define SI14_REG_CTRL2 0x3D  // general control register 2 (skip ftc calibration)

// -------------------------- external globals --------------------------
extern bool g_i2c_service_installed;       // whether i2c service is installed
extern i2c_master_bus_handle_t bus_handle; // i2c bus handle
extern bool g_gpio_isr_service_installed;  // whether gpio interrupt service is installed
extern QueueHandle_t password_queue;

// -------------------------- public interfaces --------------------------

/* initialize i2c bus and si14tp device */
void si14tp_i2c_init(void);

/* configure gpio pins */
void si14tp_gpio_init(void);

/* perform hardware reset of si14tp */
void si14tp_hard_reset(void);

/* check if device responds correctly */
bool si14tp_check(void);

/* initialize si14tp registers and settings */
void si14tp_init(void);

/* put si14tp into low power sleep mode */
void si14tp_enter_sleep(void);

/* read current touched key (returns 0 if no key pressed) */
int si14tp_get_key(void);

/* overall initialization entry, creates task */
esp_err_t si14tp_initialization(void);

#endif // SI14TP_H