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
#include "battery.h"
#include "nvs_custom.h"

static const char *TAG = "main";
size_t free_heap = 0;
void app_main(void)
{
    // initialize NVS
    if (nvs_custom_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "nvs initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initialize system components
    ESP_LOGI(TAG, "initializing system components...");

    // initializing oled display
    if (oled_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "oled display initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "oled display initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initializing battery monitoring
    if (battery_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "battery monitoring initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "battery monitoring initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initializing buzzer module
    if (smart_lock_buzzer_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "buzzer module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "buzzer module initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initializing fingerprint module
    if (fingerprint_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "fingerprint module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "fingerprint module initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initializing si14tp touch module
    if (si14tp_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "si14tp module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "si14tp module initialization successful");
    }

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    // initializing si523 NFC module
    if (si523_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "si523 module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "si523 module initialization successful");
    }

    spiffs_init_and_load_webpage();
    wifi_init_softap();
    web_server_start();

    free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "当前剩余堆内存：%zu 字节", free_heap);

    ESP_LOGI(TAG, "function: %s, file: %s, line: %d", __func__, __FILE__, __LINE__);

    ESP_LOGI(TAG, "smart lock system initialization complete.");
}
