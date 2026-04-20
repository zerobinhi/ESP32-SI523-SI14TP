#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"
#include "zw111.h"
#include "si523.h"
#include "oled.h"
#include "si14tp.h"
#include "sleep.h"
#include "battery.h"
#include "nvs_custom.h"

static const char *TAG = "main";
size_t free_heap = 0;
void app_main(void)
{
    // initialize NVS
    if (nvs_custom_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "NVS initialization successful");
    }

    // initialize system components
    ESP_LOGI(TAG, "Initializing system components...");

    // initializing oled display
    if (oled_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "Oled display initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "Oled display initialization successful");
    }

    // initializing battery monitoring
    if (battery_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Battery monitoring initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "Battery monitoring initialization successful");
    }

    // initializing buzzer module
    if (smart_lock_buzzer_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Buzzer module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "Buzzer module initialization successful");
    }

    // initializing fingerprint module
    if (fingerprint_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "Fingerprint module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "Fingerprint module initialization successful");
    }

    // // initializing si14tp touch module
    // if (si14tp_initialization() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Si14tp module initialization failed");
    // }
    // else
    // {
    //     ESP_LOGI(TAG, "Si14tp module initialization successful");
    // }

    // // initializing si523 NFC module
    // if (si523_initialization() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Si523 module initialization failed");
    // }
    // else
    // {
    //     ESP_LOGI(TAG, "Si523 module initialization successful");
    // }

    // // initializing sleep function
    // if (sleep_initialization() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Sleep function initialization failed");
    // }
    // else
    // {
    //     ESP_LOGI(TAG, "Sleep function initialization successful");
    // }

    // spiffs_init_and_load_webpage();
    // wifi_init_softap();
    // web_server_start();

    ESP_LOGI(TAG, "Smart lock system initialization complete.");
}
