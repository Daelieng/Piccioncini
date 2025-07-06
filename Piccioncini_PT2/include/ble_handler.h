// ble_handler.h
#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include "common_variables.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Inizializza il modulo BLE
void ble_handler_init(UBaseType_t task_priority);

#endif // BLE_HANDLER_H