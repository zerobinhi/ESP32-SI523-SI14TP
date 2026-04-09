#include "sleep.h"

static const char *TAG = "sleep";

uint8_t g_sleep_time = 0;
static int64_t g_last_activity_time = 0;

static void light_sleep_task(void *args)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(200));

        int64_t now = esp_timer_get_time();

        if ((now - g_last_activity_time) < (g_sleep_time * 1000000LL))
        {
            continue;
        }

        ESP_LOGI(TAG, "Idle timeout, entering light sleep");

        if (zw111.power == true)
        {
            cancel_current_operation_and_execute_command();
            prepare_turn_off_fingerprint();
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

        g_last_activity_time = esp_timer_get_time();

        ESP_LOGI(TAG, "Wake up from sleep");
    }
    vTaskDelete(NULL);
}

void notify_user_activity(void)
{
    g_last_activity_time = esp_timer_get_time();
}

esp_err_t sleep_initialization(void)
{
    g_sleep_time = DEFAULT_SLEEP_TIME;

    ESP_LOGI(TAG, "sleep time initialized to %d s", g_sleep_time);

    g_last_activity_time = esp_timer_get_time();

    xTaskCreate(light_sleep_task, "light_sleep_task", 4096, NULL, 6, NULL);
    return ESP_OK;
}