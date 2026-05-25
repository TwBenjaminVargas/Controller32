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
    size_t       length;    // Tamaño en bytes del payload recibido
} ControllEvent;

/**
 * @brief Módulo de interfaz agnóstica de hardware.
 */
typedef struct {
    /**
     * @brief Inicializa el administrador de colas, tareas e interrupciones.
     */
    int  (*init)(void* args);

    /**
     * @brief Libera los recursos asociados a un canal.
     */
    int  (*disconnect)(int cid);
    
    /**
     * @brief Obtiene un arreglo con los IDs de los canales configurados.
     */
    const int* (*get_availables_ids)(void);

    /**
     * @brief Obtiene la cantidad de canales disponibles.
     */
    size_t (*get_availables_id_len)(void);

    /**
     * @brief Bloquea de forma eficiente (0% CPU) la tarea hasta recibir datos.
     * @param event Puntero donde se volcará el último evento de la cola.
     * @param buffer Puntero al buffer donde se almacenarán los datos del evento.
     * @param timeout_ms Tiempo en milisegundos (0 para esperar indefinidamente).
     * @return true si se obtuvo un evento válido, false si ocurrió un timeout.
     */
    bool (*waitEvent)(ControllEvent *event,void* buffer,uint32_t timeout_ms);
} ControllModule;