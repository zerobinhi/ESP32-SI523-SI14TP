#include "si523.h"

static const char *TAG = "si523";

/* Semaphore used to notify card detection interrupt */
SemaphoreHandle_t si523_semaphore = NULL;

/* I2C handles */
i2c_master_bus_handle_t i2c_bus_handle; // I2C master bus handle
i2c_master_dev_handle_t si523_handle;   // si523 I2C device handle

/* Service installation flags */
bool g_gpio_isr_service_installed = false; // GPIO ISR service installation status
bool g_i2c_service_installed = false;      // I2C service installation status

/* Card storage */
uint64_t g_card_id_value[MAX_CARDS] = {0}; // Stored card IDs (uint64 format)
uint8_t g_card_count = 0;                  // Number of stored cards

uint8_t g_acd_cfg_k_val;
uint8_t g_acd_cfg_c_val;
uint8_t g_gsn_value = 0; // GSN电导值，自动校准中需使用，对应新工程 GSN_Value

uint8_t g_uid[4];
uint8_t g_uid_len = 4; // ???

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    gpio_set_intr_type(SI523_INT_PIN, GPIO_INTR_NEGEDGE);
    ESP_DRAM_LOGI(TAG, "Card detected");
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == SI523_INT_PIN)
    {
        xSemaphoreGiveFromISR(si523_semaphore, NULL);
    }
}

esp_err_t si523_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    esp_err_t err = i2c_master_transmit(si523_handle, buf, 2, portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Write reg 0x%02X failed", reg);
    }

    return err;
}

uint8_t si523_read_reg(uint8_t reg)
{
    uint8_t data = 0;
    esp_err_t err = i2c_master_transmit_receive(si523_handle, &reg, 1, &data, 1, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Read reg 0x%02X failed", reg);
    }

    return data;
}

void si523_set_bit_mask(unsigned char reg, unsigned char mask)
{
    uint8_t tmp = si523_read_reg(reg);
    si523_write_reg(reg, tmp | mask);
}

void si523_clear_bit_mask(unsigned char reg, unsigned char mask)
{
    uint8_t tmp = si523_read_reg(reg);
    si523_write_reg(reg, tmp & (~mask));
}

/* initialize i2c and device */
void si523_i2c_init(void)
{
    /* create binary semaphore for interrupt sync */
    if (si523_semaphore == NULL)
    {
        si523_semaphore = xSemaphoreCreateBinary();
        if (si523_semaphore == NULL)
        {
            ESP_LOGE(TAG, "semaphore creation failed");
        }
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

        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle));
        g_i2c_service_installed = true;

        ESP_LOGI(TAG, "i2c bus initialized");
    }

    /* add si523 device to i2c bus */
    if (si523_handle == NULL)
    {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SI523_I2C_ADDR,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };

        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &si523_handle));

        ESP_LOGI(TAG, "si523 device added");
    }
}

