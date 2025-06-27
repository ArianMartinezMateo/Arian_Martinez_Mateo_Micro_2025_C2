
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_GPIO GPIO_NUM_2
#define TIMER_PERIOD_MS 1000

TimerHandle_t timer_handle;
uint8_t led_state = 0;
static const char *TAG = "TIMER";

void timer_callback(TimerHandle_t xTimer) {
    led_state = !led_state;
    gpio_set_level(LED_GPIO, led_state);
    ESP_LOGI(TAG, "Timer callback: LED = %d", led_state);
}

void app_main(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    timer_handle = xTimerCreate("BlinkTimer",
                                pdMS_TO_TICKS(TIMER_PERIOD_MS),
                                pdTRUE,
                                NULL,
                                timer_callback);

    if (timer_handle == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el timer");
        return;
    }

    if (xTimerStart(timer_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo iniciar el timer");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
