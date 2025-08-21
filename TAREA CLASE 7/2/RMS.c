#include <stdio.h>
#include <math.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_timer.h" // Muestreo ADC de alta resolución

#define LED1_PIN  2
#define LED_R_PIN 33
#define LED_G_PIN 25
#define LED_B_PIN 26

#define SAMPLE_PERIOD_US 416   // 1s / 2400 ≈ 416 µs
#define SAMPLE_COUNT     2400
#define VREF             3.3

static const char *TAG = "Main";

// Variables de estado
static uint8_t led_state = 0;
static int samples[SAMPLE_COUNT];
static int sample_index = 0;
static bool buffer_full = false;
static int adc_val = 0;
static esp_timer_handle_t adc_timer;

// Prototipos
static esp_err_t init_leds(void);
static inline void blink_led(void);
static esp_err_t set_adc(void);
static esp_err_t start_highres_timer(void);

// Callback de muestreo
static void adc_timer_callback(void *arg)
{
    blink_led();
    adc_val = adc1_get_raw(ADC1_CHANNEL_4);

    samples[sample_index++] = adc_val;

    if (sample_index >= SAMPLE_COUNT) {
        buffer_full = true;
        sample_index = 0;
    }

    if (buffer_full) {
        double sum_sq = 0.0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            double voltage = (samples[i] / 4095.0) * VREF;
            sum_sq += voltage * voltage;
        }
        double rms = sqrt(sum_sq / SAMPLE_COUNT);
        ESP_LOGI(TAG, "RMS Voltage: %.2f V", rms);
        buffer_full = false;
    }

    ESP_LOGD(TAG, "ADC Value: %d", adc_val);

    switch (adc_val / 1000) {
        case 0:
            gpio_set_level(LED_R_PIN, 0);
            gpio_set_level(LED_G_PIN, 0);
            gpio_set_level(LED_B_PIN, 0);
            break;
        case 1:
            gpio_set_level(LED_R_PIN, 1);
            gpio_set_level(LED_G_PIN, 0);
            gpio_set_level(LED_B_PIN, 0);
            break;
        case 2:
            gpio_set_level(LED_R_PIN, 1);
            gpio_set_level(LED_G_PIN, 1);
            gpio_set_level(LED_B_PIN, 0);
            break;
        case 3:
        case 4:
            gpio_set_level(LED_R_PIN, 1);
            gpio_set_level(LED_G_PIN, 1);
            gpio_set_level(LED_B_PIN, 1);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_leds());
    ESP_ERROR_CHECK(set_adc());
    ESP_ERROR_CHECK(start_highres_timer());

    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Programa iniciado correctamente");
}

// Inicializa LEDs
static esp_err_t init_leds(void)
{
    int pins[] = {LED1_PIN, LED_R_PIN, LED_G_PIN, LED_B_PIN};
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
    }
    return ESP_OK;
}

// Parpadeo de LED indicador
static inline void blink_led(void)
{
    led_state = !led_state;
    gpio_set_level(LED1_PIN, led_state);
}

// Configuración del ADC
static esp_err_t set_adc(void)
{
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_width(ADC_WIDTH_BIT_12);
    return ESP_OK;
}

// Configura y arranca el timer de alta resolución
static esp_err_t start_highres_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &adc_timer_callback,
        .name = "adc_sample_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &adc_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(adc_timer, SAMPLE_PERIOD_US));

    ESP_LOGI(TAG, "High-res ADC timer iniciado (416 us)");
    return ESP_OK;
}
