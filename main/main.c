// mi_proyecto_robot/main/main.c

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Incluimos tu contrato
#include "CommunicationInterface.h"

static const char *TAG = "MAIN_APP";

// Traemos la instancia global que exportaste en EspNowComunication.c
extern CommunicationModule Communication;

// MAC Address del ESP32 destino (Receptor)
// OJO: Si estás probando con un solo ESP32 y no hay receptor, 
// los envíos van a fallar (ACK = false) pero el código no va a crashear.
uint8_t mac_destino[6] = {0x24, 0x0A, 0xC4, 0x12, 0x34, 0x56};

/**
 * @brief Callback opcional: Nos avisa si el paquete llegó a la otra antena (ACK de hardware)
 */
void al_enviar_datos(bool exito) {
    if (exito) {
        ESP_LOGI(TAG, ">> [TX] Paquete entregado correctamente (ACK OK).");
    } else {
        ESP_LOGW(TAG, ">> [TX] Fallo de entrega (Destino apagado o fuera de rango).");
    }
}

/**
 * @brief Tarea Consumidora: Escucha la cola de ESP-NOW sin bloquear el resto del robot
 */
void tarea_recepcion(void *pvParameters) {
    uint8_t buffer_rx[250]; // Buffer para guardar lo que llega

    while (1) {
        // La tarea se duerme acá hasta que llegue un paquete. 
        // Timeout de 5000ms (5 segundos). Si no llega nada, devuelve 0.
        int bytes_leidos = Communication.receive(buffer_rx, sizeof(buffer_rx), 5000);

        if (bytes_leidos > 0) {
            // Asumimos que nos mandaron texto para poder imprimirlo fácil
            buffer_rx[bytes_leidos] = '\0'; 
            
            // Leemos la calidad de señal del último paquete
            int calidad = Communication.comunicationQuality();

            ESP_LOGI(TAG, "<< [RX] %d bytes recibidos: '%s'", bytes_leidos, (char*)buffer_rx);
            ESP_LOGI(TAG, "--- Calidad de enlace: %d%%", calidad);
            
        } else if (bytes_leidos == 0) {
            // Timeout (No consumió CPU, solo se despertó porque pasaron 5s)
            ESP_LOGD(TAG, "[RX] Silencio en el aire... esperando datos.");
        } else {
            ESP_LOGE(TAG, "[RX] Error leyendo la cola de comunicación.");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/**
 * @brief Punto de entrada de ESP-IDF
 */
void app_main(void) {
    ESP_LOGI(TAG, "Arrancando sistema de comunicaciones...");

    // 1. Inicializamos el hardware (Pasamos la MAC destino)
    if (Communication.init(mac_destino) < 0) {
        ESP_LOGE(TAG, "Fallo catastrófico al inicializar módulo de comunicación.");
        return;
    }

    // 2. Conectamos el peer (Agrega la MAC a la tabla de ESP-NOW)
    if (Communication.connect() < 0) {
        ESP_LOGE(TAG, "Fallo al enlazar con el peer destino.");
        return;
    }

    // 3. Registramos nuestro callback de confirmación de envío
    Communication.onSend(al_enviar_datos);

    // 4. Lanzamos la tarea que se va a quedar escuchando la red
    // Le damos prioridad 5 (media-alta) y 4KB de RAM
    xTaskCreate(tarea_recepcion, "tarea_rx", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Robot listo. Iniciando bucle de transmisión...");

    // 5. Bucle principal: Enviamos telemetría
    int contador_mensajes = 0;
    char payload_tx[100];

    while (1) {
        // Armamos un paquete de texto de prueba
        snprintf(payload_tx, sizeof(payload_tx), "{\"sensor\":\"bateria\",\"volts\":12.4,\"msg_id\":%d}", contador_mensajes++);
        
        // Lo mandamos por el aire
        int enviados = Communication.send(payload_tx, strlen(payload_tx));
        
        if (enviados > 0) {
            ESP_LOGI(TAG, "Enviando ráfaga %d...", contador_mensajes);
        } else {
            ESP_LOGE(TAG, "Error empujando paquete a la antena.");
        }

        // El robot sigue haciendo sus cosas (simulamos 2 segundos de trabajo)
        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}