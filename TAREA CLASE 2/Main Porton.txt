#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define ESTADO_INICIAL 0
#define ESTADO_CERRANDO 1
#define ESTADO_ABRIENDO 2
#define ESTADO_CERRADO 3
#define ESTADO_ABIERTO 4
#define ESTADO_ERR 5
#define ESTADO_STOP 6

#define TIEMPO_LAMPARA_1 500 // en ms
#define TIEMPO_LAMPARA_2 250

int Func_ESTADO_INICIAL(void);
int Func_ESTADO_CERRANDO(void);
int Func_ESTADO_ABRIENDO(void);
int Func_ESTADO_CERRADO(void);
int Func_ESTADO_ABIERTO(void);
int Func_ESTADO_ERR(void);
int Func_ESTADO_STOP(void);

int EstadoSiguiente = ESTADO_INICIAL;
int EstadoActual = ESTADO_INICIAL;
int EstadoAnterior = ESTADO_INICIAL;

struct IO
{
    unsigned int LSC:1;
    unsigned int LSA:1;
    unsigned int BA:1;
    unsigned int BC:1;
    unsigned int SE:1;
    unsigned int PP:1;
    unsigned int MA:1;
    unsigned int MC:1;
    unsigned int BUZZER:1;
    unsigned int LAMP:1;
} io;

struct STATUS
{
    unsigned int cntTimerCA;
    unsigned int cntRunTimer;
    unsigned int tiempo;
} status;

struct CONFIG
{
    unsigned int RunTimer;
    unsigned int TimerCA;
} config = {180, 100};

void Timercallback(void)
{
    status.cntRunTimer++;
    status.cntTimerCA++;
    status.tiempo++;
}

void mqttPublishEstado(const char* estado)
{
    printf("[MQTT] Estado: %s\n", estado);
}

void manejarLampara(int estado)
{
    for (;;) {
        switch (estado) {
            case ESTADO_ABRIENDO:
                io.LAMP = (status.tiempo % 1000) < TIEMPO_LAMPARA_1;
                break;
            case ESTADO_CERRANDO:
                io.LAMP = (status.tiempo % 500) < TIEMPO_LAMPARA_2;
                break;
            case ESTADO_ABIERTO:
                io.LAMP = true;
                break;
            default:
                io.LAMP = false;
        }
        break;
    }
}

int main()
{
    for (;;) {
        switch (EstadoSiguiente) {
            case ESTADO_INICIAL:
                EstadoSiguiente = Func_ESTADO_INICIAL(); break;
            case ESTADO_ABIERTO:
                EstadoSiguiente = Func_ESTADO_ABIERTO(); break;
            case ESTADO_ABRIENDO:
                EstadoSiguiente = Func_ESTADO_ABRIENDO(); break;
            case ESTADO_CERRADO:
                EstadoSiguiente = Func_ESTADO_CERRADO(); break;
            case ESTADO_CERRANDO:
                EstadoSiguiente = Func_ESTADO_CERRANDO(); break;
            case ESTADO_ERR:
                EstadoSiguiente = Func_ESTADO_ERR(); break;
            case ESTADO_STOP:
                EstadoSiguiente = Func_ESTADO_STOP(); break;
        }
    }
    return 0;
}

int Func_ESTADO_INICIAL(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_INICIAL;
    mqttPublishEstado("INICIAL");

    if (io.LSC && io.LSA) return ESTADO_ERR;
    if (io.LSC && !io.LSA) return ESTADO_CERRADO;
    if (!io.LSC && io.LSA) return ESTADO_CERRANDO;
    if (!io.LSC && !io.LSA) return ESTADO_STOP;
    return ESTADO_ERR;
}

int Func_ESTADO_CERRANDO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRANDO;
    status.cntRunTimer = 0;
    io.MA = 0;
    io.MC = 1;
    io.BA = 0;
    io.BC = 0;
    mqttPublishEstado("CERRANDO");

    for (;;) {
        manejarLampara(ESTADO_CERRANDO);
        if (io.LSC) return ESTADO_CERRADO;
        if (io.BC || io.BA || io.SE) return ESTADO_STOP;
        if (io.PP) return ESTADO_ABRIENDO;
        if (status.cntRunTimer > config.RunTimer) return ESTADO_ERR;
        break;
    }
    return ESTADO_CERRANDO;
}

int Func_ESTADO_ABRIENDO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABRIENDO;
    status.cntRunTimer = 0;
    io.MA = 1;
    io.MC = 0;
    io.BA = 0;
    io.BC = 0;
    mqttPublishEstado("ABRIENDO");

    for (;;) {
        manejarLampara(ESTADO_ABRIENDO);
        if (io.LSA) return ESTADO_ABIERTO;
        if (io.BA || io.BC || io.SE) return ESTADO_STOP;
        if (io.PP) return ESTADO_CERRANDO;
        if (status.cntRunTimer > config.RunTimer) return ESTADO_ERR;
        break;
    }
    return ESTADO_ABRIENDO;
}

int Func_ESTADO_CERRADO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRADO;
    io.MA = 0;
    io.MC = 0;
    io.BA = 0;
    mqttPublishEstado("CERRADO");

    for (;;) {
        manejarLampara(ESTADO_CERRADO);
        if (io.BA || io.PP) return ESTADO_ABRIENDO;
        break;
    }
    return ESTADO_CERRADO;
}

int Func_ESTADO_ABIERTO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABIERTO;
    io.MA = 0;
    io.MC = 0;
    mqttPublishEstado("ABIERTO");

    for (;;) {
        manejarLampara(ESTADO_ABIERTO);
        if (status.cntTimerCA > config.TimerCA || io.PP) return ESTADO_CERRANDO;
        break;
    }
    return ESTADO_ABIERTO;
}

int Func_ESTADO_ERR(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ERR;
    io.MA = 0;
    io.MC = 0;
    io.BUZZER = 1;
    mqttPublishEstado("ERROR");

    for (;;) {
        manejarLampara(ESTADO_ERR);
        if (!io.LSC && !io.LSA) return ESTADO_STOP;
        break;
    }
    return ESTADO_ERR;
}

int Func_ESTADO_STOP(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_STOP;
    io.MA = 0;
    io.MC = 0;
    io.BUZZER = 0;
    mqttPublishEstado("STOP");

    for (;;) {
        manejarLampara(ESTADO_STOP);
        if (!io.SE) return ESTADO_INICIAL;
        break;
    }
    return ESTADO_STOP;
}
