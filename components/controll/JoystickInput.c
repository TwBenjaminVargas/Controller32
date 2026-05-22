#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_timer.h"

#include "ControllInterface.h"

// ============================================================================
// DEFINICIÓN DE PINES Y CONFIGURACIÓN (Hardware Settings)
// ============================================================================

// Ejes del Joystick (Deben ser ADC1 para no causar conflictos con el Wi-Fi)
#define PIN_JOYSTICK_X      ADC1_CHANNEL_4 // GPIO 32
#define PIN_JOYSTICK_Y      ADC1_CHANNEL_5 // GPIO 33

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
#define JOYSTICK_FREQ_HZ    100 

// Sensibilidad (Deadzone/Banda Muerta). Rango del ADC: 0 a 4095.
// Afecta a la CPU: Un umbral más alto reduce el envío de eventos "basura" 
// por ruido eléctrico, disminuyendo enormemente los cambios de contexto (context switch) 
// hacia la tarea principal que escucha la cola.
#define JOYSTICK_DEADZONE   150 

// Prioridad del hilo del Joystick.
// En FreeRTOS, números altos = mayor prioridad. 
// Tiene prioridad alta (5) para que no haya lag (latencia).
#define JOYSTICK_TASK_PRIO  5

// Antirebote en milisegundos para los botones físicos
#define DEBOUNCE_TIME_MS    50

// Tamaño máximo de la cola de eventos del sistema de control
#define CONTROL_QUEUE_LEN   20

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

static const int available_cids[] = { CID_JOY_X, CID_JOY_Y, CID_BTN_SW, CID_BTN_A, CID_BTN_B };

// Estructura interna para encolar datos (Segura para FreeRTOS)
typedef struct {
    int cid;
    int value; // Contiene el valor analógico o el estado del botón (1/0)
} InternalEvent;

static QueueHandle_t event_queue = NULL;
static int last_event_value = 0; // Buffer estático para el payload de salida

// Mux de FreeRTOS para control de regiones críticas (Spinlock multicore seguro para ISR)
static portMUX_TYPE button_mux = portMUX_INITIALIZER_UNLOCKED;

// Variables para el antirebote en la ISR
static volatile uint32_t last_isr_time_sw = 0;
static volatile uint32_t last_isr_time_a = 0;
static volatile uint32_t last_isr_time_b = 0;

// ============================================================================
// RUTINAS DE INTERRUPCIÓN (ISRs) - Máxima velocidad, mínimo procesamiento
// ============================================================================

// ISR genérica manejada por macros para cada botón
static void IRAM_ATTR button_isr_handler(void* arg) {
    uint32_t current_time = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    int cid = (int)arg;
    volatile uint32_t* last_time = NULL;
    bool execute_enqueue = false;

    // Entrada a la región crítica: adquiere el spinlock y deshabilita interrupciones locales
    portENTER_CRITICAL_ISR(&button_mux);

    if (cid == CID_BTN_SW) last_time = &last_isr_time_sw;
    else if (cid == CID_BTN_A) last_time = &last_isr_time_a;
    else if (cid == CID_BTN_B) last_time = &last_isr_time_b;

    // Antirebote de extrema fortaleza por software (Lockout period)
    // No gasta ciclos extra si el botón "rebota", aborta al instante.
    if (last_time && (current_time - *last_time > DEBOUNCE_TIME_MS)) {
        *last_time = current_time;
        execute_enqueue = true;
    }

    // Salida inmediata de la región crítica
    portEXIT_CRITICAL_ISR(&button_mux);

    if (execute_enqueue) {
        InternalEvent ev;
        ev.cid = cid;
        ev.value = 1; // Botón presionado (el PULLUP lo pone a GND, pero lógicamente es 1)

        BaseType_t high_task_wakeup = pdFALSE;
        // Encolamos y notificamos si debemos cambiar el contexto
        xQueueSendFromISR(event_queue, &ev, &high_task_wakeup);
        if (high_task_wakeup) {
            portYIELD_FROM_ISR();
        }
    }
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

        int current_x = adc1_get_raw(PIN_JOYSTICK_X);
        int current_y = adc1_get_raw(PIN_JOYSTICK_Y);

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
    // Aquí podrías deshabilitar interrupciones de un pin específico
    return 0;
}

static bool wait_event_impl(ControllEvent *event, uint32_t timeout_ms) {
    if (!event_queue || !event) return false;

    TickType_t ticks_to_wait = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    InternalEvent internal_ev;

    // xQueueReceive frena la ejecución hasta que la ISR o el Hilo del Joystick envíen algo.
    if (xQueueReceive(event_queue, &internal_ev, ticks_to_wait) == pdTRUE) {
        event->cid = internal_ev.cid;
        last_event_value = internal_ev.value;
        
        event->data = &last_event_value; 
        event->length = sizeof(int);
        return true;
    }
    return false; // Timeout
}

static int init_impl(void* args) {
    //Crear la cola (capacidad parametrizada)
    event_queue = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(InternalEvent));
    if (!event_queue) return -1;

    // Configurar Hardware ADC (Joystick)
    adc1_config_width(ADC_WIDTH_BIT_12); // Resolución 0-4095
    adc1_config_channel_atten(PIN_JOYSTICK_X, ADC_ATTEN_DB_11); // Voltaje completo 0-3.3V
    adc1_config_channel_atten(PIN_JOYSTICK_Y, ADC_ATTEN_DB_11);

    // Iniciar el hilo del Joystick
    xTaskCreate(joystick_task, "joy_task", 2048, NULL, JOYSTICK_TASK_PRIO, NULL);

    // Configurar Botones (PULLUP interno)
    gpio_config_t btn_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Flanco de bajada (al presionar a GND)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_BTN_SW) | (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE // Mágico: Cero resistencias externas
    };
    gpio_config(&btn_conf);

    // Instalar servicio de ISR y adjuntar handlers
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BTN_SW, button_isr_handler, (void*)CID_BTN_SW);
    gpio_isr_handler_add(PIN_BTN_A, button_isr_handler, (void*)CID_BTN_A);
    gpio_isr_handler_add(PIN_BTN_B, button_isr_handler, (void*)CID_BTN_B);

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