#include "sleep.h"

static const char *TAG = "sleep";

uint8_t g_sleep_time = 0; // system sleep time (in seconds)

static void light_sleep_task(void *args)
{
    int64_t t_before_us = 0;
    int64_t t_after_us = 0;
    while (1)
    {
        /* Get timestamp before entering sleep */
        t_before_us = esp_timer_get_time();

        vTaskDelay(pdMS_TO_TICKS(g_sleep_time * 1000));

        ESP_LOGI(TAG, "Entering light sleep");
        if (zw111.power == true)
        {
            cancel_current_operation_and_execute_command(); // Cancel current operation
            prepare_turn_off_fingerprint();                 // Prepare to turn off fingerprint module
        }
        if (g_input_len != 0)
        {
            g_input_len = 0;
            memset(g_input_password, 0, sizeof(g_input_password));
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        gpio_wakeup_enable(SI14TP_INT_PIN, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(FINGERPRINT_INT_PIN, GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable(SI523_INT_PIN, GPIO_INTR_LOW_LEVEL);

        esp_sleep_enable_gpio_wakeup();

        esp_light_sleep_start();

        /* Get timestamp after waking up from sleep */
        t_after_us = esp_timer_get_time();

        ESP_LOGI(TAG, "Returned from light sleep, t=%lld ms, slept for %lld ms", t_after_us / 1000, (t_after_us - t_before_us) / 1000);
    }
    vTaskDelete(NULL);
}

esp_err_t sleep_initialization(void)
{
    esp_err_t err = nvs_custom_get_u8(NULL, "sleep", "sleep_time", &g_sleep_time);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "sleep time not found, using default");

        g_sleep_time = DEFAULT_SLEEP_TIME;

        nvs_custom_set_u8(NULL, "sleep", "sleep_time", g_sleep_time);
    }
    else
    {
        ESP_LOGI(TAG, "sleep time loaded from NVS");
    }
    xTaskCreate(light_sleep_task, "light_sleep_task", 4096, NULL, 6, NULL);
    return ESP_OK;
}