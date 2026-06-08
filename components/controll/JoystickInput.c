#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include <string.h>
#include "esp_log.h"

#include "ControllInterface.h"

// ============================================================================
// DEFINICIÓN DE PINES Y CONFIGURACIÓN (Hardware Settings)
// ============================================================================

// Ejes del Joystick (Deben ser ADC1 para no causar conflictos con el Wi-Fi)
#define PIN_JOYSTICK_X      ADC_CHANNEL_4 // GPIO 32
#define PIN_JOYSTICK_Y      ADC_CHANNEL_5 // GPIO 33

// Botones (Pines seguros con soporte de PULL-UP interno)
#define PIN_BTN_SW          GPIO_NUM_25 // Botón al presionar el joystick
#define PIN_BTN_A           GPIO_NUM_26
#define PIN_BTN_B           GPIO_NUM_27

// ============================================================================
// CONFIGURACIÓN DE RENDIMIENTO Y SENSIBILIDAD
// ============================================================================

// Frecuencia de muestreo del Joystick.
// Afecta a la CPU: 100Hz significa que la tarea del joystick despertará
// 100 veces por segundo. Como usamos vTaskDelayUntil, la CPU entra en 
// reposo el resto del tiempo (no hay busy wait, 0% CPU en espera).
#define JOYSTICK_FREQ_HZ    35//! Si subimos de 100 muere

// Sensibilidad (Deadzone/Banda Muerta). Rango del ADC: 0 a 4095.
// Afecta a la CPU: Un umbral más alto reduce el envío de eventos "basura" 
// por ruido eléctrico, disminuyendo enormemente los cambios de contexto (context switch) 
// hacia la tarea principal que escucha la cola.
#define JOYSTICK_DEADZONE   100 //* con 10 anda joia pero problemas en send

// Prioridad del hilo del Joystick.
// En FreeRTOS, números altos = mayor prioridad. 
// Tiene prioridad alta (5) para que no haya lag (latencia).
#define JOYSTICK_TASK_PRIO  5

// Antirebote en milisegundos para los botones físicos
#define DEBOUNCE_TIME_MS    200
#define DEBOUNCE_TABLE_SIZE 3

// Tamaño máximo de la cola de eventos del sistema de control
#define CONTROL_QUEUE_LEN   2

// ============================================================================
// ESTRUCTURAS INTERNAS Y VARIABLES GLOBALES
// ============================================================================

// Identificadores de canales (CIDs)
enum {
    CID_JOY_X = 10,
    CID_JOY_Y = 11,
    CID_BTN_SW = 20,
    CID_BTN_A = 21,
    CID_BTN_B = 22
};

static const char *TAG = "JOYSTICK";

static const int available_cids[] = { CID_JOY_X, CID_JOY_Y, CID_BTN_SW, CID_BTN_A, CID_BTN_B };

// Estructura interna para encolar datos (Segura para FreeRTOS)
typedef struct {
    int cid;
    int value; // Contiene el valor analógico o el estado del botón (1/0)
} InternalEvent;

static QueueHandle_t event_queue = NULL;

// Mux de FreeRTOS para control de regiones críticas (Spinlock multicore seguro para ISR)

static adc_oneshot_unit_handle_t adc1_handle;
static bool s_initialized = false;

// ============================================================================
// RUTINAS DE INTERRUPCIÓN (ISRs) - Máxima velocidad, mínimo procesamiento
// ============================================================================

// ISR genérica manejada por macros para cada botón
static void IRAM_ATTR button_isr_handler(void* arg) {
    int cid = *(const int*)arg;
    InternalEvent ev = { .cid = cid, .value = 1 };
    BaseType_t wakeup = pdFALSE;
    xQueueSendFromISR(event_queue, &ev, &wakeup);
    if (wakeup) portYIELD_FROM_ISR();
}

// ============================================================================
// HILO DEL JOYSTICK (Task)
// ============================================================================