/* initialize gpio pins */
void si523_gpio_init(void)
{
    /* configure reset pin as output */
    gpio_config_t si523_rst_cfg = {
        .pin_bit_mask = (1ULL << SI523_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&si523_rst_cfg);

    /* configure interrupt pin */
    gpio_config_t si523_irq_cfg = {
        .pin_bit_mask = (1ULL << SI523_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&si523_irq_cfg);

    /* install gpio isr service if not installed */
    if (!g_gpio_isr_service_installed)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;

        ESP_LOGI(TAG, "gpio isr service installed");
    }

    /* attach interrupt handler */
    gpio_isr_handler_add(SI523_INT_PIN, gpio_isr_handler, (void *)SI523_INT_PIN);

    ESP_LOGI(TAG, "si523 int pin isr handler added");

    /* Load card data from NVS */
    if (nvs_custom_get_u8(NULL, "card", "count", &g_card_count) == ESP_OK)
    {
        size_t size = sizeof(g_card_id_value);
        nvs_custom_get_blob(NULL, "card", "card_ids", g_card_id_value, &size);
        ESP_LOGI(TAG, "loaded %d cards from NVS", g_card_count);
    }
    else
    {
        ESP_LOGW(TAG, "no card data found in NVS");
        g_card_count = 0;
    }

    ESP_LOGI(TAG, "g_card_count:%d", g_card_count);
}

/* hardware reset sequence */
void si523_hard_reset(void)
{
    gpio_set_level(SI523_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    gpio_set_level(SI523_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

/* check device communication */
bool si523_check(void)
{
    uint8_t chip_version = si523_read_reg(SI523_REG_VERSION);
    ESP_LOGI(TAG, "Si523 Chip Version: 0x%02x", chip_version);
    return (chip_version != 0x00 && chip_version != 0xFF);
}

/* initialize si523 registers */
void si523_init(void)
{
    si523_type_a_init(); // 读A卡初始化配置

    si523_acd_auto_calc(); // 自动获取阈值

    si523_acd_init(); // ACD初始化配置
}

void si523_soft_reset(void)
{
    /* 移植自新工程 PcdReset：硬复位 + 软复位 + GSN/CWGsP/Control/RxThreshold/RFCfg 配置
     * 硬复位可清除静电干扰导致的芯片异常状态，软复位无法完全替代
     * 包含 GSN_Value 下溢保护，防止 (g_gsn_value-1) 下溢成 0xFF 导致天线电导异常
     */
    /* 1. 硬复位：RST 拉低 2ms → 拉高 2ms */
    gpio_set_level(SI523_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(SI523_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 2. 软复位 */
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(1)); // 复位需要 1ms

    /* 3. 配置寄存器 */
    if (g_gsn_value == 0)
    {
        g_gsn_value = 1; // 防止下溢成 0xFF
    }
    si523_write_reg(SI523_REG_GS_N_ON, (g_gsn_value << 4) | (g_gsn_value - 1)); // 天线驱动打开时电导系数
    si523_write_reg(SI523_REG_CW_GS_P, 10);                                     // 不调制时 P 驱动电导值
    si523_write_reg(SI523_REG_CONTROL, 0x10);                                   // 指示最后一个接收到的字节都有效
    si523_write_reg(SI523_REG_RX_THRESHOLD, (6 << 4) | 4);                      // 位译码器阈值
    si523_write_reg(SI523_REG_RF_CFG, 0x78);                                    // 接收增益
}

bool si523_check_chip(void)
{
    uint8_t chip_version = si523_read_reg(SI523_REG_VERSION);
    ESP_LOGI(TAG, "Si523 Chip Version: 0x%02x", chip_version);
    return (chip_version != 0x00 && chip_version != 0xFF);
}

void si523_antenna_on(void)
{
    /* 移植自新工程 PcdAntennaOn：
     * - 无条件置位 Tx1RFEn/Tx2RFEn（修复仅一个 bit 置位的异常中间态）
     * - 开启后延时 1ms，保证天线场强稳定（注释要求开关间隔至少 1ms）
     */
    si523_write_reg(SI523_REG_TX_CONTROL, si523_read_reg(SI523_REG_TX_CONTROL) | 0x03);
    vTaskDelay(pdMS_TO_TICKS(1));
}

void si523_antenna_off(void)
{
    si523_clear_bit_mask(SI523_REG_TX_CONTROL, 0x03);
}

void si523_set_rx_gain(uint8_t gain)
{
    uint8_t reg_val = si523_read_reg(SI523_REG_RF_CFG);

    reg_val &= 0x8F;
    reg_val |= (gain & 0x70);
    si523_write_reg(SI523_REG_RF_CFG, reg_val);
}

void si523_calculate_crc(uint8_t *in_buf, uint8_t data_len, uint8_t *out_buf)
{
    uint8_t loop_cnt;
    uint8_t irq_reg;
    uint32_t timeout = 0xFF;

    si523_clear_bit_mask(SI523_REG_DIV_IRQ, 0x04);
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_IDLE);
    si523_write_reg(SI523_REG_FIFO_LEVEL, 0x80);

    for (loop_cnt = 0; loop_cnt < data_len; loop_cnt++)
    {
        si523_write_reg(SI523_REG_FIFO_DATA, in_buf[loop_cnt]);
    }

    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_CALC_CRC);

    /* 无延时忙等待，对应新工程 CalulateCRC 的 do-while 循环
     * CRC 计算很快，vTaskDelay(1ms) 会导致最坏 255ms 延迟
     */
    do
    {
        irq_reg = si523_read_reg(SI523_REG_DIV_IRQ);
        timeout--;
    } while ((timeout != 0) && !(irq_reg & 0x04));

    out_buf[0] = si523_read_reg(SI523_REG_CRC_RESULT_L);
    out_buf[1] = si523_read_reg(SI523_REG_CRC_RESULT_H);
}

static uint8_t si523_raw_cmd(uint8_t cmd, uint8_t *in_buf, uint8_t in_len, uint8_t *out_buf, uint32_t *out_bits)
{
    uint8_t ret_status = SI523_ERR;
    uint8_t irq_en = 0x00;
    uint8_t wait_for = 0x00;
    uint8_t last_bits;
    uint8_t reg_val;
    uint32_t loop_cnt;
    uint32_t timeout = 2000;

    switch (cmd)
    {
    case SI523_CMD_AUTHENT:
        irq_en = 0x12;
        wait_for = 0x10;
        break;
    case SI523_CMD_TRANSCEIVE:
        irq_en = 0x77;
        wait_for = 0x30;
        break;
    default:
        break;
    }

    si523_clear_bit_mask(SI523_REG_COM_IRQ, 0x80);
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_IDLE);
    vTaskDelay(pdMS_TO_TICKS(1));                /* IDLE 后需等待，对应新工程 delay_ms(1) */
    si523_write_reg(SI523_REG_FIFO_LEVEL, 0x80); /* Flush FIFO */

    for (loop_cnt = 0; loop_cnt < in_len; loop_cnt++)
    {
        si523_write_reg(SI523_REG_FIFO_DATA, in_buf[loop_cnt]);
    }

    si523_write_reg(SI523_REG_COMMAND, cmd);

    if (cmd == SI523_CMD_TRANSCEIVE)
    {
        si523_set_bit_mask(SI523_REG_BIT_FRAMING, 0x80); /* StartSend */
    }

    /* 无延时忙等待，对应新工程 PcdComMF522 的 do-while 循环
     * I2C 读寄存器本身已有足够延时，无需额外 vTaskDelay
     */
    do
    {
        reg_val = si523_read_reg(SI523_REG_COM_IRQ);
        timeout--;
    } while ((timeout != 0) && !(reg_val & 0x01) && !(reg_val & wait_for));

    si523_clear_bit_mask(SI523_REG_BIT_FRAMING, 0x80);

    if (timeout != 0)
    {
        if (!(si523_read_reg(SI523_REG_ERROR) & 0x1B))
        {
            ret_status = SI523_OK;

            if (reg_val & irq_en & 0x01) // ???
            {
                ret_status = SI523_ERR_NO_TAG;
            }

            if (cmd == SI523_CMD_TRANSCEIVE)
            {
                reg_val = si523_read_reg(SI523_REG_FIFO_LEVEL);
                last_bits = si523_read_reg(SI523_REG_CONTROL) & 0x07;

                if (last_bits)
                {
                    *out_bits = (reg_val - 1) * 8 + last_bits;
                }
                else
                {
                    *out_bits = reg_val * 8;
                }

                if (reg_val == 0)
                {
                    reg_val = 1;
                }
                if (reg_val > SI523_MAX_RLEN)
                {
                    reg_val = SI523_MAX_RLEN;
                }

                for (loop_cnt = 0; loop_cnt < reg_val; loop_cnt++)
                {
                    out_buf[loop_cnt] = si523_read_reg(SI523_REG_FIFO_DATA);
                }
            }
        }
        else
        {
            ret_status = SI523_ERR;
        }
    }

    si523_set_bit_mask(SI523_REG_CONTROL, 0x80); /* StopTimerNow */
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_IDLE);

    return ret_status;
}

uint8_t si523_request(uint8_t req_code, uint8_t *tag_type)
{
    uint8_t ret_status;
    uint32_t recv_bits;
    uint8_t fifo_buf[SI523_MAX_RLEN];

    si523_clear_bit_mask(SI523_REG_STATUS2, 0x08);
    si523_write_reg(SI523_REG_BIT_FRAMING, 0x07);
    si523_set_bit_mask(SI523_REG_TX_CONTROL, 0x03);

    fifo_buf[0] = req_code;
    ret_status = si523_raw_cmd(SI523_CMD_TRANSCEIVE, fifo_buf, 1, fifo_buf, &recv_bits);

    if ((ret_status == SI523_OK) && (recv_bits == 0x10))
    {
        tag_type[0] = fifo_buf[0];
        tag_type[1] = fifo_buf[1];
    }
    else
    {
        ret_status = SI523_ERR;
    }

    return ret_status;
}

uint8_t si523_anticollision(uint8_t *uid, uint8_t anticoll_level)
{
    uint8_t ret_status;
    uint8_t loop_cnt;
    uint8_t uid_check = 0;
    uint32_t recv_bits;
    uint8_t fifo_buf[SI523_MAX_RLEN];

    si523_clear_bit_mask(SI523_REG_STATUS2, 0x08);
    si523_write_reg(SI523_REG_BIT_FRAMING, 0x00);
    si523_clear_bit_mask(SI523_REG_COLL, 0x80);

    fifo_buf[0] = anticoll_level;
    fifo_buf[1] = 0x20;

    ret_status = si523_raw_cmd(SI523_CMD_TRANSCEIVE, fifo_buf, 2, fifo_buf, &recv_bits);

    if (ret_status == SI523_OK)
    {
        for (loop_cnt = 0; loop_cnt < 4; loop_cnt++)
        {
            uid[loop_cnt] = fifo_buf[loop_cnt];
            uid_check ^= fifo_buf[loop_cnt];
        }
        if (uid_check != fifo_buf[loop_cnt])
        {
            ret_status = SI523_ERR;
        }
    }

    si523_set_bit_mask(SI523_REG_COLL, 0x80);
    return ret_status;
}

uint8_t si523_select_card(uint8_t *uid, uint8_t anticoll_level, uint8_t *sak)
{
    uint8_t ret_status;
    uint8_t loop_cnt;
    uint32_t recv_bits;
    uint8_t fifo_buf[SI523_MAX_RLEN];

    fifo_buf[0] = anticoll_level;
    fifo_buf[1] = 0x70;
    fifo_buf[6] = 0;

    for (loop_cnt = 0; loop_cnt < 4; loop_cnt++)
    {
        fifo_buf[loop_cnt + 2] = uid[loop_cnt];
        fifo_buf[6] ^= uid[loop_cnt];
    }

    si523_calculate_crc(fifo_buf, 7, &fifo_buf[7]);
    si523_clear_bit_mask(SI523_REG_STATUS2, 0x08);

    ret_status = si523_raw_cmd(SI523_CMD_TRANSCEIVE, fifo_buf, 9, fifo_buf, &recv_bits);

    if ((ret_status == SI523_OK) && (recv_bits == 0x18))
    {
        *sak = fifo_buf[0];
        ret_status = SI523_OK;
    }
    else
    {
        ret_status = SI523_ERR;
    }

    return ret_status;
}

void si523_type_a_init(void)
{
    /* 移植自新工程 PCD_SI522A_TypeA_Init = PcdReset + PcdAntennaOff + M500PcdConfigISOTypeA：
     * - 配置寄存器前先关闭天线，避免配置过程中射频干扰
     * - 显式设置 ComIEnReg 为低电平触发中断（BIT7）
     * - RFCfgReg 使用 0x58（新工程值）
     * - 调用 SiModifyReg(0x01, 0, 0x20) 打开接收机模拟部分
     * - 保留旧工程定时器配置（PcdComMF522 超时机制依赖）
     * 注：PcdReset 由调用方在外部调用（si523_init / si523_handle_card_detected 等）
     */
    si523_antenna_off(); // 先关天线，避免配置过程中射频干扰（新工程新增）

    si523_clear_bit_mask(SI523_REG_STATUS2, 0x08); // 清 MFCrypto1On
    si523_set_bit_mask(SI523_REG_COM_IEN, 0x80);   // 低电平触发中断（新工程新增）

    si523_write_reg(SI523_REG_MODE, 0x3D);    /* CRC preset 0x6363 */
    si523_write_reg(SI523_REG_RX_SEL, 0x86);  /* RxWait 延迟（新工程值） */
    si523_write_reg(SI523_REG_RF_CFG, 0x58);  /* 接收增益（新工程值，旧值 0x68） */
    si523_write_reg(SI523_REG_TX_AUTO, 0x40); /* Force 100% ASK, typeA */
    si523_write_reg(SI523_REG_TX_MODE, 0x00); /* Tx Framing A */
    si523_write_reg(SI523_REG_RX_MODE, 0x00); /* Rx Framing A */
    si523_write_reg(SI523_REG_CONTROL, 0x10);

    /* Timer configuration（保留旧工程，PcdComMF522 超时机制依赖） */
    si523_write_reg(SI523_REG_T_MODE, 0x80);
    si523_write_reg(SI523_REG_T_PRESCALER, 0xA9);
    si523_write_reg(SI523_REG_T_RELOAD_H, 0x03);
    si523_write_reg(SI523_REG_T_RELOAD_L, 0xE8);

    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_IDLE); /* Idle */

    /* 打开接收机模拟部分：SiModifyReg(0x01, 0, 0x20)
     * 即 CommandReg 的 bit5 清 0（Turn on the analog part of receiver）
     */
    si523_clear_bit_mask(SI523_REG_COMMAND, 0x20);

    si523_antenna_on();
    esp_rom_delay_us(400);
}

