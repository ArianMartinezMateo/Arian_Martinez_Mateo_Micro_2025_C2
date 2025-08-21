
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

// ===================== USER CONFIG =====================
#define WIFI_SSID      "ESP32_Net_987A"
#define WIFI_PASS      "Conexión$2025!"
#define MQTT_URI       "mqtt://broker.hivemq.com"
#define CLIENT_ID      "esp32_door_fsm_01"
#define MQTT_BASE      "esp32/door"   // esp32/door/cmd/* , esp32/door/state
#define TAG            "ESP32_DOOR"

// Tick de la FSM
#define TICK_MS        100

// ===================== PINOUT (según esquema) =====================
// Entradas (pull-up, activas en 0)
#define PIN_LSA      GPIO_NUM_15   // Límite Apertura
#define PIN_LSC      GPIO_NUM_4    // Límite Cierre
#define PIN_FTC      GPIO_NUM_16   // Falla térmica / emergencia
#define PIN_KEYA     GPIO_NUM_17   // Botón Abrir
#define PIN_KEYC     GPIO_NUM_18   // Botón Cerrar
#define PIN_PP       GPIO_NUM_19   // Pulsador paso a paso / start

// Salidas
#define PIN_MA       GPIO_NUM_22   // Puente H IN1
#define PIN_MC       GPIO_NUM_23   // Puente H IN2
#define PIN_LAMP     GPIO_NUM_21   // Lámpara
#define PIN_BUZZ     GPIO_NUM_27   // Buzzer (LED en protoboard)

// ===================== ESTADOS =====================
typedef enum {
    ESTADO_INICIAL = 0,
    ESTADO_CERRANDO,
    ESTADO_ABRIENDO,
    ESTADO_CERRADO,
    ESTADO_ABIERTO,
    ESTADO_ERR,
    ESTADO_STOP
} estado_t;

static estado_t estado_siguiente = ESTADO_INICIAL;
static estado_t estado_actual    = ESTADO_INICIAL;
static estado_t estado_anterior  = ESTADO_INICIAL;

// Timers SW (incrementados cada TICK_MS)
static TimerHandle_t tick_timer = NULL;
static uint32_t cntTimerCA   = 0;   // tiempo en ABIERTO para autocierre
static uint32_t cntRunTimer  = 0;   // watchdog de movimiento
static uint32_t tiempo_ms    = 0;   // tiempo global

// Config
static uint32_t RunTimer_ticks = 180;   // ~18s si TICK=100ms
static uint32_t TimerCA_ticks  = 100;   // ~10s si TICK=100ms

// Debounce simple
typedef struct { gpio_num_t pin; uint8_t thresh; uint8_t stable; int level; int last; } deb_t;
static deb_t d_lsa  = {.pin = PIN_LSA,  .thresh=3};
static deb_t d_lsc  = {.pin = PIN_LSC,  .thresh=3};
static deb_t d_keya = {.pin = PIN_KEYA, .thresh=3};
static deb_t d_keyc = {.pin = PIN_KEYC, .thresh=3};
static deb_t d_pp   = {.pin = PIN_PP,   .thresh=3};
static deb_t d_ftc  = {.pin = PIN_FTC,  .thresh=3};

static esp_mqtt_client_handle_t mqtt = NULL;

// ===================== Helpers IO =====================
static inline void motor_stop(void){ gpio_set_level(PIN_MA, 0); gpio_set_level(PIN_MC, 0); }
static inline void motor_open(void){ gpio_set_level(PIN_MA, 1); gpio_set_level(PIN_MC, 0); }
static inline void motor_close(void){ gpio_set_level(PIN_MA, 0); gpio_set_level(PIN_MC, 1); }

static void deb_update(deb_t* d){
    int raw = gpio_get_level(d->pin);
    if (raw == d->last){
        if (d->stable < d->thresh) d->stable++;
        if (d->stable == d->thresh) d->level = raw;
    } else {
        d->stable = 0;
        d->last = raw;
    }
}

