/**
 * @file main.c
 * @brief ESP32 Joystick Controller — Punto de entrada principal.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "CommunicationInterface.h"
#include "ControllInterface.h"
#include "DisplayInterface.h"

static const char *TAG = "MAIN";

// ============================================================================
// CONFIGURACIÓN AJUSTABLE
// ============================================================================

// Prioridades (mayor número = mayor prioridad en FreeRTOS)
#define TASK_PRIO_CONTROL       6
#define TASK_PRIO_INCOMING      4
#define TASK_PRIO_TELEMETRY     3
#define TASK_PRIO_UI            1

// Tamaños de stack en bytes
#define STACK_CONTROL           4096
#define STACK_INCOMING          3072
#define STACK_TELEMETRY         2048
#define STACK_UI                4096

// Frecuencias de tareas periódicas
#define TELEMETRY_PERIOD_MS     1000
#define UI_REFRESH_PERIOD_MS    100

// Timeout de receive() — cuánto espera incoming_data_task por un paquete.
// Con portMAX_DELAY bloquea sin consumir CPU hasta que llegue algo.
#define INCOMING_RECEIVE_TIMEOUT_MS  0xFFFFFFFF  // portMAX_DELAY equivalente

// CIDs definidos en JoystickInput.c (deben coincidir)
#define CID_JOY_X               10
#define CID_JOY_Y               11
#define CID_BTN_SW              20
#define CID_BTN_A               21
#define CID_BTN_B               22

// ============================================================================
// MÓDULOS — exportados por sus respectivos .c
// ============================================================================

extern CommunicationModule Communication;
extern ControllModule       HardwareController;
extern InterfaceModule      UI;

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

// Paquete de control enviado al robot en cada evento del joystick
typedef struct {
    int  joy_x;
    int  joy_y;
    bool btn_sw;
    bool btn_a;
    bool btn_b;
} ControlPacket_t;

// Paquete de telemetría recibido desde el robot
typedef struct {
    int battery_level;  // Porcentaje 0-100
    int speed;          // Unidad definida por el robot
} RobotTelemetry_t;

// Estructura central compartida entre tareas.
// Todo acceso debe hacerse bajo g_data_mutex.
typedef struct {
    ControlPacket_t  control;
    RobotTelemetry_t robot;
    int              signal_quality;
} SharedData_t;

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

static SharedData_t      g_data       = {0};
static SemaphoreHandle_t g_data_mutex = NULL;

// ============================================================================
// TAREAS
// ============================================================================

/**
 * @brief Tarea de control (ALTA PRIORIDAD).
 *
 * Bloquea en waitEvent() sin consumir CPU hasta que el joystick o un botón
 * generen un evento. Actualiza el estado compartido y transmite por ESP-NOW.
 */
static void control_task(void *pvParameters) {
    ControllEvent   event        = {0};
    int             event_value  = 0;
    ControlPacket_t packet       = {0};

    while (1) {
        // Bloquea internamente con portMAX_DELAY — 0% CPU hasta que hay evento
        if (!HardwareController.waitEvent(&event, &event_value, 0)) {
            continue;
        }

        // Actualizar campo correspondiente bajo mutex y capturar snapshot
        if (xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE) {
            switch (event.cid) {
                case CID_JOY_X:  g_data.control.joy_x  = event_value;        break;
                case CID_JOY_Y:  g_data.control.joy_y  = event_value;        break;
                case CID_BTN_SW: g_data.control.btn_sw = (event_value != 0); break;
                case CID_BTN_A:  g_data.control.btn_a  = (event_value != 0); break;
                case CID_BTN_B:  g_data.control.btn_b  = (event_value != 0); break;
                default: break;
            }
            // Snapshot fuera del mutex: no llamar send() mientras se sostiene el lock
            packet = g_data.control;
            xSemaphoreGive(g_data_mutex);
        }

        // Transmitir al robot — fuera del mutex para no bloquear otras tareas
        // mientras la radio procesa el envío
        if (Communication.send(&packet, sizeof(ControlPacket_t)) < 0) {
            ESP_LOGW(TAG, "control_task: send() falló");
        }
    }
}

/**
 * @brief Tarea de datos entrantes (PRIORIDAD MEDIA-ALTA).
 *
 * Llama a Communication.receive() que bloquea sobre la cola interna de
 * EspNowCommunication.c. Sin busy-wait: 0% CPU mientras no llegan paquetes.
 * Cuando recibe un paquete actualiza la estructura compartida bajo mutex.
 */
static void incoming_data_task(void *pvParameters) {
    RobotTelemetry_t pkt = {0};

    while (1) {
        // receive() bloquea internamente en xQueueReceive con el timeout dado.
        // Retorna > 0 si hay datos, 0 si timeout, < 0 si error.
        int received = Communication.receive(&pkt, sizeof(RobotTelemetry_t),
                                             INCOMING_RECEIVE_TIMEOUT_MS);
        if (received <= 0) {
            // 0 = timeout (normal), < 0 = error de parámetros o módulo apagado
            continue;
        }

        // Validar que recibimos exactamente el tamaño esperado
        if (received != sizeof(RobotTelemetry_t)) {
            ESP_LOGW(TAG, "incoming_data_task: paquete de tamaño inesperado (%d bytes)", received);
            continue;
        }

        // Escribir en estructura compartida bajo protección de mutex
        if (xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE) {
            g_data.robot = pkt;
            xSemaphoreGive(g_data_mutex);
        }
    }
}

/**
 * @brief Tarea de telemetría (PRIORIDAD MEDIA).
 *
 * Mide la calidad de señal ESP-NOW cada TELEMETRY_PERIOD_MS.
 * vTaskDelayUntil garantiza período exacto sin deriva temporal.
 */