uint8_t si523_type_a_get_uid(uint8_t *uid, uint8_t *uid_len)
{
    uint8_t atqa[2];
    uint8_t uid_buf[12] = {0};
    uint8_t sak = 0;
    uint8_t offset = 0;

    ESP_LOGI(TAG, "Type A: Get UID");

    /* 移植自新工程 PCD_SI522A_TypeA_GetUID：
     * 第一次 Request 使用 0x58（38dB），失败后依次降为 0x48（33dB）、0x58（38dB）
     * 不使用 0x68（43dB），避免高增益下噪声误判
     */
    si523_set_rx_gain(SI523_RX_GAIN_38DB); // 0x58，沿用 M500PcdConfigISOTypeA 设置值

    /* Request card with auto gain fallback */
    if (si523_request(SI523_PICC_REQ_IDL, atqa) != SI523_OK)
    {
        si523_set_rx_gain(SI523_RX_GAIN_33DB); // 0x48
        if (si523_request(SI523_PICC_REQ_IDL, atqa) != SI523_OK)
        {
            si523_set_rx_gain(SI523_RX_GAIN_38DB); // 0x58
            if (si523_request(SI523_PICC_REQ_IDL, atqa) != SI523_OK)
            {
                ESP_LOGW(TAG, "Request failed (no card)");
                return SI523_ERR_NO_TAG;
            }
        }
    }

    ESP_LOGI(TAG, "ATQA: %02X %02X", atqa[0], atqa[1]);

    // Level 1 Anticollision and Select
    if (si523_anticollision(uid_buf, SI523_PICC_ANTICOLL1) != SI523_OK)
    {
        ESP_LOGE(TAG, "Anticoll L1 failed");
        return SI523_ERR;
    }

    if (si523_select_card(uid_buf, SI523_PICC_ANTICOLL1, &sak) != SI523_OK)
    {
        ESP_LOGE(TAG, "Select L1 failed");
        return SI523_ERR;
    }

    ESP_LOGI(TAG, "SAK L1: 0x%02X", sak);

    /* 判断是否有 CT (0x88) */
    if (uid_buf[0] == 0x88)
    {
        // 跳过 CT
        memcpy(uid, &uid_buf[1], 3);
        offset = 3;
    }
    else
    {
        memcpy(uid, &uid_buf[0], 4);
        offset = 4;
    }

    // Level 2 Anticollision and Select
    if (sak & 0x04)
    {
        if (si523_anticollision(uid_buf + 4, SI523_PICC_ANTICOLL2) != SI523_OK)
        {
            ESP_LOGE(TAG, "Anticoll L2 failed");
            return SI523_ERR;
        }

        if (si523_select_card(uid_buf + 4, SI523_PICC_ANTICOLL2, &sak) != SI523_OK)
        {
            ESP_LOGE(TAG, "Select L2 failed");
            return SI523_ERR;
        }

        ESP_LOGI(TAG, "SAK L2: 0x%02X", sak);

        if (uid_buf[4] == 0x88)
        {
            memcpy(uid + offset, &uid_buf[5], 3);
            offset += 3;
        }
        else
        {
            memcpy(uid + offset, &uid_buf[4], 4);
            offset += 4;
        }

        // Level 3 Anticollision and Select
        if (sak & 0x04)
        {
            if (si523_anticollision(uid_buf + 8, SI523_PICC_ANTICOLL3) != SI523_OK)
            {
                ESP_LOGE(TAG, "Anticoll L3 failed");
                return SI523_ERR;
            }

            if (si523_select_card(uid_buf + 8, SI523_PICC_ANTICOLL3, &sak) != SI523_OK)
            {
                ESP_LOGE(TAG, "Select L3 failed");
                return SI523_ERR;
            }

            ESP_LOGI(TAG, "SAK L3: 0x%02X", sak);

            memcpy(uid + offset, &uid_buf[8], 4);
            offset += 4;
        }
    }

    *uid_len = offset;

    ESP_LOGI(TAG, "UID (Len:%d):", *uid_len);
    ESP_LOG_BUFFER_HEX(TAG, uid, *uid_len);

    return SI523_OK;
}

