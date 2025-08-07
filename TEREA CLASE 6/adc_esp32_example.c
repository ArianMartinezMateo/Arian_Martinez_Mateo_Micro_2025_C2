
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_log.h"

#define TAG "ADC_EXAMPLE"

void app_main() {
    // Configurar el ADC1 canal 0 (GPIO36)
    adc1_config_width(ADC_WIDTH_BIT_12);  // Resolución de 12 bits (0-4095)
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11); // Atenuación de 11dB (~0 - 3.3V)

    while (1) {
        int raw_value = adc1_get_raw(ADC1_CHANNEL_0);
        float voltage = (raw_value * 3.3) / 4095.0; // Conversión a voltaje (aproximado)

        ESP_LOGI(TAG, "Valor crudo: %d | Voltaje: %.2f V", raw_value, voltage);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
    }
}
