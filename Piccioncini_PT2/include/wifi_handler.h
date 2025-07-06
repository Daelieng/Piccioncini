// wifi_handler.h
#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include "common_variables.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Inizializza il modulo WiFi in modalità Station
void wifi_init_sta(UBaseType_t task_priority);

#endif // WIFI_HANDLER_H