void si523_acd_auto_calc(void)
{
    /* 移植自新工程 Si522A_ACD_V1.7 的 PCD_ACD_AutoCalc
     * 三档增益 TR_7/TR_3/TR_1 联动 GSN 扫描 + 5 次复测 + 6 次采样去极值平均 + 失败兜底
     */
    uint8_t status = 0;          // 0=ERROR, 1=SUCCESS
    uint8_t temp_compare = 0;    // 场强采样值暂存
    uint8_t gsn_exp = 0;         // GsNOnReg 寄存器值
    uint8_t c_val_temp[6] = {0}; // 采样 C 值缓存

    /* 三档增益档位，按优先级从高到低尝试
     * TI=01, TR=10 (3倍) / TR=01 (3倍) / TR=00 (1倍)
     */
    const uint8_t k_val_tr_7[8] = {0x4f, 0x4e, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48};
    const uint8_t k_val_tr_3[8] = {0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28};
    const uint8_t k_val_tr_1[8] = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08};

    /* 三档增益档位指针数组，统一遍历 */
    const uint8_t *k_val_tables[3] = {k_val_tr_7, k_val_tr_3, k_val_tr_1};
    const char *tr_names[3] = {"TR_7", "TR_3", "TR_1"};

    si523_soft_reset();                                       // PcdReset，含 GSN 配置
    si523_write_reg(SI523_REG_TX_CONTROL, 0x83);              // 打开天线 TX1,TX2
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_ADC_EXCUTE); // 开启 ADC_EXCUTE
    esp_rom_delay_us(130);

    /* 三档增益逐档尝试 */
    for (uint8_t tr_idx = 0; tr_idx < 3 && status == 0; tr_idx++)
    {
        const uint8_t *k_val_table = k_val_tables[tr_idx];

        gsn_exp = 0;
        status = 0;
        while ((gsn_exp < 15) && (status == 0))
        {
            gsn_exp += 1;
            si523_write_reg(SI523_REG_GS_N_ON, (gsn_exp << 4)); // 不调制时 N 驱动电导值

            for (uint8_t i = 0; i < 8; i++)
            {
                si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_LPD_CFG1 << 2) | 0x40);
                si523_write_reg(SI523_REG_ACD_CFG, k_val_table[i]);

                si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ADC_VAL << 2) | 0x40);
                temp_compare = si523_read_reg(SI523_REG_ACD_CFG);

                ESP_LOGI(TAG, "%s i=%d, temp_G=%02x", tr_names[tr_idx], i, temp_compare);

                /* 场强采样值不在合适范围内 */
                if ((temp_compare >= 0x7f) || (temp_compare < 0x50))
                {
                    status = 0;
                    continue;
                }

                /* 5 次复测确认稳定性 */
                status = 1;
                for (uint8_t j = 0; j < 5; j++)
                {
                    si523_write_reg(SI523_REG_GS_N_ON, (gsn_exp << 4));
                    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_LPD_CFG1 << 2) | 0x40);
                    si523_write_reg(SI523_REG_ACD_CFG, k_val_table[i]);
                    esp_rom_delay_us(130);

                    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ADC_VAL << 2) | 0x40);
                    temp_compare = si523_read_reg(SI523_REG_ACD_CFG);
                    ESP_LOGI(TAG, "%s j=%d, temp_G=%02x", tr_names[tr_idx], j, temp_compare);

                    if ((temp_compare >= 0x7f) || (temp_compare < 0x50))
                    {
                        status = 0;
                        break;
                    }
                }

                if (status == 0)
                {
                    continue;
                }

                /* 场强采样值在合适范围内 */
                g_acd_cfg_k_val = k_val_table[i]; // 获取有效 K 值
                g_acd_cfg_c_val = temp_compare;   // 设定有效无卡场强参考值
                g_gsn_value = gsn_exp;            // 获取有效电导值

                ESP_LOGI(TAG, "ACD AutoCalc success: K=0x%02x, C=0x%02x, GSN=%d (%s)",
                         g_acd_cfg_k_val, g_acd_cfg_c_val, g_gsn_value, tr_names[tr_idx]);
                break;
            }
        }
    }

    /* 三档增益均失败，使用安全默认值兜底 */
    if (status == 0)
    {
        ESP_LOGW(TAG, "ACD AutoCalc failed! Using default values.");
        g_gsn_value = 1;
        g_acd_cfg_k_val = 0x4f;
        g_acd_cfg_c_val = 0x60;
    }

    /* 写入获取的参数，6 次采样去极值平均，验证 C 值，防止误触发 */
    si523_soft_reset();
    si523_write_reg(SI523_REG_TX_CONTROL, 0x83); // 打开天线

    si523_write_reg(SI523_REG_GS_N_ON, (g_gsn_value << 4)); // 注意：此处用 g_gsn_value，与原版 GSN_EXP 等价
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_LPD_CFG1 << 2) | 0x40);
    si523_write_reg(SI523_REG_ACD_CFG, g_acd_cfg_k_val);

    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_ADC_EXCUTE);
    esp_rom_delay_us(130);

    /* 6 次采样 */
    for (uint8_t j = 0; j < 6; j++)
    {
        si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ADC_VAL << 2) | 0x40);
        c_val_temp[j] = si523_read_reg(SI523_REG_ACD_CFG);
        esp_rom_delay_us(10);
    }
    si523_write_reg(SI523_REG_COMMAND, SI523_CMD_ADC_EXCUTE); // 关闭 ADC_EXCUTE

    ESP_LOGI(TAG, "C_Val_temp samples:");
    ESP_LOG_BUFFER_HEX(TAG, c_val_temp, 6);

    /* 冒泡排序，从小到大 */
    for (uint8_t i = 0; i < 6 - 1; i++)
    {
        for (uint8_t j = 0; j < 6 - 1 - i; j++)
        {
            if (c_val_temp[j] > c_val_temp[j + 1])
            {
                temp_compare = c_val_temp[j];
                c_val_temp[j] = c_val_temp[j + 1];
                c_val_temp[j + 1] = temp_compare;
            }
        }
    }

    /* 去除最大值最小值，取中间 4 次平均 */
    temp_compare = 0;
    for (uint8_t j = 1; j < 5; j++)
    {
        temp_compare += c_val_temp[j];
    }
    temp_compare = temp_compare / 4;
    g_acd_cfg_c_val = temp_compare;

    ESP_LOGI(TAG, "ACD AutoCalc final: K=0x%02x, C=0x%02x, GSN=%d",
             g_acd_cfg_k_val, g_acd_cfg_c_val, g_gsn_value);
}

