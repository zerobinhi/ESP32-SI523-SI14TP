#include "si14tp.h"
#include "esp_rom_sys.h"

static const char *TAG = "si14tp";

/* 默认每通道触摸灵敏度(每个 nibble 0x0..0xF, 数值越大越灵敏)。
   si14tp_init() 与 si14tp_check() 共用此宏, 避免自检把灵敏度悄悄复位成别的值。 */
#define SI14_SENS_DEFAULT 0x77

SemaphoreHandle_t si14tp_semaphore = NULL;
i2c_master_dev_handle_t si14tp_handle = NULL;

char g_touch_password[TOUCH_PASSWORD_LEN + 1] = {0}; // 已存储密码
char g_input_password[TOUCH_PASSWORD_LEN + 1] = {0}; // 当前输入缓冲
uint8_t g_input_len = 0;

/* 通道索引 0..14 -> 按键字符(0 表示无/未用)。key1 与 key14 为未使用通道。 */
static const char key_map[15] = {0, 0, '5', '2', '8', '3', '6', '9', '#', '0', '*', '7', '4', '1', 0};

/* GPIO 中断处理: 仅唤醒任务。保持精简——不在 ISR 内打印日志或调用
   gpio_set_intr_type(后者非 ISR 安全, 且此处多余)。 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    gpio_set_intr_type(SI14TP_INT_PIN, GPIO_INTR_NEGEDGE);
    ESP_DRAM_LOGI(TAG, "Password touch detected");
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == SI14TP_INT_PIN)
    {
        xSemaphoreGiveFromISR(si14tp_semaphore, NULL);
    }
}

// -------------------------- 静态辅助函数 --------------------------
/* 向寄存器写一个字节 */
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

/* 从寄存器读一个字节 */
static esp_err_t si14tp_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t err = i2c_master_transmit_receive(si14tp_handle, &reg, 1, data, 1, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "read reg 0x%02x failed", reg);
    }

    return err;
}

/* 初始化 I2C 总线与 si14tp 设备 */
void si14tp_i2c_init(void)
{
    /* 创建二值信号量用于中断同步(仅创建一次, 防重复调用泄漏) */
    if (si14tp_semaphore == NULL)
    {
        si14tp_semaphore = xSemaphoreCreateBinary();
        if (si14tp_semaphore == NULL)
        {
            ESP_LOGE(TAG, "semaphore creation failed");
        }
    }

    /* 若 I2C 总线尚未安装则安装 */
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

        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle));
        g_i2c_service_installed = true;

        ESP_LOGI(TAG, "i2c bus initialized");
    }

    /* 将 si14tp 设备挂到总线上(判空, 防重复 add_device 触发 abort) */
    if (si14tp_handle == NULL)
    {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SI14TP_I2C_ADDR,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };

        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &si14tp_handle));
        ESP_LOGI(TAG, "si14tp device added");
    }
}

/* 初始化 GPIO 引脚 */
void si14tp_gpio_init(void)
{
    /* 复位引脚配置为输出 */
    gpio_config_t si14tp_rst_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&si14tp_rst_cfg);

    /* I2C 使能引脚配置为输出 */
    gpio_config_t cen_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_IIC_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cen_cfg);

    /* 默认关闭 I2C(低功耗待机) */
    gpio_set_level(SI14TP_IIC_EN, 1);

    /* 中断引脚配置为输入, 下降沿触发 */
    gpio_config_t si14tp_irq_cfg = {
        .pin_bit_mask = (1ULL << SI14TP_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&si14tp_irq_cfg);

    /* 若 GPIO ISR 服务未安装则安装 */
    if (!g_gpio_isr_service_installed)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;

        ESP_LOGI(TAG, "gpio isr service installed");
    }

    /* 挂载中断处理函数 */
    gpio_isr_handler_add(SI14TP_INT_PIN, gpio_isr_handler, (void *)SI14TP_INT_PIN);
    ESP_LOGI(TAG, "si14tp int pin isr handler added");

    /* 从 NVS 读取密码, 未找到则用默认值并写回 */
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

/* 硬件复位时序(高电平复位脉冲, 低电平为运行态)。
   用 esp_rom_delay_us 而非 vTaskDelay, 保证 1ms/2ms 真实时序,
   不受 FreeRTOS tick 频率影响(100Hz tick 下 pdMS_TO_TICKS(1)=0)。 */
void si14tp_hard_reset(void)
{
    gpio_set_level(SI14TP_RST_PIN, 0);
    esp_rom_delay_us(1000);

    gpio_set_level(SI14TP_RST_PIN, 1);
    esp_rom_delay_us(2000);

    gpio_set_level(SI14TP_RST_PIN, 0);
    esp_rom_delay_us(1000);
}

/* 通过写/读回测试检查设备通信 */
bool si14tp_check(void)
{
    uint8_t temp = 0;

    gpio_set_level(SI14TP_IIC_EN, 0);

    si14tp_write_reg(SI14_REG_SENSE1, 0xAA);
    si14tp_read_reg(SI14_REG_SENSE1, &temp);

    bool ok = (temp == 0xAA);
    if (ok)
    {
        /* 恢复灵敏度: 用共享宏, 与 init 保持同步 */
        si14tp_write_reg(SI14_REG_SENSE1, SI14_SENS_DEFAULT);
    }

    gpio_set_level(SI14TP_IIC_EN, 1);
    return ok;
}

