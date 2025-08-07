
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#define WIFI_SSID      "ESP32_Net_987A"
#define WIFI_PASS      "Conexión$2025!"
#define TAG            "ESP32_APP"

typedef enum {
    STATE_IDLE,
    STATE_WIFI_CONNECTING,
    STATE_WIFI_CONNECTED,
    STATE_MQTT_CONNECTING,
    STATE_MQTT_CONNECTED,
    STATE_PUBLISHING,
    STATE_ERROR
} app_state_t;

static app_state_t current_state = STATE_IDLE;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static TimerHandle_t timer_handle;

static void wifi_init_sta(void);
static void mqtt_app_start(void);
static void timer_callback(TimerHandle_t xTimer);

void app_state_machine(void) {
    static int publish_counter = 0;

    switch (current_state) {
        case STATE_IDLE:
            ESP_LOGI(TAG, "Estado: IDLE -> WIFI_CONNECTING");
            wifi_init_sta();
            current_state = STATE_WIFI_CONNECTING;
            break;

        case STATE_WIFI_CONNECTING:
            // Esperando evento de conexión WiFi
            break;

        case STATE_WIFI_CONNECTED:
            ESP_LOGI(TAG, "Estado: WIFI_CONNECTED -> MQTT_CONNECTING");
            mqtt_app_start();
            current_state = STATE_MQTT_CONNECTING;
            break;

        case STATE_MQTT_CONNECTING:
            // Esperando conexión MQTT
            break;

        case STATE_MQTT_CONNECTED:
            ESP_LOGI(TAG, "Estado: MQTT_CONNECTED -> PUBLISHING");
            current_state = STATE_PUBLISHING;
            break;

        case STATE_PUBLISHING:
            publish_counter++;
            char msg[64];
            snprintf(msg, sizeof(msg), "Mensaje #%d desde ESP32", publish_counter);
            esp_mqtt_client_publish(mqtt_client, "/demo/topic", msg, 0, 1, 0);
            ESP_LOGI(TAG, "Publicado: %s", msg);
            break;

        case STATE_ERROR:
            ESP_LOGE(TAG, "Estado: ERROR");
            break;
    }
}

static void timer_callback(TimerHandle_t xTimer) {
    app_state_machine();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi conectado con IP");
        current_state = STATE_WIFI_CONNECTED;
    }
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado");
            current_state = STATE_MQTT_CONNECTED;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT desconectado");
            current_state = STATE_ERROR;
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .credentials.client_id = "esp32_client",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    current_state = STATE_IDLE;

    timer_handle = xTimerCreate("app_timer", pdMS_TO_TICKS(50), pdTRUE, NULL, timer_callback);
    if (timer_handle != NULL) {
        xTimerStart(timer_handle, 0);
    }
}