void si523_acd_init(void)
{
    si523_write_reg(SI523_REG_DIV_IRQ, 0x60); ////清中断，该处不清中断，进入ACD模式后会异常产生有卡中断。
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ACC_CFG << 2) | 0x40);
    si523_write_reg(SI523_REG_ACD_CFG, 0x55); // Clear ACC_IRQ

    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_RCCFG1 << 2) | 0x40); // 设置轮询时间
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_RCCFG1_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ACRDCFG << 2) | 0x40); // 设置相对模式或者绝对模式
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_ACRDCFG_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_MAN_REF << 2) | 0x40); // 设置无卡场强值
    si523_write_reg(SI523_REG_ACD_CFG, g_acd_cfg_c_val);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_VAL_DELTA << 2) | 0x40); // 设置灵敏度，一般建议为4，在调试时，可以适当降低验证该值，验证ACD功能
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_VAL_DELTA_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_WDT_CNT << 2) | 0x40); // 设置看门狗定时器时间
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_WDT_CNT_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ARI_CFG << 2) | 0x40); // 设置ARI功能，在天线场强打开前1us产生ARI电平控制触摸芯片si523的硬件屏蔽引脚SCT
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_ARI_CFG_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_LPD_CFG1 << 2) | 0x40); // 设置ADC的基准电压和放大增益
    si523_write_reg(SI523_REG_ACD_CFG, g_acd_cfg_k_val);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_RF_LOW_DET << 2) | 0x40); // 设置监测ACD功能是否产生场强，意外产生可能导致读卡芯片复位或者寄存器丢失
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_RF_LOW_DET_DEFAULT);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_IRQ_EN << 2) | 0x40); // 设置ACD模式下相关功能的标志位传导到IRQ引脚
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_IRQ_EN_DEFAULT);

    si523_write_reg(SI523_REG_COM_IEN, SI523_COM_IEN_DEFAULT); // ComIEnReg，DivIEnReg   设置IRQ选择上升沿或者下降沿
    si523_write_reg(SI523_REG_DIV_IEN, SI523_DIV_IEN_DEFAULT);

    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ACC_CFG << 2) | 0x40); // 设置监测ACD功能下的重要寄存器的配置值，寄存器丢失后会立即产生中断
    si523_write_reg(SI523_REG_ACD_CFG, SI523_ACD_ACC_CFG_DEFAULT);         // 写非0x55的值即开启功能，写0x55清除使能停止功能。

    si523_write_reg(SI523_REG_COMMAND, 0xB0); // 进入ACD
}