static void joystick_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency_ticks = pdMS_TO_TICKS(1000 / JOYSTICK_FREQ_HZ);

    int last_x = -1000;
    int last_y = -1000;

    while (1) {
        // vTaskDelayUntil duerme el hilo de forma exacta según la frecuencia.
        // Afecta a la CPU: Libera el procesador por completo (0% uso) hasta que expire el tiempo.
        vTaskDelayUntil(&last_wake_time, frequency_ticks);

        int current_x = 0;
        int current_y = 0;
        adc_oneshot_read(adc1_handle, PIN_JOYSTICK_X, &current_x);
        adc_oneshot_read(adc1_handle, PIN_JOYSTICK_Y, &current_y);

        // Solo encolamos si el cambio supera el Deadzone (ahorra muchísimos cambios de contexto)
        if (abs(current_x - last_x) > JOYSTICK_DEADZONE) {
            InternalEvent ev = { .cid = CID_JOY_X, .value = current_x };
            xQueueSend(event_queue, &ev, 0);
            last_x = current_x;
        }

        if (abs(current_y - last_y) > JOYSTICK_DEADZONE) {
            InternalEvent ev = { .cid = CID_JOY_Y, .value = current_y };
            xQueueSend(event_queue, &ev, 0);
            last_y = current_y;
        }
    }
}

// ============================================================================
// IMPLEMENTACIÓN DE LA INTERFAZ
// ============================================================================

static const int* get_availables_ids_impl(void) {
    return available_cids;
}

static size_t get_availables_id_len_impl(void) {
    return sizeof(available_cids) / sizeof(available_cids[0]);
}

static int disconnect_impl(int cid) {
     // Mapeo de CID al pin GPIO correspondiente para poder desregistrar la ISR
    gpio_num_t pin = GPIO_NUM_NC; // GPIO_NUM_NC = "No Connected", valor inválido seguro

    switch (cid) {
        case CID_BTN_SW: pin = PIN_BTN_SW; break;
        case CID_BTN_A:  pin = PIN_BTN_A;  break;
        case CID_BTN_B:  pin = PIN_BTN_B;  break;

        case CID_JOY_X:
        case CID_JOY_Y:
            // Los ejes analógicos no tienen ISR — no hay nada que desregistrar
            ESP_LOGW(TAG, "disconnect(%d): los ejes del joystick no soportan desconexión individual", cid);
            return -1;

        default:
            ESP_LOGE(TAG, "disconnect(%d): CID desconocido", cid);
            return -1;
    }

    // Desregistrar el handler de ISR para este pin específico
    esp_err_t err = gpio_isr_handler_remove(pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "disconnect(%d): gpio_isr_handler_remove falló: %s", cid, esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "disconnect(%d): ISR del pin %d desregistrada correctamente", cid, pin);
    return 0;
}

static int cid_to_debounce_index(int cid) {
    switch (cid) {
        case CID_BTN_SW: return 0;
        case CID_BTN_A:  return 1;
        case CID_BTN_B:  return 2;
        default:         return -1; // CID no tiene antirebote
    }
}

static int64_t s_last_processed_us[DEBOUNCE_TABLE_SIZE] = {0};

static bool wait_event_impl(ControllEvent *event, void *buffer, uint32_t timeout_ms) {
    if (!event_queue || !event) return false;

    // timeout_ms == 0 → esperar indefinidamente (portMAX_DELAY)
    // Cualquier otro valor → convertir a ticks de FreeRTOS
    TickType_t ticks_to_wait = (timeout_ms == 0)
                               ? portMAX_DELAY
                               : pdMS_TO_TICKS(timeout_ms);

    InternalEvent raw;

    // El while permite descartar rebotes y seguir esperando el próximo
    // evento válido sin retornar false prematuramente al llamador.
    // Si el timeout original expiró, xQueueReceive retorna pdFALSE
    // y salimos naturalmente.
    while (xQueueReceive(event_queue, &raw, ticks_to_wait) == pdTRUE) {

        // Solo los botones necesitan antirebote — el joystick ya tiene
        // su propio filtro por deadzone en la tarea.
        // Distinguimos por rango de CID: botones son CID >= CID_BTN_SW (20)
        bool needs_debounce = (raw.cid >= CID_BTN_SW);

        if (needs_debounce) {

            int idx = cid_to_debounce_index(raw.cid);
            if (idx < 0 || idx >= DEBOUNCE_TABLE_SIZE) {
                // CID de botón desconocido — no sabemos cómo hacer antirebote
                ESP_LOGW(TAG, "CID %d marcado para antirebote pero sin entrada en tabla", raw.cid);
                continue;
            }


            // esp_timer_get_time() es seguro aquí: estamos en contexto
            // de tarea normal, no en ISR. Retorna microsegundos desde boot.
            int64_t now_us = esp_timer_get_time();
            int64_t elapsed_us = now_us - s_last_processed_us[idx];
            int64_t debounce_us = (int64_t)DEBOUNCE_TIME_MS * 1000LL;

            if (elapsed_us < debounce_us) {
                // Evento dentro del período de rebote → descartar silenciosamente
                // No logueamos aquí para no generar overhead en cada rebote
                continue;
            }

            // Evento válido: actualizar timestamp antes de procesar
            s_last_processed_us[idx] = now_us;
        }

        // Evento válido — llenar el struct del llamador
        event->cid    = raw.cid;
        event->length = sizeof(int);

        // Copiar el valor al buffer externo del llamador si fue provisto.
        // El llamador es dueño de ese buffer — no hay punteros a memoria interna.
        if (buffer != NULL) {
            memcpy(buffer, &raw.value, sizeof(int));
        }

        return true;
    }

    // xQueueReceive retornó pdFALSE: timeout agotado sin evento válido
    return false;
}

