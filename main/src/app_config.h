#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>

#define DEFAULT_AP_SSID "ESP32-SmartLock"
#define DEFAULT_AP_PASS "12345678"

#define INDEX_HTML_BUFFER_SIZE 32768
#define RESPONSE_DATA_BUFFER_SIZE 32768

#define I2C_MASTER_NUM I2C_NUM_0  // I2C port number
#define I2C_MASTER_SCL_IO 7       // GPIO for I2C master clock
#define I2C_MASTER_SDA_IO 6       // GPIO for I2C master data
#define I2C_MASTER_FREQ_HZ 100000 // I2C master clock frequency
#define SI523_I2C_ADDR 0x28       // I2C address of SI523
#define OLED_I2C_ADDR 0x3C        // I2C address of OLED display
#define SI14TP_I2C_ADDR 0x68      // I2C address of SI14TP
#define SI523_RST_PIN 10          // GPIO for SI523 reset
#define SI523_INT_PIN 0           // GPIO for SI523 interrupt
#define SI14TP_IIC_EN 21          // GPIO for SI14TP I2C enable
#define SI14TP_RST_PIN 19         // GPIO for SI14TP reset
#define SI14TP_INT_PIN 3          // GPIO for SI14TP interrupt
#define FINGERPRINT_TX_PIN 22     // GPIO for fingerprint module TX
#define FINGERPRINT_RX_PIN 23     // GPIO for fingerprint module RX
#define FINGERPRINT_CTL_PIN 20    // GPIO for fingerprint module control
#define FINGERPRINT_INT_PIN 2     // GPIO for fingerprint module interrupt

#define BUZZER_CTL_PIN 12

#define LOCK_LED_PIN 11

#define BATTERY_PIN 1 // ???

#define MAX_CARDS 20

#define TOUCH_PASSWORD_LEN 6
#define DEFAULT_PASSWORD "123456"
#define DEFAULT_SLEEP_TIME 60

#define true 1
#define false 0

#endif
