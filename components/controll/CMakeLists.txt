idf_component_register(
    SRCS 
        "JoystickInput.c"
    INCLUDE_DIRS 
        "." 
    REQUIRES
        driver      # Para GPIO y periféricos básicos
        esp_adc     # El nuevo componente oficial para el ADC moderno
        log
        esp_timer
)