static int init_impl(void* args) {
     if (s_initialized) {
        ESP_LOGW(TAG, "JoystickInput ya inicializado");
        return -1;
    }

    event_queue = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(InternalEvent));
    if (!event_queue) {
        ESP_LOGE(TAG, "No se pudo crear la cola de eventos");
        return -1;
    }

    // Inicializar unidad ADC1
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_config1, &adc1_handle) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar ADC1");
        vQueueDelete(event_queue);
        event_queue = NULL;
        return -1;
    }

    // Configurar canales — ADC_ATTEN_DB_11 es la constante oficial en ESP-IDF v5
    // para el rango máximo de 0–3.9V. DB_12 no existe como constante estándar.
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };

    if (adc_oneshot_config_channel(adc1_handle, PIN_JOYSTICK_X, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar canal ADC para eje X");
        adc_oneshot_del_unit(adc1_handle);
        vQueueDelete(event_queue);
        event_queue = NULL;
        return -1;
    }

    if (adc_oneshot_config_channel(adc1_handle, PIN_JOYSTICK_Y, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar canal ADC para eje Y");
        adc_oneshot_del_unit(adc1_handle);
        vQueueDelete(event_queue);
        event_queue = NULL;
        return -1;
    }

    // Crear tarea del joystick — verificar que FreeRTOS pudo reservar el stack
    // Si el heap está fragmentado o lleno, xTaskCreate retorna pdFAIL silenciosamente
    BaseType_t task_created = xTaskCreate(
        joystick_task,
        "joy_task",
        4096,
        NULL,
        JOYSTICK_TASK_PRIO,
        NULL
    );
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea del joystick (heap insuficiente)");
        adc_oneshot_del_unit(adc1_handle);
        vQueueDelete(event_queue);
        event_queue = NULL;
        return -1;
    }

    // Configurar pines de botones con PULLUP interno
    gpio_config_t btn_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_BTN_SW) | (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_conf);

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE significa que ya estaba instalado — eso está bien
        ESP_LOGE(TAG, "gpio_install_isr_service falló: %s", esp_err_to_name(isr_err));
        // rollback...
        return -1;
    }

    static const int cid_sw = CID_BTN_SW;
    static const int cid_a  = CID_BTN_A;
    static const int cid_b  = CID_BTN_B;
    gpio_isr_handler_add(PIN_BTN_SW, button_isr_handler, (void*)&cid_sw);
    gpio_isr_handler_add(PIN_BTN_A,  button_isr_handler, (void*)&cid_a);
    gpio_isr_handler_add(PIN_BTN_B,  button_isr_handler, (void*)&cid_b);

    s_initialized = true;
    ESP_LOGI(TAG, "JoystickInput inicializado correctamente");
    return 0;
}

// Instancia de la interfaz exportable
ControllModule HardwareController = {
    .init = init_impl,
    .disconnect = disconnect_impl,
    .get_availables_ids = get_availables_ids_impl,
    .get_availables_id_len = get_availables_id_len_impl,
    .waitEvent = wait_event_impl
};