uint8_t si523_acd_irq_process(void)
{
    uint8_t div_irq_reg = si523_read_reg(SI523_REG_DIV_IRQ);

    if (div_irq_reg & 0x40)
    {
        si523_write_reg(SI523_REG_DIV_IRQ, 0x40); /* Clear ACDIRq */
        return 1;
    }

    if (div_irq_reg & 0x20)
    {
        si523_write_reg(SI523_REG_DIV_IRQ, 0x20); /* Clear ACDTIMER_IRQ */
        return 2;
    }

    si523_write_reg(SI523_REG_DIV_IRQ, 0x60); /* Clear ACDIRq + WdtIRq */

    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_IRQ_FLAG << 2) | 0x40);
    si523_write_reg(SI523_REG_ACD_CFG, 0x0A);
    si523_write_reg(SI523_REG_PAGE2, (SI523_ACD_REG_ACC_CFG << 2) | 0x40);
    si523_write_reg(SI523_REG_ACD_CFG, 0x55);

    return 0;
}

uint8_t find_card_id(uint64_t card_id)
{
    // Check if any cards exist
    if (g_card_count == 0)
    {
        return 0;
    }
    // Traverse card list to find match
    for (uint8_t i = 0; i < g_card_count; i++)
    {
        if (g_card_id_value[i] == card_id)
        {
            return i + 1; // Return 1-based index if found
        }
    }
    return 0; // Not found
}

