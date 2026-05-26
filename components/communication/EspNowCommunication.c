/**
 * @file EspNowComunication.c
 * @brief Implementación del contrato de comunicación para ESP-IDF v5.3 usando ESP-NOW.
 */

#include "CommunicationInterface.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdatomic.h>


#define ESP_NOW_CHANNEL 1 // Canal Wi-Fi para ESP-NOW  (1-13 = canal específico)
#define MAX_QUEUE_ELEMENTS   10 // Tamaño máximo de la cola para eventos entrantes
#define DEST_MAC_ADDRESS  {0x24, 0x0A, 0xC4, 0x12, 0x34, 0x56} //MAC de destino

#define RSSI_MIN (-95)
#define RSSI_MAX (-30)

static uint8_t peer_mac_address[6] = DEST_MAC_ADDRESS;

static const char *TAG_ESPNOW = "COMM_ESPNOW";

static atomic_bool is_initialized = ATOMIC_VAR_INIT(false);

// Variable privada para el RSSI y su Mutex protector
//static int last_received_rssi = -120;
static atomic_int last_received_rssi = ATOMIC_VAR_INIT(-120);


typedef struct {
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    size_t  length;
} EspNowPacket_t;

static QueueHandle_t rx_packet_queue = NULL;

// Punteros a funciones de main (Callbacks de la interfaz)
static void (*private_on_receive_cb)(const void *payload, size_t length) = NULL;
static void (*private_on_send_cb)(bool success) = NULL;


/* =========================================================================
 * CALLBACKS NATIVOS DE ESP-NOW (ESP-IDF v5.3)
 * ========================================================================= */

static void native_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!atomic_load(&is_initialized)) return;
    if (recv_info != NULL && recv_info->rx_ctrl != NULL) {
        atomic_store(&last_received_rssi, recv_info->rx_ctrl->rssi)
    }
    // Encolar O disparar callback (Exclusión mutua)
    if (data != NULL && len > 0) {
        
        if (private_on_receive_cb != NULL) {
            //Si hay callback definido, el paquete se procesa de forma asíncrona inmediatamente
            private_on_receive_cb(data, (size_t)len);
        } 
        else if (rx_packet_queue != NULL) {
            // Si NO hay callback, se guarda en la cola para procesamiento síncrono por polling (receive)
            EspNowPacket_t packet;
            packet.length = (len > ESP_NOW_MAX_DATA_LEN) ? ESP_NOW_MAX_DATA_LEN : len;
            memcpy(packet.data, data, packet.length);

            // Intentamos meter el paquete en la cola. Si está llena, se descarta.
            if (xQueueSend(rx_packet_queue, &packet, 0) != pdTRUE) {
                ESP_LOGW(TAG_ESPNOW, "¡Cola llena! Paquete descartado por saturación.");
            }
        } 
        else {
            // No hay callback configurado Y la cola no existe
            ESP_LOGE(TAG_ESPNOW, "Paquete recibido pero no hay destino (sin callback ni cola activa).");
        }
    }
}

static void native_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (!atomic_load(&is_initialized)) return;

    if (private_on_send_cb != NULL) {
        // Traduccion de enum nativo a booleano simple (Éxito = true)
        private_on_send_cb(status == ESP_NOW_SEND_SUCCESS);
    }
}

/* =========================================================================
 * IMPLEMENTACIÓN DEL CONTRATO DE COMUNICACIÓN
 * ========================================================================= */

static int espnow_impl_init(void *args) {
    if (atomic_load(&is_initialized)) {
        ESP_LOGW(TAG_ESPNOW, "El módulo ya se encuentra inicializado.");
        return -1; 
    }

    if (args != NULL) {
        memcpy(peer_mac_address, (uint8_t *)args, 6);
    }
    // Creación de la Cola para RX (Heap)
    rx_packet_queue = xQueueCreate(MAX_QUEUE_ELEMENTS, sizeof(EspNowPacket_t));
    if (rx_packet_queue == NULL) {
        ESP_LOGE(TAG_ESPNOW, "No se pudo crear la cola RX (Falta de memoria RAM).");
        return -1;
    }

    
    // Inicializar la memoria no volátil (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Fallo crítico al inicializar NVS Flash.");
        goto err_clean_queue; // rollback
    }

    // Verificar Netif
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_ESPNOW, "Fallo en esp_netif_init(): %s", esp_err_to_name(ret));
        goto err_clean_queue;
    }

    // Verificar Event Loop preexistente
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_ESPNOW, "Fallo en esp_event_loop_create_default(): %s", esp_err_to_name(ret));
        goto err_clean_netif; 
    }
    
    // Configuración e inicialización de Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_ESPNOW, "Fallo en esp_wifi_init(): %s", esp_err_to_name(ret));
        goto err_clean_event_loop;
    }

    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Error al configurar o encender la radio Wi-Fi física.");
        goto err_clean_wifi;
    }

    // Inicializar el protocolo ESP-NOW
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Error al arrancar el protocolo ESP-NOW.");
        goto err_stop_wifi;
    }

    // Enlazar las funciones de hardware con callbacks seguros 
    if (esp_now_register_send_cb(native_now_send_cb) != ESP_OK ||
        esp_now_register_recv_cb(native_now_recv_cb) != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Error al registrar los callbacks nativos de ESP-NOW.");
        goto err_deinit_espnow;
    }

    atomic_store(&is_initialized, true);
    ESP_LOGI(TAG_ESPNOW, "Módulo configurado en v5.3");
    return 0;
    
    err_deinit_espnow:
        esp_now_deinit();

    err_stop_wifi:
        esp_wifi_stop();

    err_clean_wifi:
        esp_wifi_deinit();

    err_clean_event_loop:
        esp_event_loop_delete_default();

    err_clean_netif:
        esp_netif_deinit();


    err_clean_queue:
        if (rx_packet_queue != NULL) {
            vQueueDelete(rx_packet_queue);
            rx_packet_queue = NULL;
        }

        return -2;
}

