/**
 * @file CommunicationInterface.h
 * @brief Contrato genérico y agnóstico para módulos de comunicación (ESP-IDF)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Estructura que define el contrato de comunicación genérico.
 */
typedef struct {
    
    /**
     * @brief Inicializa el hardware y stacks internos (Wi-Fi, Bluetooth, etc).
     * @param args Puntero genérico para configuraciones específicas (ej: Estructura con SSID/Pass o MAC destino).
     */
    int (*init)(void *args);

    /**
     * @brief Inicia el proceso de conexión activa (esencial para MQTT o Bluetooth SPP).
     * @return int 0 si la conexión fue exitosa o iniciada, menor a 0 en caso de error.
     */
    int (*connect)(void);

    /**
     * @brief Corta la conexión de forma segura y libera canales.
     */
    void (*disconnect)(void);

    /**
     * @brief Consulta el estado actual de la conexión.
     * @return true si el canal está operativo y listo para transmitir, false si está caído.
     */
    bool (*isConnected)(void);


    /**
     * @brief Envía un bloque de datos crudo a través del canal activo.
     * @param payload Puntero al buffer de datos (struct, array, string, etc).
     * @param length Tamaño en bytes del payload a transmitir.
     * @return int Cantidad de bytes enviados con éxito, menor a 0 en caso de error.
     */
    int (*send)(const void *payload, size_t length);

    /**
     * @brief Lee datos del canal mediante Polling activo (Bloqueante).
     * @param buffer Puntero donde se almacenarán los datos recibidos.
     * @param max_length Capacidad máxima del buffer para evitar desbordamientos.
     * @param timeout_ms Tiempo máximo de espera en milisegundos antes de abortar.
     * @return int Cantidad de bytes leídos, 0 si hubo timeout, menor a 0 si hubo error.
     */
    int (*receive)(void *buffer, size_t max_length, uint32_t timeout_ms);



    /**
     * @brief Registra una función de interrupción (Callback) para recepción asincrónica.
     * @param callback Puntero a la función de tu aplicación que procesará los datos entrantes.
     */
    void (*onReceive)(void (*callback)(const void *payload, size_t length));

    /**
     * @brief Registra una función que se ejecutará automáticamente al finalizar un envío.
     * @param callback Puntero a la función que recibirá el estado del envío (true = Éxito / ACK).
     */
    void (*onSend)(void (*callback)(bool success));



    /**
     * @brief Devuelve la calidad actual del enlace (RSSI mapeado o porcentaje de paquetes).
     * @return int Calidad de la comunicación en un rango de 0 (Sin señal) a 100 (Excelente).
     */
    int (*comunicationQuality)(void);

} CommunicationModule;

