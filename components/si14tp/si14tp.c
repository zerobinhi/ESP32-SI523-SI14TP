#include "si14tp.h"

static const char *TAG = "si14tp";

SemaphoreHandle_t si14tp_semaphore = NULL;
i2c_master_dev_handle_t si14tp_handle = NULL;

char g_touch_password[TOUCH_PASSWORD_LEN + 1] = {0}; // Stored password
char g_input_password[TOUCH_PASSWORD_LEN + 1] = {0}; // Current input buffer
uint8_t g_input_len = 0;

static const char key_map[15] = {0, 0, '3', '6', '9', '1', '4', '7', '*', '5', '2', '8', '0', '#', 0};

/* gpio interrupt handler */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    ESP_EARLY_LOGI(TAG, "Password touch detected");
    gpio_set_intr_type(SI14TP_INT_PIN, GPIO_INTR_NEGEDGE);
    gpio_intr_enable(SI14TP_INT_PIN);
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == SI14TP_INT_PIN)
    {
        // gpio_intr_disable(SI14TP_INT_PIN);
        xSemaphoreGiveFromISR(si14tp_semaphore, NULL);
    }
}

// -------------------------- static helper functions --------------------------
/* write one byte to register */
static esp_err_t si14tp_write_reg(uint8_t reg, uint8_t data)
{
    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &reg, .buffer_size = 1},
        {.write_buffer = &data, .buffer_size = 1},
    };

    esp_err_t err = i2c_master_multi_buffer_transmit(si14tp_handle, buffers, 2, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "write reg 0x%02x failed", reg);
    }

    return err;
}

/* read one byte from register */
static esp_err_t si14tp_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t err = i2c_master_transmit_receive(si14tp_handle, &reg, 1, data, 1, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "read reg 0x%02x failed", reg);
    }

    return err;
}

/* initialize i2c and device */
void si14tp_i2c_init(void)
{
    /* create binary semaphore for interrupt sync */
    si14tp_semaphore = xSemaphoreCreateBinary();

    if (si14tp_semaphore == NULL)
    {
        ESP_LOGE(TAG, "semaphore creation failed");
    }

    /* initialize i2c bus if not already installed */
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

        ESP_LOGI(TAG, "i2c bus initialized");
    }

    /* add si14tp device to i2c bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SI14TP_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &si14tp_handle));

    ESP_LOGI(TAG, "si14tp device added");
}

/* initialize gpio pins */
void si14tp_gpio_init(void)
{
    /* configure reset pin as output */
    gpio_config_t si14tp_rst_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&si14tp_rst_cfg);

    /* configure i2c enable pin */
    gpio_config_t cen_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_IIC_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cen_cfg);

    /* disable i2c by default */
    gpio_set_level(SI14TP_IIC_EN, 1);

    /* configure interrupt pin */
    gpio_config_t si14tp_irq_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&si14tp_irq_cfg);

    /* install gpio isr service if not installed */
    if (!g_gpio_isr_service_installed)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;

        ESP_LOGI(TAG, "gpio isr service installed");
    }

    /* attach interrupt handler */
    gpio_isr_handler_add(SI14TP_INT_PIN, gpio_isr_handler, (void *)SI14TP_INT_PIN);

    ESP_LOGI(TAG, "si14tp int pin isr handler added");
    size_t len = sizeof(g_touch_password);

    esp_err_t err = nvs_custom_get_str(NULL, "touch", "touch_password", g_touch_password, &len);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "password not found, using default");
        strcpy(g_touch_password, DEFAULT_PASSWORD);

        nvs_custom_set_str(NULL, "touch", "touch_password", g_touch_password);
    }
    else
    {
        ESP_LOGI(TAG, "password loaded from NVS");
    }
}

