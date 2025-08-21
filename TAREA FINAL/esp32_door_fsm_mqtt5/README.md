
# ESP32 Puerta – FSM + MQTT v5 + WiFi Station + Timer 100 ms

## Pinout (según tu esquemático)
- **LSA (limit apertura)** → GPIO15 (entrada, pull-up, activo en 0)
- **LSC (limit cierre)** → GPIO4  (entrada, pull-up, activo en 0)
- **FTC (falla térmica/emergencia)** → GPIO16 (entrada, pull-up, activo en 0)
- **KEYA (abrir)** → GPIO17 (entrada, pull-up, activo en 0)
- **KEYC (cerrar)** → GPIO18 (entrada, pull-up, activo en 0)
- **PP (paso a paso/start)** → GPIO19 (entrada, pull-up, activo en 0)

- **MA (H-Bridge IN1)** → GPIO22 (salida)
- **MC (H-Bridge IN2)** → GPIO23 (salida)
- **LAMPARA** → GPIO21 (salida)
- **BUZZER (LED)** → GPIO27 (salida)  ← (reubicado para no chocar con PP)

**Nota:** si usas L298N/L293D, conecta ENA habilitado (jumper a 5V) o a un PWM si quieres control de velocidad (no implementado aquí). Fuente del motor separada y **GND común** con el ESP32.

## MQTT
Base: `esp32/door/*`

### Comandos (suscripción)
- `esp32/door/cmd/open` – abre (giro MA=1, MC=0)
- `esp32/door/cmd/close` – cierra (MA=0, MC=1)
- `esp32/door/cmd/stop` – parar (MA=0, MC=0)
- `esp32/door/cmd/reset` – volver a INICIAL
- `esp32/door/cmd/lamp_on`, `esp32/door/cmd/lamp_off`
- `esp32/door/cmd/status` – fuerza publicación inmediata
- `esp32/door/cmd/set/RunTimer` – payload en ms (watchdog de movimiento)
- `esp32/door/cmd/set/TimerCA` – payload en ms (tiempo para autocierre en ABIERTO)

### Estado (publicación)
- `esp32/door/state` → JSON con `state`, entradas, salidas y `reason`

## Máquina de estados
Estados: `INICIAL`, `CERRANDO`, `ABRIENDO`, `CERRADO`, `ABIERTO`, `ERR`, `STOP`  
- **ERR**: motor off, buzzer parpadea. Sale a `STOP` cuando no hay límites activos simultáneamente.
- **STOP**: motor off; vuelve a `INICIAL` cuando se libera `FTC`.
- **ABIERTO**: autocierre cuando `TimerCA` expira.
- Lámpara: parpadeo lento (abriendo), rápido (cerrando), fija en abierto.

## Compilar y flashear
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Credenciales WiFi/MQTT
En `main.c`:
```c
#define WIFI_SSID "ESP32_Net_987A"
#define WIFI_PASS "Conexión$2025!"
#define MQTT_URI  "mqtt://broker.hivemq.com"
```
