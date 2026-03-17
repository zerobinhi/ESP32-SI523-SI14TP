#ifndef SI14TP_H
#define SI14TP_H

#include <esp_log.h>
#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// -------------------------- 引脚与 I2C 配置 --------------------------
#define SI14TP_IIC_EN 15
#define SI14TP_RST_PIN 23
#define SI14TP_INT_PIN 22

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 18
#define I2C_MASTER_SDA_IO 19
#define I2C_MASTER_FREQ_HZ 100000
#define SI14TP_I2C_ADDR 0x68

// -------------------------- 寄存器定义 --------------------------
#define SI14_REG_SENSE1 0x02   // Ch1-Ch2 灵敏度
#define SI14_REG_SENSE2 0x03   // Ch3-Ch4 灵敏度
#define SI14_REG_SENSE3 0x04   // Ch5-Ch6 灵敏度
#define SI14_REG_SENSE4 0x05   // Ch7-Ch8 灵敏度
#define SI14_REG_SENSE5 0x06   // Ch9-Ch10 灵敏度
#define SI14_REG_SENSE6 0x07   // Ch11-Ch12 灵敏度
#define SI14_REG_SENSE7 0x22   // Ch13-Ch14 灵敏度
#define SI14_REG_CFIG 0x08     // 通用配置寄存器
#define SI14_REG_CTRL 0x09     // 通用控制寄存器
#define SI14_REG_REFRST1 0x0A  // 通道参考复位1
#define SI14_REG_REFRST2 0x0B  // 通道参考复位2
#define SI14_REG_CHHOLD1 0x0C  // 通道感应控制1
#define SI14_REG_CHHOLD2 0x0D  // 通道感应控制2
#define SI14_REG_CALHOLD1 0x0E // 通道校准控制1
#define SI14_REG_CALHOLD2 0x0F // 通道校准控制2
#define SI14_REG_OUT1 0x10     // Ch1-Ch4 输出
#define SI14_REG_OUT2 0x11     // Ch5-Ch8 输出
#define SI14_REG_OUT3 0x12     // Ch9-Ch12 输出
#define SI14_REG_OUT4 0x13     // Ch13-Ch14 输出

#define SI14_REG_UNLOCK 0x3B // CTRL2 解锁寄存器
#define SI14_REG_CTRL2 0x3D  // 通用控制寄存器2 (用于跳过 FTC 校准)

// -------------------------- 外部变量声明 --------------------------
extern i2c_master_dev_handle_t si14tp_handle;

// -------------------------- 驱动接口 --------------------------
void si14tp_gpio_init(void);
void si14tp_hard_reset(void);
esp_err_t si14tp_init(void);
bool si14tp_check(void);
void si14tp_enter_sleep(void);
int si14tp_get_key(void); // 获取按键值，0代表无按键

#endif // SI14TP_H