static void telemetry_task(void *pvParameters) {
    TickType_t       last_wake = xTaskGetTickCount();
    const TickType_t period    = pdMS_TO_TICKS(TELEMETRY_PERIOD_MS);

    while (1) {
        // Duerme exactamente el período — 0% CPU durante la espera
        vTaskDelayUntil(&last_wake, period);

        // comunicationQuality() usa atomic internamente, es thread-safe sin mutex
        int quality = Communication.comunicationQuality();

        if (xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE) {
            g_data.signal_quality = quality;
            xSemaphoreGive(g_data_mutex);
        }

        ESP_LOGI(TAG, "Calidad de señal: %d%%", quality);
    }
}

/**
 * @brief Tarea de UI (PRIORIDAD BAJA).
 *
 * Refresca la pantalla serial cada UI_REFRESH_PERIOD_MS.
 * Copia los datos compartidos en un snapshot local para minimizar
 * el tiempo que sostiene el mutex — el render ocurre fuera del lock.
 */
static void ui_task(void *pvParameters) {
    TickType_t       last_wake = xTaskGetTickCount();
    const TickType_t period    = pdMS_TO_TICKS(UI_REFRESH_PERIOD_MS);
    SharedData_t     snap      = {0};

    // Construcción del layout — una sola vez antes del loop
    UI.addTitle("ESP32 JOYSTICK CONTROLLER");

    UI.addSubtitle("Control Inputs");
    ui_eid_t id_joy_x  = UI.addBipolarProgressBar("Joystick X",  0, "ADC");
    ui_eid_t id_joy_y  = UI.addBipolarProgressBar("Joystick Y",  0, "ADC");
    ui_eid_t id_btn_a  = UI.addStateIndicator("Button A",  false);
    ui_eid_t id_btn_b  = UI.addStateIndicator("Button B",  false);
    ui_eid_t id_btn_sw = UI.addStateIndicator("Button SW", false);

    UI.addSubtitle("Link & Robot Status");
    ui_eid_t id_signal  = UI.addProgressBar("Signal Quality", 0, "Q");
    ui_eid_t id_battery = UI.addProgressBar("Robot Battery",  0, "%");
    ui_eid_t id_speed   = UI.addProgressBar("Robot Speed",    0, "RPM");

    while (1) {
        vTaskDelayUntil(&last_wake, period);

        // Snapshot rápido bajo mutex — el render ocurre fuera del lock
        if (xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE) {
            snap = g_data;
            xSemaphoreGive(g_data_mutex);
        }

        // Mapeo ADC crudo (0-4095) a bipolar (-100 a +100), centro ≈ 2048
        int x_pct = ((snap.control.joy_x - 2048) * 100) / 2048;
        int y_pct = ((snap.control.joy_y - 2048) * 100) / 2048;

        // Clampear por asimetría del ADC físico
        if (x_pct >  100) x_pct =  100;
        if (x_pct < -100) x_pct = -100;
        if (y_pct >  100) y_pct =  100;
        if (y_pct < -100) y_pct = -100;

        UI.updateBipolarProgressBar(id_joy_x,  x_pct, "");
        UI.updateBipolarProgressBar(id_joy_y,  y_pct, "");
        UI.updateStateIndicator(id_btn_a,  snap.control.btn_a);
        UI.updateStateIndicator(id_btn_b,  snap.control.btn_b);
        UI.updateStateIndicator(id_btn_sw, snap.control.btn_sw);
        UI.updateProgressBar(id_signal,  snap.signal_quality,      "Q");
        UI.updateProgressBar(id_battery, snap.robot.battery_level, "%");
        UI.updateProgressBar(id_speed,   snap.robot.speed,         "RPM");

        UI.refreshUi();
    }
}

// ============================================================================
// PUNTO DE ENTRADA
// ============================================================================

void app_main(void) {
    ESP_LOGI(TAG, "=== Iniciando sistema del controlador ===");

    // Mutex compartido — debe existir antes de inicializar módulos o crear tareas
    g_data_mutex = xSemaphoreCreateMutex();
    if (g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Fallo crítico: no se pudo crear el mutex.");
        return;
    }

    // Módulo de hardware (joystick + botones + ADC + ISRs)
    if (HardwareController.init(NULL) != 0) {
        ESP_LOGE(TAG, "HardwareController.init() falló.");
    }

    // Módulo de comunicación ESP-NOW
    // init(NULL) usa la MAC destino hardcodeada en EspNowCommunication.c
    if (Communication.init(NULL) != 0) {
        ESP_LOGE(TAG, "Communication.init() falló — sin comunicación inalámbrica.");
    } else if (Communication.connect() != 0) {
        ESP_LOGE(TAG, "Communication.connect() falló — peer no registrado.");
    }
    // UI
    UI.initUi(NULL);

    // 5. Creación de tareas
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(control_task,       "ctrl",  STACK_CONTROL,   NULL, TASK_PRIO_CONTROL,   NULL);
    ok &= xTaskCreate(incoming_data_task, "rx",    STACK_INCOMING,  NULL, TASK_PRIO_INCOMING,  NULL);
    ok &= xTaskCreate(telemetry_task,     "telem", STACK_TELEMETRY, NULL, TASK_PRIO_TELEMETRY, NULL);
    ok &= xTaskCreate(ui_task,            "ui",    STACK_UI,        NULL, TASK_PRIO_UI,        NULL);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Una o más tareas no pudieron crearse (heap insuficiente).");
    } else {
        ESP_LOGI(TAG, "Sistema inicializado. Todas las tareas activas.");
    }
}