#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DisplayInterface.h"

// Referencia a la instancia global definida en SerialOutput.c
extern InterfaceModule UI;

void app_main(void) {
    // 1. Inicializar la UI
    UI.initUi(NULL);

    // 2. Registrar los elementos estáticos (no necesitamos guardar sus IDs)
    UI.addTitle("ESP32 DASHBOARD");
    UI.addSubtitle("System Status");

    // Registrar los elementos dinámicos (estos sí llevan update, guardamos el ID)
    ui_eid_t bat_id = UI.addProgressBar("Battery", 100, "4.2V");
    ui_eid_t joy_id = UI.addBipolarProgressBar("Joystick X", 0, "0 deg");
    ui_eid_t wifi_id = UI.addStateIndicator("WiFi Link", true);
    ui_eid_t alert_id = UI.addAlertIndicator("OVERHEATING WARNING");

    int battery_level = 100;
    int joy_pos = 0;
    int joy_dir = 5;
    int loop_counter = 0;

    // 3. Bucle infinito de actualización
    while (1) {
        // Simular descarga de batería
        battery_level -= 1;
        if (battery_level < 0) battery_level = 100;
        
        // Agrandamos el buffer a 16 bytes para evitar el warning de truncamiento
        char bat_val_str[16]; 
        snprintf(bat_val_str, sizeof(bat_val_str), "%d%%", battery_level);
        UI.updateProgressBar(bat_id, battery_level, bat_val_str);

        // Simular movimiento del joystick
        joy_pos += joy_dir;
        if (joy_pos > 100 || joy_pos < -100) {
            joy_dir = -joy_dir;
        }
        
        char joy_val_str[16];
        snprintf(joy_val_str, sizeof(joy_val_str), "%d deg", joy_pos);
        UI.updateBipolarProgressBar(joy_id, joy_pos, joy_val_str);

        // Simular alertas y estado de red
        if (loop_counter % 20 == 0) {
            UI.updateStateIndicator(wifi_id, false);
            UI.triggerAlertIndicator(alert_id, "TEMP OVER 80C!");
        } else if (loop_counter % 20 == 10) {
            UI.updateStateIndicator(wifi_id, true);
            UI.clearAlertIndicator(alert_id, "");
        }

        // 4. Renderizar cambios
        UI.refreshUi();

        loop_counter++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}