/* 移植自新工程 ACD_Fun 的 case 0/1 统一处理流程：
 * 开天线 → 复位 → TypeA 初始化 → 3 次重试读卡 → 业务逻辑 → 重新进入 ACD
 * 防止静电干扰导致天线异常关闭、芯片寄存器丢失
 */
static void si523_handle_card_detected(void)
{
    uint64_t card_id_value = 0;
    uint8_t read_ok = 0;
    uint8_t i;

    /* 1. 开天线，防止静电干扰产生中断后天线场强被异常关掉 */
    si523_antenna_on();
    /* 2. 复位（含 GSN 配置），防止芯片被静电异常操作导致无法工作 */
    si523_soft_reset();
    /* 3. 重新初始化 TypeA 默认配置 */
    si523_type_a_init();

    /* 4. 3 次重试读卡 */
    for (i = 0; i < 3; i++)
    {
        if (si523_type_a_get_uid(g_uid, &g_uid_len) == SI523_OK)
        {
            read_ok = 1;
            break;
        }
    }

    /* 5. 读卡失败：复位 + 重新初始化后重新进入 ACD */
    if (!read_ok)
    {
        ESP_LOGW(TAG, "Read UID failed after 3 retries, reconfigure ACD");
        si523_soft_reset();
        si523_type_a_init();
        si523_acd_init();
        return;
    }

    /* 6. 读卡成功，执行业务逻辑 */
    card_id_value = 0;
    for (i = 0; i < g_uid_len; i++)
    {
        card_id_value = (card_id_value << 8) | g_uid[i];
    }
    ESP_LOGI(TAG, "Card ID (uint64): 0x%llX", card_id_value);

    if (g_ready_add_card == true) // 添加卡操作
    {
        if (find_card_id(card_id_value) == 0) // 卡不存在，可以添加
        {
            g_card_id_value[g_card_count] = card_id_value;
            nvs_custom_set_blob(NULL, "card", "card_ids", g_card_id_value, sizeof(g_card_id_value));
            g_card_count++;
            send_operation_result("card_added", true);
            nvs_custom_set_u8(NULL, "card", "count", g_card_count);
            ESP_LOGI(TAG, "add card ID (uint64): 0x%llX", card_id_value);
            send_card_list();
        }
        else // 卡已存在
        {
            send_operation_result("card_added", false);
            ESP_LOGI(TAG, "card already exists: 0x%llX", card_id_value);
        }
        g_ready_add_card = false; // 复位添加卡标志
    }
    else // 卡识别操作
    {
        if (find_card_id(card_id_value) == 0) // 未知卡
        {
            ESP_LOGW(TAG, "unknown card ID (uint64): 0x%llX", card_id_value);
            uint8_t message = 0x00;
            xQueueSend(card_queue, &message, pdMS_TO_TICKS(1000));
        }
        else // 已识别卡
        {
            ESP_LOGI(TAG, "recognized card: 0x%llX", card_id_value);
            uint8_t message = 0x01;
            xQueueSend(card_queue, &message, pdMS_TO_TICKS(1000));
        }
    }

    /* 7. 重新进入 ACD（si523_acd_init 末尾会写 CommandReg=0xB0） */
    si523_acd_init();
}

