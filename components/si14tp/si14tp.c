#include "si14tp.h"

static const char *TAG = "si14tp";

// -------------------------- 静态辅助函数 --------------------------
static esp_err_t si14tp_write_reg(uint8_t reg, uint8_t data)
{
    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &reg, .buffer_size = 1},
        {.write_buffer = &data, .buffer_size = 1},
    };
    esp_err_t err = i2c_master_multi_buffer_transmit(si14tp_handle, buffers, 2, portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Write reg 0x%02X failed", reg);
    }
    return err;
}

static esp_err_t si14tp_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t err = i2c_master_transmit_receive(si14tp_handle, &reg, 1, data, 1, portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Read reg 0x%02X failed", reg);
    }
    return err;
}

// -------------------------- 控制接口 --------------------------
esp_err_t si14tp_init(void)
{
    gpio_set_level(SI14TP_IIC_EN, 0); // 使能 I2C
    vTaskDelay(pdMS_TO_TICKS(20));

    // 软件复位 (SRST=1, SLEEP=1 -> 0x0F)
    si14tp_write_reg(SI14_REG_CTRL, 0x0F);
    vTaskDelay(pdMS_TO_TICKS(10));
    // 释放复位 (SRST=0, SLEEP=1 -> 0x07)
    si14tp_write_reg(SI14_REG_CTRL, 0x07);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 灵敏度设置 (0x77 代表高灵敏度配置)
    for (uint8_t reg = SI14_REG_SENSE1; reg <= SI14_REG_SENSE6; reg++)
    {
        si14tp_write_reg(reg, 0x77);
    }
    si14tp_write_reg(SI14_REG_SENSE7, 0x77); // 不要漏掉通道 13/14

    // 通用配置: 开启自动快慢模式，设高中断
    si14tp_write_reg(SI14_REG_CFIG, 0x1B);

    // 开启所有通道的感应和校准 (0x00 表示全部启用)
    si14tp_write_reg(SI14_REG_CHHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD2, 0x00);
    si14tp_write_reg(SI14_REG_REFRST1, 0x00);
    si14tp_write_reg(SI14_REG_REFRST2, 0x00);
    si14tp_write_reg(SI14_REG_CALHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CALHOLD2, 0x00);

    vTaskDelay(pdMS_TO_TICKS(40));
    gpio_set_level(SI14TP_IIC_EN, 1); // 结束使能，进入低功耗待命

    ESP_LOGI(TAG, "SI14TP init configured based on datasheet V0.17");
    return ESP_OK;
}

bool si14tp_check(void)
{
    uint8_t temp = 0;
    gpio_set_level(SI14TP_IIC_EN, 0);

    si14tp_write_reg(SI14_REG_SENSE1, 0xAA);
    si14tp_read_reg(SI14_REG_SENSE1, &temp);

    if (temp == 0xAA)
    {
        si14tp_write_reg(SI14_REG_SENSE1, 0x77); // 恢复正确配置
        gpio_set_level(SI14TP_IIC_EN, 1);
        return true;
    }

    gpio_set_level(SI14TP_IIC_EN, 1);
    return false;
}

void si14tp_gpio_init(void)
{
    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << SI14TP_RST_PIN) | (1ULL << SI14TP_IIC_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);

    // 默认失能 I2C (拉高)
    gpio_set_level(SI14TP_IIC_EN, 1);
}

void si14tp_hard_reset(void)
{
    gpio_set_level(SI14TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(SI14TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(SI14TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
}

void si14tp_enter_sleep(void)
{
    gpio_set_level(SI14TP_IIC_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    si14tp_write_reg(SI14_REG_CTRL, 0x07);
    si14tp_write_reg(SI14_REG_REFRST1, 0x00);
    si14tp_write_reg(SI14_REG_REFRST2, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD2, 0x30);
    si14tp_write_reg(SI14_REG_CTRL, 0x07);
    si14tp_write_reg(0x3B, 0xA5);
    si14tp_write_reg(0x3D, 0x01);
    gpio_set_level(SI14TP_IIC_EN, 1);
}

// -------------------------- 数据获取解析 --------------------------
int si14tp_get_key(void)
{
    uint8_t buf[4] = {0};

    gpio_set_level(SI14TP_IIC_EN, 0); // 使能读取
    si14tp_read_reg(SI14_REG_OUT1, &buf[0]);
    si14tp_read_reg(SI14_REG_OUT2, &buf[1]);
    si14tp_read_reg(SI14_REG_OUT3, &buf[2]);
    si14tp_read_reg(SI14_REG_OUT4, &buf[3]);
    gpio_set_level(SI14TP_IIC_EN, 1);

    // 解析按键
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            int key = i * 4 + j + 1;
            if (key > 14)
                return 0; // 超过 14 通道退出

            // 提取当前通道的 2 个 bit
            // j=0(bit0-1), j=1(bit2-3), j=2(bit4-5), j=3(bit6-7)
            uint8_t ch_bits = (buf[i] >> (j * 2)) & 0x03;

            // 只有 bit0 和 bit1 都是 1 时才返回 key
            if (ch_bits == 0x03)
            {
                return key;
            }
        }
    }
    return 0; // 无按键按下
}