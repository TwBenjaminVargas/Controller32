/**
 * @file DisplayInterface.h
 * @brief Motor de Interfaz Gráfica Agnóstico para Sistemas Embebidos (ESP-IDF)
 * * Este componente define el contrato de diseño para una interfaz basada en 
 * Componentes Lógicos y Manejadores. Permite construir
 * Layouts dinámicos en pantallas Seriales ANSI, LCD, OLED o TFT.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Alias para el Identificador de Elemento (Element File Descriptor).
 * Representa el índice único de cada componente dentro de la memoria de la UI.
 */
typedef int ui_eid_t;

/**
 * @brief Estructura que define el contrato público de la interfaz.
 * Contiene punteros a función que encapsulan el comportamiento de cada componente.
 */
typedef struct {

    /**
     * @brief Inicializa el hardware del display o la configuración de la consola.
     * @param optionalArgs Puntero genérico para pasar configuraciones físicas 
     * (ej: dirección I2C, pines SPI). Pasar NULL si no se requiere.
     * @return int 0 si se inicializó correctamente, menor a 0 en caso de error.
     */
    int (*initUi)(void *optionalArgs);

    /**
     * @brief Sincroniza la memoria RAM con la pantalla física. Vuelca todos los
     * cambios y renderiza el Layout completo de un solo golpe (Flicker-free).
     * @return int 0 si el renderizado fue exitoso, menor a 0 en caso de error.
     */
    int (*refreshUi)(void);

    /**
     * @brief Borra lógicamente todos los elementos registrados en la memoria de la UI,
     * reiniciando el contador de elementos a cero.
     */
    void (*cleanUi)(void);

    /**
     * @brief Elimina un elemento específico de la interfaz liberando su espacio.
     * @param eid Identificador único del elemento que se desea remover.
     */
    void (*deleteElement)(ui_eid_t eid);

    /**
     * @brief Agrega un título principal destacado (normalmente centrado o en mayúsculas).
     * @param text Cadena de texto con el contenido del título.
     * @return ui_eid_t Identificador único asignado al componente, o -1 si está llena la memoria.
     */
    ui_eid_t (*addTitle)(const char *text);

    /**
     * @brief Agrega un subtítulo para divisiones de secciones dentro del Layout.
     * @param text Cadena de texto con el contenido del subtítulo.
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addSubtitle)(const char *text);

    /**
     * @brief Agrega una línea de texto plano genérica o etiqueta (Label).
     * @param text Cadena de texto a mostrar en pantalla.
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addTextLine)(const char *text);

    /**
     * @brief Actualiza el contenido de un título previamente creado.
     * @param eid Identificador único del título.
     * @param text Nuevo texto que reemplazará al anterior.
     */
    void (*updateTitle)(ui_eid_t eid, const char *text);

    /**
     * @brief Actualiza el contenido de un subtítulo previamente creado.
     * @param eid Identificador único del subtítulo.
     * @param text Nuevo texto que reemplazará al anterior.
     */
    void (*updateSubtitle)(ui_eid_t eid, const char *text);

    /**
     * @brief Actualiza el contenido de una línea de texto plano previamente creada.
     * @param eid Identificador único de la línea de texto.
     * @param text Nuevo texto que reemplazará al anterior.
     */
    void (*updateTextLine)(ui_eid_t eid, const char *text);

    /**
     * @brief Agrega una barra de progreso lineal tradicional (Rango de 0 a 100%).
     * @param title Etiqueta descriptiva que acompaña a la barra (ej: "Battery").
     * @param percentage Valor numérico inicial del progreso (0 a 100).
     * @param value Texto complementario para mostrar a la derecha (ej: "4.2V" o "85%").
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addProgressBar)(const char *title, int percentage, const char *value);

    /**
     * @brief Agrega una barra de progreso bipolar centrada (Rango de -100% a +100%).
     * Ideal para actuadores mecánicos, joysticks, timones o servomotores.
     * @param title Etiqueta descriptiva (ej: "Joystick X").
     * @param percentage Valor numérico inicial (-100 a 100). El 0 representa el centro.
     * @param value Texto complementario para mostrar a la derecha (ej: "-45 deg").
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addBipolarProgressBar)(const char *title, int percentage, const char *value);

    /**
     * @brief Actualiza el porcentaje y el texto descriptivo de una barra de progreso común.
     * @param eid Identificador único de la barra de progreso.
     * @param percentage Nuevo valor de porcentaje (0 a 100).
     * @param value Nuevo texto complementario.
     */
    void (*updateProgressBar)(ui_eid_t eid, int percentage, const char *value);

    /**
     * @brief Actualiza el porcentaje y el texto descriptivo de una barra bipolar.
     * @param eid Identificador único de la barra bipolar.
     * @param percentage Nuevo valor de porcentaje (-100 a 100).
     * @param value Nuevo texto complementario.
     */
    void (*updateBipolarProgressBar)(ui_eid_t eid, int percentage, const char *value);

    /**
     * @brief Agrega un indicador de estado booleano (ej: Conectado/Desconectado, Activo/Inactivo).
     * @param title Etiqueta descriptiva del estado (ej: "WiFi Status").
     * @param state Estado inicial del componente (true = Activo/OK, false = Inactivo/Error).
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addStateIndicator)(const char *title, bool state);

    /**
     * @brief Actualiza el estado booleano de un indicador.
     * @param eid Identificador único del componente de estado.
     * @param state Nuevo estado booleano (true o false).
     */
    void (*updateStateIndicator)(ui_eid_t eid, bool state);

    /**
     * @brief Agrega un espacio reservado en el Layout exclusivo para alertas críticas.
     * Por defecto se crea en estado inactivo (invisible/vacío).
     * @param text Texto base o mensaje de la alerta (ej: "HIGH TEMPERATURE").
     * @return ui_eid_t Identificador único asignado, o -1 en caso de error.
     */
    ui_eid_t (*addAlertIndicator)(const char *text);

    /**
     * @brief Dispara/Enciende de forma visual la alerta en la pantalla.
     * Suele aplicar estilos llamativos como parpadeo o colores de advertencia (rojo).
     * @param eid Identificador único del indicador de alerta.
     * @param text Mensaje específico que se desea mostrar durante la emergencia.
     */
    void (*triggerAlertIndicator)(ui_eid_t eid, const char *text);

    /**
     * @brief Apaga o limpia el indicador de alerta, regresando la zona a su estado
     * neutro invisible o vacío.
     * @param eid Identificador único del indicador de alerta.
     * @param text Mensaje específico que se desea mostrar después de limpiar.
     */
    void (*clearAlertIndicator)(ui_eid_t eid, const char *text);

} InterfaceModule;