void si523_task(void *arg)
{
    while (1)
    {
        if (xSemaphoreTake(si523_semaphore, portMAX_DELAY) == pdTRUE)
        {
            notify_user_activity();
            gpio_intr_disable(SI523_INT_PIN); // Disable GPIO interrupt

            switch (si523_acd_irq_process())
            {
            case 0: // Other_IRQ（OSCMon/RFLowDetect/ACC）
                ESP_LOGI(TAG, "Other_IRQ: handle as card detected");
                si523_handle_card_detected();
                break;

            case 1: // ACD_IRQ（有卡靠近）
                ESP_LOGI(TAG, "ACD_IRQ: handle as card detected");
                si523_handle_card_detected();
                break;

            case 2: // ACDTIMER_IRQ（看门狗中断）
                ESP_LOGI(TAG, "ACDTIMER_IRQ: reconfigure the register");
                si523_soft_reset();  // 软复位（含 GSN 配置）
                si523_type_a_init(); // 重新初始化 TypeA
                si523_acd_init();    // 重新进入 ACD
                break;
            }
            gpio_intr_enable(SI523_INT_PIN); // Enable GPIO interrupt
        }
    }
}

esp_err_t si523_initialization(void)
{
    si523_i2c_init();
    si523_gpio_init();
    si523_hard_reset();
    si523_init();
    xTaskCreate(si523_task, "si523_task", 8192, NULL, 10, NULL);
    return ESP_OK;
}