// ===================== MQTT =====================
static void mqtt_pub_state(const char* reason){
    if (!mqtt) return;
    const char* s = "INICIAL";
    switch(estado_actual){
        case ESTADO_CERRANDO: s="CERRANDO"; break;
        case ESTADO_ABRIENDO: s="ABRIENDO"; break;
        case ESTADO_CERRADO:  s="CERRADO"; break;
        case ESTADO_ABIERTO:  s="ABIERTO"; break;
        case ESTADO_ERR:      s="ERR"; break;
        case ESTADO_STOP:     s="STOP"; break;
        default: break;
    }
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"state\":\"%s\",\"lsa\":%d,\"lsc\":%d,\"keya\":%d,\"keyc\":%d,"
        "\"pp\":%d,\"ftc\":%d,\"lamp\":%d,\"ma\":%d,\"mc\":%d,\"reason\":\"%s\"}",
        s,
        d_lsa.level, d_lsc.level, d_keya.level, d_keyc.level, d_pp.level, d_ftc.level,
        gpio_get_level(PIN_LAMP), gpio_get_level(PIN_MA), gpio_get_level(PIN_MC),
        reason?reason:"");
    esp_mqtt_client_publish(mqtt, MQTT_BASE"/state", payload, 0, 1, 0);
    ESP_LOGI(TAG, "PUB state: %s", payload);
}

static void handle_mqtt_cmd(const char* topic, const char* data){
    // Topics: esp32/door/cmd/open|close|stop|reset|lamp_on|lamp_off|status
    if (strstr(topic, "/cmd/open")){
        estado_siguiente = ESTADO_ABRIENDO; mqtt_pub_state("mqtt_cmd_open");
    } else if (strstr(topic, "/cmd/close")){
        estado_siguiente = ESTADO_CERRANDO; mqtt_pub_state("mqtt_cmd_close");
    } else if (strstr(topic, "/cmd/stop")){
        estado_siguiente = ESTADO_STOP; motor_stop(); mqtt_pub_state("mqtt_cmd_stop");
    } else if (strstr(topic, "/cmd/reset")){
        estado_siguiente = ESTADO_INICIAL; mqtt_pub_state("mqtt_cmd_reset");
    } else if (strstr(topic, "/cmd/lamp_on")){
        gpio_set_level(PIN_LAMP, 1); mqtt_pub_state("mqtt_cmd_lamp_on");
    } else if (strstr(topic, "/cmd/lamp_off")){
        gpio_set_level(PIN_LAMP, 0); mqtt_pub_state("mqtt_cmd_lamp_off");
    } else if (strstr(topic, "/cmd/status")){
        mqtt_pub_state("mqtt_cmd_status");
    } else if (strstr(topic, "/cmd/set/RunTimer")){
        int v = atoi(data);
        if (v>0 && v<60000) RunTimer_ticks = v / TICK_MS; // ms -> ticks
        mqtt_pub_state("set_RunTimer");
    } else if (strstr(topic, "/cmd/set/TimerCA")){
        int v = atoi(data);
        if (v>0 && v<60000) TimerCA_ticks = v / TICK_MS;
        mqtt_pub_state("set_TimerCA");
    }
}

static esp_err_t mqtt_handler(esp_mqtt_event_handle_t e){
    switch(e->event_id){
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado");
            esp_mqtt_client_subscribe(e->client, MQTT_BASE"/cmd/#", 1);
            mqtt_pub_state("connected");
            break;
        case MQTT_EVENT_DATA:{
            char topic[128]={0}; char data[128]={0};
            memcpy(topic, e->topic, e->topic_len);
            memcpy(data, e->data, e->data_len);
            handle_mqtt_cmd(topic, data);
            break;
        }
        default: break;
    }
    return ESP_OK;
}

// ===================== WiFi =====================
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data){
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        ESP_LOGI(TAG, "WiFi conectado, IP obtenida");
        esp_mqtt_client_config_t cfg = {
            .broker.address.uri = MQTT_URI,
            .session.protocol_ver = MQTT_PROTOCOL_V_5,
            .credentials.client_id = CLIENT_ID,
        };
        mqtt = esp_mqtt_client_init(&cfg);
        esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_handler, NULL);
        esp_mqtt_client_start(mqtt);
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        ESP_LOGW(TAG, "WiFi desconectado, reintento...");
        esp_wifi_connect();
    }
}