static int espnow_impl_connect(void) {
    if (!atomic_load(&is_initialized)) return -1;

    // Configuración del Peer destino en el ecosistema ESP-NOW
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, peer_mac_address, 6);
    peer_info.channel = ESP_NOW_CHANNEL;
    peer_info.ifidx = WIFI_IF_STA;  // Interfaz Estación
    peer_info.encrypt = false;      // Modo directo sin encriptación para velocidad analítica

    // Limpieza preventiva si el nodo ya estaba registrado
    if (esp_now_is_peer_exist(peer_mac_address)) {
        esp_now_del_peer(peer_mac_address);
    }

    // Agregar Peer a la tabla del procesador del ESP32
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Error crítico al registrar el Peer de destino");
        return -2;
    }

    ESP_LOGI(TAG_ESPNOW, "Conexión virtual establecida con el Peer destino.");
    return 0;
}

static void espnow_impl_disconnect(void) {
    return -1 // no implementada
}

static bool espnow_impl_isConnected(void) {
    // Retorna si el hardware está listo y el peer está activo en la tabla
    return (atomic_load(&is_initialized) && esp_now_is_peer_exist(peer_mac_address));
}

static int espnow_impl_send(const void *payload, size_t length) {
    if (!espnow_impl_isConnected() || payload == NULL || length == 0) return -1;
    
    // ESP-NOW soporta un límite físico estricto de 250 bytes por ráfaga
    if (length > ESP_NOW_MAX_DATA_LEN)
    {
        ESP_LOGE(TAG_ESPNOW, "Payload excede límite ESP-NOW (%zu > %d)", length, ESP_NOW_MAX_DATA_LEN);
        return -1;
    }

    // Disparamos el paquete por el aire sin bloquear el flujo principal
    esp_err_t err = esp_now_send(peer_mac_address, (const uint8_t *)payload, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ESPNOW, "Error al enviar datos por ESP-NOW: %s", esp_err_to_name(err));
        return -2;
    }

    return (int)length; // Retorna los bytes que se enviaron exitosamente al búfer de salida
}

static int espnow_impl_receive(void *buffer, size_t max_length, uint32_t timeout_ms) {
    if (!atomic_load(&is_initialized) || rx_packet_queue == NULL || buffer == NULL || max_length == 0) {
        return -1; // Error de parámetros o módulo apagado
    }

    EspNowPacket_t received_packet;

    // Traduccion de timeout del contrato a Ticks de FreeRTOS
    TickType_t ticks_to_wait = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    // Bloqueo hasta que llegue un paquete o se agote el tiempo de espera
    if (xQueueReceive(rx_packet_queue, &received_packet, ticks_to_wait) == pdTRUE) {
        
        // Verificamos que los datos entren en el buffer del usuario para evitar desbordamientos (Buffer Overflow)
        size_t bytes_to_copy = (received_packet.length > max_length) ? max_length : received_packet.length;
        memcpy(buffer, received_packet.data, bytes_to_copy);

        return (int)bytes_to_copy; // Éxito: devolvemos la cantidad de bytes reales leídos
    }

    return 0; // Timeout: Pasó el tiempo y no llegó ningún paquete por el aire
}

static void espnow_impl_onReceive(void (*callback)(const void *payload, size_t length)) {
    private_on_receive_cb = callback; // Mapeo directo al main
}

static void espnow_impl_onSend(void (*callback)(bool success)) {
    private_on_send_cb = callback; // Mapeo directo al main
}

static int espnow_impl_comunicationQuality(void) {
    if (!espnow_impl_isConnected()) {
        return 0;
    }

    int rssi = -120; // Valor de respaldo seguro

    rssi = atomic_load(&last_received_rssi);

    if (rssi <= RSSI_MIN) return 0;
    if (rssi >= RSSI_MAX) return 100;

    //Mapeo lineal para transformar el rango [RSSI_MIN, RSSI_MAX] a [0, 100]
    return (int)((rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN));
}

/* =========================================================================
 * ASIGNACIÓN E INYECCIÓN DEL OBJETO GLOBAL
 * ========================================================================= */
CommunicationModule Communication = {
    .init                = espnow_impl_init,
    .connect             = espnow_impl_connect,
    .disconnect          = espnow_impl_disconnect,
    .isConnected         = espnow_impl_isConnected,
    .send                = espnow_impl_send,
    .receive             = espnow_impl_receive,
    .onReceive           = espnow_impl_onReceive,
    .onSend              = espnow_impl_onSend,
    .comunicationQuality = espnow_impl_comunicationQuality
};