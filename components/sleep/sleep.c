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

        gpio_intr_disable(FINGERPRINT_INT_PIN);
        gpio_intr_disable(SI523_INT_PIN);
        gpio_intr_disable(SI14TP_INT_PIN);

        gpio_wakeup_enable(SI14TP_INT_PIN, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(FINGERPRINT_INT_PIN, GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable(SI523_INT_PIN, GPIO_INTR_LOW_LEVEL);

        esp_sleep_enable_gpio_wakeup();

        esp_light_sleep_start();

        g_last_activity_time = esp_timer_get_time();

        ESP_LOGI(TAG, "Wake up from sleep");
        gpio_wakeup_disable(SI14TP_INT_PIN);
        gpio_wakeup_disable(FINGERPRINT_INT_PIN);
        gpio_wakeup_disable(SI523_INT_PIN);

        uint32_t wakeup_mask = esp_sleep_get_wakeup_causes();

        if (wakeup_mask & BIT(ESP_SLEEP_WAKEUP_GPIO))
        {
            uint64_t gpio_mask = esp_sleep_get_gpio_wakeup_status();
            ESP_LOGI(TAG, "Wakeup source is GPIO, mask=0x%08X", wakeup_mask); // 为什么都是Wakeup source is GPIO, mask=0x00000080，无论是指纹触发还是触摸键盘触发，还是刷卡触发，都是这个值

            if (gpio_get_level(FINGERPRINT_INT_PIN) == 1)
            {
                ESP_LOGI(TAG, "Fingerprint touch detected");
            }
            if (gpio_get_level(SI14TP_INT_PIN) == 0)
            {
                ESP_LOGI(TAG, "SI14TP touch detected");
            }
            if (gpio_get_level(SI523_INT_PIN) == 0)
            {
                ESP_LOGI(TAG, "SI523 card detected");
            }

            if (gpio_mask & (1ULL << FINGERPRINT_INT_PIN)) // 如何正确打印？
            {
                ESP_LOGI(TAG, "Wakeup source: FINGERPRINT_INT_PIN");
                // xSemaphoreGive(fingerprint_semaphore);
            }
            if (gpio_mask & (1ULL << SI523_INT_PIN))
            {
                ESP_LOGI(TAG, "Wakeup source: SI523_INT_PIN");
                // xSemaphoreGive(si523_semaphore);
            }
            if (gpio_mask & (1ULL << SI14TP_INT_PIN))
            {
                ESP_LOGI(TAG, "Wakeup source: SI14TP_INT_PIN");
                // xSemaphoreGive(si14tp_semaphore);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Wakeup source is not GPIO, mask=0x%08X", wakeup_mask);
        }
        gpio_intr_enable(FINGERPRINT_INT_PIN);
        gpio_intr_enable(SI523_INT_PIN);
        gpio_intr_enable(SI14TP_INT_PIN);
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