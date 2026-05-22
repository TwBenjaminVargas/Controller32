/**
 * @file main.c
 * @brief Programa principal de prueba para el sistema de control del joystick.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Inclusión de la interfaz de control (ESP-IDF la encuentra automáticamente en el componente)
#include "ControllInterface.h"

// Etiqueta para los logs de ESP-IDF
static const char *TAG = "MAIN_APP";

// Declaramos la instancia externa que definiste al final de tu archivo fuente
extern ControllModule HardwareController;

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando sistema de pruebas del Joystick...");

    // 1. Inicializar el módulo de hardware (cola, ADC, hilos e interrupciones)
    if (HardwareController.init(NULL) != 0) {
        ESP_LOGE(TAG, "Error crítico: No se pudo inicializar el controlador de hardware.");
        return;
    }
    ESP_LOGI(TAG, "Hardware inicializado correctamente.");

    // 2. Probar los nuevos métodos de la interfaz para listar los CIDs disponibles
    size_t total_ids = HardwareController.get_availables_id_len();
    const int* ids_array = HardwareController.get_availables_ids();

    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "Canales de control detectados (%d en total):", total_ids);
    for (size_t i = 0; i < total_ids; i++) {
        ESP_LOGI(TAG, "  -> Canal ID (CID) disponible: %d", ids_array[i]);
    }
    ESP_LOGI(TAG, "=========================================");

    // Estructura local donde waitEvent volcará los datos recibidos
    ControllEvent received_event;

    ESP_LOGI(TAG, "Esperando eventos del joystick y botones (Mover palanca o presionar)...");

    // 3. Bucle principal de eventos
    while (1) {
        // Bloqueo eficiente de 0% CPU. Espera indefinida (timeout = 0)
        // La tarea se duerme por completo hasta que ocurra una interrupción o el hilo del joystick envíe datos.
        bool success = HardwareController.waitEvent(&received_event, 0);

        if (success) {
            // Reconstruimos el valor original apuntando al payload de data (que sabemos que es un entero)
            int event_value = *(const int*)received_event.data;

            // Procesamos el evento dependiendo de qué canal (CID) lo envió
            switch (received_event.cid) {
                case 10: // CID_JOY_X
                    ESP_LOGI(TAG, "[JOYSTICK] Eje X modificado -> Valor ADC: %d", event_value);
                    break;

                case 11: // CID_JOY_Y
                    ESP_LOGI(TAG, "[JOYSTICK] Eje Y modificado -> Valor ADC: %d", event_value);
                    break;

                case 20: // CID_BTN_SW
                    ESP_LOGW(TAG, "[BOTÓN] ¡Pulsador del Joystick presionado! (SW)");
                    break;

                case 21: // CID_BTN_A
                    ESP_LOGA(TAG, "[BOTÓN] ¡Botón A presionado!");
                    break;

                case 22: // CID_BTN_B
                    ESP_LOGA(TAG, "[BOTÓN] ¡Botón B presionado!");
                    break;

                default:
                    ESP_LOGW(TAG, "Aviso: Se recibió un evento de un CID desconocido: %d", received_event.cid);
                    break;
            }
        }
    }
}