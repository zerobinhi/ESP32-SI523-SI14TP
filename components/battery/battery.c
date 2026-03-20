#include "battery.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;

static esp_err_t adc_init(void)
{
    // initialize ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    // configure ADC channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    // initialize ADC calibration
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));

    ESP_LOGI(TAG, "ADC initialized");
    return ESP_OK;
}
void battery_task(void *arg)
{
    int voltage_mv = 0;

    while (1)
    {
        int sum = 0;
        int raw = 0;
        int avg_raw = 0;

        // read raw ADC value
        for (int i = 0; i < 8; i++)
        {
            adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
            sum += raw;
        }
        avg_raw = sum >> 3;

        // convert raw to voltage in mV
        adc_cali_raw_to_voltage(cali_handle, avg_raw, &voltage_mv);

        // calculate battery voltage (considering voltage divider)
        float battery_voltage = voltage_mv * ((float)(R_UPPER + R_LOWER) / R_LOWER);

        // ESP_LOGI(TAG, "Battery Voltage: %.2f mV, voltage_mv: %d", battery_voltage, voltage_mv);
        if (battery_voltage >= BATTERY_FULL_MV)
        {
            oled_draw_bitmap(112, 2, &c_chBat816_Full[0], 16, 8, 0);
        }
        else if (battery_voltage >= BATTERY_TWO_THIRD_MV)
        {
            oled_draw_bitmap(112, 2, &c_chBat816_TwoThird[0], 16, 8, 0);
        }
        else if (battery_voltage >= BATTERY_ONE_THIRD_MV)
        {
            oled_draw_bitmap(112, 2, &c_chBat816_OneThird[0], 16, 8, 0);
        }
        else
        {
            oled_draw_bitmap(112, 2, &c_chBat816_Empty[0], 16, 8, 0);
        }
        oled_refresh();
        // ESP_LOGI(TAG, "Battery voltage updated on OLED");
        // ===== Heap 信息 =================================================================================================================================
        // uint32_t free_heap = esp_get_free_heap_size();
        // uint32_t min_free_heap = esp_get_minimum_free_heap_size();
        // uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        // uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

        // ESP_LOGI(TAG, "====== HEAP INFO ======");
        // ESP_LOGI(TAG, "total: %lu", total_heap);
        // ESP_LOGI(TAG, "free : %lu", free_heap);
        // ESP_LOGI(TAG, "min  : %lu", min_free_heap);
        // ESP_LOGI(TAG, "used : %lu", total_heap - free_heap);
        // ESP_LOGI(TAG, "largest free block: %lu", largest_block);

        // // ===== 当前任务栈 =====
        // UBaseType_t stack_words = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI(TAG, "====== CURRENT TASK STACK ======");
        // ESP_LOGI(TAG, "stack free: %lu bytes", stack_words * 4);

        // // ===== 所有任务信息 =====
        // ESP_LOGI(TAG, "====== TASK LIST ======");
        // char *task_buffer = malloc(1024);
        // if (task_buffer)
        // {
        //     vTaskList(task_buffer);
        //     printf("%s\n", task_buffer);
        //     free(task_buffer);
        // }
        // else
        // {
        //     ESP_LOGE(TAG, "malloc failed for task list");
        // }

        // // ===== Heap 完整性检查 =====
        // ESP_LOGI(TAG, "====== HEAP CHECK ======");
        // if (heap_caps_check_integrity_all(true))
        // {
        //     ESP_LOGI(TAG, "heap integrity: OK");
        // }
        // else
        // {
        //     ESP_LOGE(TAG, "heap integrity: ERROR!");
        // }

        // // ===== Reset 原因 =====
        // esp_reset_reason_t reason = esp_reset_reason();
        // ESP_LOGI(TAG, "====== RESET REASON ======");
        // ESP_LOGI(TAG, "reset reason: %d", reason);

        // ESP_LOGI(TAG, "===============================");
        vTaskDelay(pdMS_TO_TICKS(5000)); // delay 30 seconds
    }
}

esp_err_t battery_init(void)
{
    adc_init();
    xTaskCreate(battery_task, "battery_task", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Battery task created");
    return ESP_OK;
}