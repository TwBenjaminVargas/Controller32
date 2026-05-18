/**
 * @file ControllInterface.h
 * @brief Interfaz polimórfica de control por canales guiada por eventos.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Contenedor del evento empaquetado por el driver.
 */
typedef struct {
    int          cid;       // Canal identificado (ID único)
    const void   *data;     // Datos genéricos del periférico (un bool, un struct, etc.)
    size_t       length;    // Tamaño en bytes del payload recibido
} ControllEvent;

/**
 * @brief Módulo de interfaz agnóstica de hardware.
 */
typedef struct {
    /**
     * @brief Inicializa el administrador de colas e interrupciones básicas.
     */
    int  (*init)(void* args);

    /**
     * @brief Vincula un CID a un hardware específico (Ej: un pin GPIO o canal ADC).
     * @return 0 si se conectó con éxito, menor a 0 si hubo error o conflicto.
     */
    int  (*connect)(int cid, void* hw_config);

    /**
     * @brief Libera los recursos asociados a ese canal y apaga su interrupción.
     */
    int  (*disconnect)(int cid);
    
    /**
     * @brief Bloquea de forma eficiente (0% CPU) la tarea hasta recibir datos de cualquier canal.
     * @param event Puntero donde se volcará el último evento de la cola.
     * @param timeout_ms Tiempo máximo de espera en milisegundos antes de abortar (0 para esperar indefinidamente).
     * @return true si se obtuvo un evento válido, false si ocurrió un timeout.
     */
    bool (*waitEvent)(ControllEvent *event, uint32_t timeout_ms);
} ControllModule;