/* hardware reset sequence */
void si14tp_hard_reset(void)
{
    gpio_set_level(SI14TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    gpio_set_level(SI14TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    gpio_set_level(SI14TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
}

/* check device communication */
bool si14tp_check(void)
{
    uint8_t temp = 0;

    /* enable i2c */
    gpio_set_level(SI14TP_IIC_EN, 0);

    /* write test value */
    si14tp_write_reg(SI14_REG_SENSE1, 0xAA);

    /* read back */
    si14tp_read_reg(SI14_REG_SENSE1, &temp);

    if (temp == 0xAA)
    {
        /* restore correct config */
        si14tp_write_reg(SI14_REG_SENSE1, 0x77);

        gpio_set_level(SI14TP_IIC_EN, 1);
        return true;
    }

    gpio_set_level(SI14TP_IIC_EN, 1);
    return false;
}

/* initialize si14tp registers */
void si14tp_init(void)
{
    /* enable i2c */
    gpio_set_level(SI14TP_IIC_EN, 0);

    /* set sensitivity for all channels */
    for (uint8_t reg = SI14_REG_SENSE1; reg <= SI14_REG_SENSE6; reg++)
    {
        si14tp_write_reg(reg, 0x77);
    }

    /* configure channel 13 and 14 */
    si14tp_write_reg(SI14_REG_SENSE7, 0x77);

    /* general config: enable auto fast/slow mode and high interrupt */
    si14tp_write_reg(SI14_REG_CFIG, 0x1B);

    /* enable all channels */
    si14tp_write_reg(SI14_REG_CHHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD2, 0x00);

    /* enable reference reset */
    si14tp_write_reg(SI14_REG_REFRST1, 0x00);
    si14tp_write_reg(SI14_REG_REFRST2, 0x00);

    /* enable calibration */
    si14tp_write_reg(SI14_REG_CALHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CALHOLD2, 0x00);

    /* wait for device stable */
    vTaskDelay(pdMS_TO_TICKS(40));

    /* disable i2c to enter low power standby */
    gpio_set_level(SI14TP_IIC_EN, 1);
}

/* enter low power sleep mode */
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

    /* unlock and skip calibration */
    si14tp_write_reg(0x3B, 0xA5);
    si14tp_write_reg(0x3D, 0x01);

    gpio_set_level(SI14TP_IIC_EN, 1);
}

// -------------------------- data acquisition --------------------------

/* read touch key status */
int si14tp_get_key(void)
{
    uint8_t buf[4] = {0};

    /* enable i2c for reading */
    gpio_set_level(SI14TP_IIC_EN, 0);

    si14tp_read_reg(SI14_REG_OUT1, &buf[0]);
    si14tp_read_reg(SI14_REG_OUT2, &buf[1]);
    si14tp_read_reg(SI14_REG_OUT3, &buf[2]);
    si14tp_read_reg(SI14_REG_OUT4, &buf[3]);

    gpio_set_level(SI14TP_IIC_EN, 1);

    /* parse touch keys */
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            int key = i * 4 + j + 1;

            /* only support 14 channels */
            if (key > 14)
                return 0;

            /* extract 2-bit status of each channel */
            uint8_t ch_bits = (buf[i] >> (j * 2)) & 0x03;

            /* valid touch when both bits are 1 */
            if (ch_bits == 0x03)
            {
                return key;
            }
        }
    }

    /* no key pressed */
    return 0;
}

/* task to handle touch interrupt */
void si14tp_task(void *arg)
{
    char key = 0;
    while (1)
    {
        /* wait for interrupt */
        if (xSemaphoreTake(si14tp_semaphore, portMAX_DELAY))
        {
            key = key_map[si14tp_get_key()];

            if (key != 0)
            {
                ESP_LOGI(TAG, "key %c pressed", key);
                if (key >= '0' && key <= '9')
                {
                    if (g_input_len < TOUCH_PASSWORD_LEN)
                    {
                        g_input_password[g_input_len++] = key;
                        g_input_password[g_input_len] = '\0';
                    }
                }
                else if (key == '*')
                {
                    // Clear input
                    g_input_len = 0;
                    memset(g_input_password, 0, sizeof(g_input_password));
                }
                else if (key == '#')
                {
                    // Confirm password
                    if (g_input_len == TOUCH_PASSWORD_LEN)
                    {
                        if (strcmp(g_input_password, g_touch_password) == 0)
                        {
                            ESP_LOGI(TAG, "password verification OK");
                            uint8_t message = 0x01; // Success
                            xQueueSend(password_queue, &message, pdMS_TO_TICKS(1000));
                        }
                        else
                        {
                            ESP_LOGW(TAG, "password verification failed");
                            uint8_t message = 0x00; // Failure
                            xQueueSend(password_queue, &message, pdMS_TO_TICKS(1000));
                        }
                    }
                    else
                    {
                        ESP_LOGW(TAG, "invalid password length: %d", g_input_len);
                    }

                    // Reset input state
                    g_input_len = 0;
                    memset(g_input_password, 0, sizeof(g_input_password));
                }
                // else
                // {
                //     gpio_set_intr_type(SI14TP_INT_PIN, GPIO_INTR_POSEDGE);
                //     gpio_intr_enable(SI14TP_INT_PIN);
                // }
            }
        }
    }
}

/* overall initialization entry */
esp_err_t si14tp_initialization(void)
{
    si14tp_i2c_init();
    si14tp_gpio_init();
    si14tp_hard_reset();
    si14tp_init();

    xTaskCreate(si14tp_task, "si14tp_task", 8192, NULL, 10, NULL);

    return ESP_OK;
}