/* 初始化 si14tp 寄存器 */
void si14tp_init(void)
{
    gpio_set_level(SI14TP_IIC_EN, 0);

    /* 设置全部 14 通道灵敏度 */
    for (uint8_t reg = SI14_REG_SENSE1; reg <= SI14_REG_SENSE6; reg++)
    {
        si14tp_write_reg(reg, SI14_SENS_DEFAULT);
    }
    si14tp_write_reg(SI14_REG_SENSE7, SI14_SENS_DEFAULT);

    /* 通用配置(CTRL1): 自动快/慢模式 + 高中断 */
    si14tp_write_reg(SI14_REG_CFIG, 0x1B);

    si14tp_write_reg(SI14_REG_CHHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD2, 0x00);
    si14tp_write_reg(SI14_REG_REFRST1, 0x00);
    si14tp_write_reg(SI14_REG_REFRST2, 0x00);
    si14tp_write_reg(SI14_REG_CALHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CALHOLD2, 0x00);

    vTaskDelay(pdMS_TO_TICKS(40));

    /* 关闭 I2C 进入低功耗待机 */
    gpio_set_level(SI14TP_IIC_EN, 1);
}

/* 进入低功耗睡眠模式 */
void si14tp_enter_sleep(void)
{
    gpio_set_level(SI14TP_IIC_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(200)); /* 进入前让设备稳定(STM32 用 200ms) */

    si14tp_write_reg(SI14_REG_CTRL, 0x07);
    si14tp_write_reg(SI14_REG_REFRST1, 0x00);
    si14tp_write_reg(SI14_REG_REFRST2, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD1, 0x00);
    si14tp_write_reg(SI14_REG_CHHOLD2, 0x30);
    si14tp_write_reg(SI14_REG_CTRL, 0x07);

    /* 解锁并跳过校准 */
    si14tp_write_reg(SI14_REG_UNLOCK, 0xA5);
    si14tp_write_reg(SI14_REG_CTRL2, 0x01);
    esp_rom_delay_us(800); /* 收尾稳定(STM32 delay_us(800)) */

    gpio_set_level(SI14TP_IIC_EN, 1);
}

// -------------------------- 数据采集 --------------------------

/* 读取当前触摸按键(无按下返回 0) */
int si14tp_get_key(void)
{
    uint8_t buf[4] = {0};

    gpio_set_level(SI14TP_IIC_EN, 0);
    esp_rom_delay_us(2000); /* 使能 I2C 通路后短稳定, 避免睡眠唤醒路径读到旧数据 */

    si14tp_read_reg(SI14_REG_OUT1, &buf[0]);
    si14tp_read_reg(SI14_REG_OUT2, &buf[1]);
    si14tp_read_reg(SI14_REG_OUT3, &buf[2]);
    si14tp_read_reg(SI14_REG_OUT4, &buf[3]);

    gpio_set_level(SI14TP_IIC_EN, 1);

    /* 每个输出字节打包 4 通道 x 2bit; 0x03 表示被按下 */
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            int key = i * 4 + j + 1;
            if (key > 14)
            {
                /* 通道 15/16 超出范围: 1..14 已扫完无有效按键 */
                return 0;
            }

            uint8_t ch_bits = (buf[i] >> (j * 2)) & 0x03;
            if (ch_bits == 0x03)
            {
                return key;
            }
        }
    }

    return 0;
}

/* 触摸中断处理任务 */
void si14tp_task(void *arg)
{
    char key = 0;
    while (1)
    {
        if (xSemaphoreTake(si14tp_semaphore, portMAX_DELAY))
        {
            notify_user_activity();

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
                    /* 清空输入 */
                    g_input_len = 0;
                    memset(g_input_password, 0, sizeof(g_input_password));
                }
                else if (key == '#')
                {
                    /* 确认密码 */
                    if (g_input_len == TOUCH_PASSWORD_LEN)
                    {
                        if (strcmp(g_input_password, g_touch_password) == 0)
                        {
                            ESP_LOGI(TAG, "password verification OK");
                            uint8_t message = 0x01; /* 成功 */
                            xQueueSend(password_queue, &message, pdMS_TO_TICKS(1000));
                        }
                        else
                        {
                            ESP_LOGW(TAG, "password verification failed");
                            uint8_t message = 0x00; /* 失败 */
                            xQueueSend(password_queue, &message, pdMS_TO_TICKS(1000));
                        }
                    }
                    else
                    {
                        ESP_LOGW(TAG, "invalid password length: %d", g_input_len);
                    }

                    /* 复位输入状态 */
                    g_input_len = 0;
                    memset(g_input_password, 0, sizeof(g_input_password));
                }
            }
        }
    }
}

/* 总初始化入口, 创建任务 */
esp_err_t si14tp_initialization(void)
{
    si14tp_i2c_init();
    si14tp_gpio_init();
    si14tp_hard_reset();
    si14tp_init();

    xTaskCreate(si14tp_task, "si14tp_task", 8192, NULL, 10, NULL);

    return ESP_OK;
}
