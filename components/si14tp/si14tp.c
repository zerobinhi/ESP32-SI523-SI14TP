#include "si14tp.h"

static const char *TAG = "si14tp";

SemaphoreHandle_t si14tp_semaphore = NULL;
i2c_master_dev_handle_t si14tp_handle = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == SI14TP_INT_PIN)
    {
        xSemaphoreGiveFromISR(si14tp_semaphore, NULL);
    }
}

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

void si14tp_i2c_init(void)
{
    /* create binary semaphore */
    si14tp_semaphore = xSemaphoreCreateBinary();
    if (si14tp_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Semaphore creation failed");
    }

    /* initialize I2C bus if not installed */
    if (!g_i2c_service_installed)
    {
        i2c_master_bus_config_t i2c_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_MASTER_NUM,
            .scl_io_num = I2C_MASTER_SCL_IO,
            .sda_io_num = I2C_MASTER_SDA_IO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };

        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &bus_handle));
        g_i2c_service_installed = true;
        ESP_LOGI(TAG, "I2C bus initialized");
    }

    /* add si14tp device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SI14TP_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &si14tp_handle));
    ESP_LOGI(TAG, "si14tp device added");
}

void si14tp_gpio_init(void)
{
    /* configure reset pin */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&rst_cfg);

    /* configure i2c en pin */
    gpio_config_t cen_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_IIC_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cen_cfg);
    gpio_set_level(SI14TP_IIC_EN, 1); // 默认不使能I2C

    /* configure interrupt pin*/
    gpio_config_t si14tp_irq_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&si14tp_irq_cfg);

    /* install GPIO ISR service if needed */
    if (!g_gpio_isr_service_installed)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
        ESP_LOGI(TAG, "GPIO ISR service installed");
    }

    gpio_isr_handler_add(SI14TP_INT_PIN, gpio_isr_handler, (void *)SI14TP_INT_PIN);
    ESP_LOGI(TAG, "si14tp INT pin ISR handler added");
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

void si14tp_init(void)
{
    gpio_set_level(SI14TP_IIC_EN, 0); // 使能 I2C

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

void si14tp_task(void *arg)
{
    while (1)
    {
        if (xSemaphoreTake(si14tp_semaphore, portMAX_DELAY))
        {
            // 获取按键并直接打印
            int key = si14tp_get_key();
            if (key > 0)
            {
                ESP_LOGI(TAG, "T%d TOUCH", key);
            }
        }
    }
}

esp_err_t si14tp_initialization(void)
{
    si14tp_i2c_init();
    si14tp_gpio_init();
    si14tp_hard_reset();
    si14tp_init();

    xTaskCreate(si14tp_task, "si14tp_task", 2048, NULL, 10, NULL);

    return ESP_OK;
}