static void wifi_init(void){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t sta = {.sta = {.ssid = WIFI_SSID, .password = WIFI_PASS}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ===================== LÁMPARA/Buzzer =====================
#define TIEMPO_LAMPARA_1 500
#define TIEMPO_LAMPARA_2 250

static void actualizar_lampara(estado_t e){
    // tiempos en ms, usamos tiempo_ms
    switch(e){
        case ESTADO_ABRIENDO:
            gpio_set_level(PIN_LAMP, (tiempo_ms % 1000) < TIEMPO_LAMPARA_1);
            break;
        case ESTADO_CERRANDO:
            gpio_set_level(PIN_LAMP, (tiempo_ms % 500) < TIEMPO_LAMPARA_2);
            break;
        case ESTADO_ABIERTO:
            gpio_set_level(PIN_LAMP, 1);
            break;
        default:
            // en STOP/ERR INICIAL/CERRADO: controlado abajo
            break;
    }
}

// ===================== FSM =====================
static estado_t do_ESTADO_INICIAL(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_INICIAL;
    mqtt_pub_state("INICIAL");
    // lectura actualizada antes del switch
    if (d_lsc.level==1 && d_lsa.level==1) return ESTADO_ERR;     // ambos activos
    if (d_lsc.level==1 && d_lsa.level==0) return ESTADO_CERRADO; // CERRADO
    if (d_lsc.level==0 && d_lsa.level==1) return ESTADO_ABIERTO; // ABIERTO
    return ESTADO_STOP;
}

static estado_t do_ESTADO_CERRANDO(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_CERRANDO;
    cntRunTimer = 0;
    motor_close();
    gpio_set_level(PIN_BUZZ, 0);
    mqtt_pub_state("CERRANDO");

    // Transiciones
    if (d_lsc.level==1) return ESTADO_CERRADO;              // llegó al cierre
    if (d_keyc.level==0 || d_keya.level==0 || d_ftc.level==0) return ESTADO_STOP;  // botón o falla
    if (d_pp.level==0) return ESTADO_ABRIENDO;              // cambio sentido por PP
    if (cntRunTimer > RunTimer_ticks) return ESTADO_ERR;    // timeout
    return ESTADO_CERRANDO;
}

static estado_t do_ESTADO_ABRIENDO(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_ABRIENDO;
    cntRunTimer = 0;
    motor_open();
    gpio_set_level(PIN_BUZZ, 0);
    mqtt_pub_state("ABRIENDO");

    if (d_lsa.level==1) return ESTADO_ABIERTO;              // llegó a apertura
    if (d_keya.level==0 || d_keyc.level==0 || d_ftc.level==0) return ESTADO_STOP;
    if (d_pp.level==0) return ESTADO_CERRANDO;
    if (cntRunTimer > RunTimer_ticks) return ESTADO_ERR;
    return ESTADO_ABRIENDO;
}

static estado_t do_ESTADO_CERRADO(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_CERRADO;
    motor_stop();
    gpio_set_level(PIN_BUZZ, 0);
    mqtt_pub_state("CERRADO");

    if (d_keya.level==0 || d_pp.level==0) return ESTADO_ABRIENDO;
    return ESTADO_CERRADO;
}

static estado_t do_ESTADO_ABIERTO(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_ABIERTO;
    motor_stop();
    gpio_set_level(PIN_BUZZ, 0);
    mqtt_pub_state("ABIERTO");

    if (cntTimerCA > TimerCA_ticks || d_pp.level==0) return ESTADO_CERRANDO;
    return ESTADO_ABIERTO;
}

static estado_t do_ESTADO_ERR(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_ERR;
    motor_stop();
    // buzzer intermitente
    gpio_set_level(PIN_BUZZ, (tiempo_ms/200)%2);
    mqtt_pub_state("ERROR");

    if (d_lsc.level==0 && d_lsa.level==0) return ESTADO_STOP;
    return ESTADO_ERR;
}

static estado_t do_ESTADO_STOP(void){
    estado_anterior = estado_actual; estado_actual = ESTADO_STOP;
    motor_stop();
    gpio_set_level(PIN_BUZZ, 0);
    mqtt_pub_state("STOP");

    if (d_ftc.level==1) return ESTADO_INICIAL; // libera falla
    return ESTADO_STOP;
}

// ===================== Timer Tick =====================
static void on_tick(TimerHandle_t xTimer){
    // actualizar contadores
    tiempo_ms += TICK_MS;
    cntRunTimer++;
    cntTimerCA++;

    // actualizar entradas (pull-up -> activo en 0)
    deb_update(&d_lsa);
    deb_update(&d_lsc);
    deb_update(&d_keya);
    deb_update(&d_keyc);
    deb_update(&d_pp);
    deb_update(&d_ftc);

    // FSM
    switch(estado_siguiente){
        case ESTADO_INICIAL:  estado_siguiente = do_ESTADO_INICIAL(); break;
        case ESTADO_CERRANDO: estado_siguiente = do_ESTADO_CERRANDO(); break;
        case ESTADO_ABRIENDO: estado_siguiente = do_ESTADO_ABRIENDO(); break;
        case ESTADO_CERRADO:  estado_siguiente = do_ESTADO_CERRADO(); break;
        case ESTADO_ABIERTO:  estado_siguiente = do_ESTADO_ABIERTO(); break;
        case ESTADO_ERR:      estado_siguiente = do_ESTADO_ERR(); break;
        case ESTADO_STOP:     estado_siguiente = do_ESTADO_STOP(); break;
        default: estado_siguiente = ESTADO_INICIAL; break;
    }

    // Lámpara según estado
    actualizar_lampara(estado_actual);
    if (estado_actual==ESTADO_CERRADO || estado_actual==ESTADO_INICIAL || estado_actual==ESTADO_STOP){
        // Apagada en estos estados por defecto
        if (estado_actual!=ESTADO_STOP) gpio_set_level(PIN_LAMP, 0);
    }
    if (estado_actual==ESTADO_ERR){
        // parpadeo rápido de buzzer ya gestionado, lámpara apagada
        gpio_set_level(PIN_LAMP, 0);
    }

    // Publicación periódica simple cada 1s
    static uint32_t hb = 0;
    hb += TICK_MS;
    if (hb >= 1000){
        hb = 0;
        mqtt_pub_state("periodic");
    }
}

// ===================== IO init =====================
static void io_init(void){
    // Salidas
    gpio_config_t out = {
        .pin_bit_mask = (1ULL<<PIN_MA) | (1ULL<<PIN_MC) | (1ULL<<PIN_LAMP) | (1ULL<<PIN_BUZZ),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out);
    motor_stop(); gpio_set_level(PIN_LAMP, 0); gpio_set_level(PIN_BUZZ, 0);

    // Entradas con pull-up
    gpio_config_t in = {
        .pin_bit_mask = (1ULL<<PIN_LSA) | (1ULL<<PIN_LSC) | (1ULL<<PIN_KEYA) |
                        (1ULL<<PIN_KEYC) | (1ULL<<PIN_PP) | (1ULL<<PIN_FTC),
        .mode = GPIO_MODE_INPUT, .pull_up_en = 1, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&in);

    // init debounce levels
    d_lsa.level = d_lsa.last = gpio_get_level(PIN_LSA);
    d_lsc.level = d_lsc.last = gpio_get_level(PIN_LSC);
    d_keya.level = d_keya.last = gpio_get_level(PIN_KEYA);
    d_keyc.level = d_keyc.last = gpio_get_level(PIN_KEYC);
    d_pp.level = d_pp.last = gpio_get_level(PIN_PP);
    d_ftc.level = d_ftc.last = gpio_get_level(PIN_FTC);
}

// ===================== APP MAIN =====================
void app_main(void){
    ESP_ERROR_CHECK(nvs_flash_init());
    io_init();
    wifi_init();

    estado_siguiente = ESTADO_INICIAL;
    tick_timer = xTimerCreate("tick", pdMS_TO_TICKS(TICK_MS), pdTRUE, NULL, on_tick);
    xTimerStart(tick_timer, 0);
}
