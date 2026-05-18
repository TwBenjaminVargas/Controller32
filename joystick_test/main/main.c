#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/adc.h"
#include "driver/gpio.h"

#define ESP_NOW_CHANNEL 6
#define ESP_NOW_FRECUENCY_MS 50

static const char *TAG = "SENDER";

// ── Cambiá esta MAC por la del receptor ──────────────────────────────────────
// Para obtenerla: corré en el receptor: ESP_LOGI(TAG, "%s", esp_mac_str);
// o usá el ejemplo get_started/hello_world y leé el log de boot
static uint8_t receiver_mac[6] = {0xB4, 0x8A, 0x0A, 0xB2, 0x97, 0x9C}; // broadcast por defecto
// ─────────────────────────────────────────────────────────────────────────────
//static uint8_t receiver_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define PIN_SW      GPIO_NUM_32
#define ADC_VRX     ADC1_CHANNEL_6   // GPIO34
#define ADC_VRY     ADC1_CHANNEL_7   // GPIO35
#define ADC_WIDTH   ADC_WIDTH_BIT_12 // 0-4095

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t btn;
} joystick_data_t;

static void espnow_send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Envío: %s", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    // Canal fijo — debe coincidir en ambos lados
    esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

static void espnow_init(void)
{
    esp_now_init();
    esp_now_register_send_cb(espnow_send_cb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = ESP_NOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void adc_init(void)
{
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_VRX, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_VRY, ADC_ATTEN_DB_11);
}

static void gpio_init_sw(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,  // KY-023 SW activo en LOW
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    espnow_init();
    adc_init();
    gpio_init_sw();

    ESP_LOGI(TAG, "Sender listo. Enviando datos...");

    joystick_data_t data;

    while (1) {
        data.x   = adc1_get_raw(ADC_VRX);
        data.y   = adc1_get_raw(ADC_VRY);
        data.btn = gpio_get_level(PIN_SW) == 0 ? 1 : 0; // 1 = presionado

        esp_now_send(receiver_mac, (uint8_t *)&data, sizeof(data));

        ESP_LOGI(TAG, "X=%4d  Y=%4d  BTN=%d", data.x, data.y, data.btn);
        vTaskDelay(pdMS_TO_TICKS(ESP_NOW_FRECUENCY_MS));
    }
}