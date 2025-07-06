// common_variables.h 
#ifndef COMMON_VARIABLES_H 
#define COMMON_VARIABLES_H 

// Include standard libraries prima di tutto
#include <stdbool.h> 
#include <stdint.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// — flag globale che indica se siamo connessi al Wi‑Fi — 
extern bool g_wifi_connected; 

// — le due queue condivise fra BLE e Wi‑Fi — 
// IMPORTANTE: usa "extern" per DICHIARARE le variabili nel .h
extern QueueHandle_t ble_to_wifi_q;
extern QueueHandle_t wifi_to_ble_q;

typedef enum { 
    BLE_WIFI_EVT_BTN_PRESS,  // Start Wi‑Fi scan 
    BLE_WIFI_EVT_CONNECT     // Connect to given SSID/PASSWORD 
} ble_wifi_evt_type_t; 
 
// — Payload for BLE → Wi‑Fi events — 
typedef struct { 
    ble_wifi_evt_type_t type; 
    char ssid[32]; 
    char password[64]; 
} ble_wifi_evt_t; 

typedef enum { 
    WIFI_BLE_EVT_SCAN_DONE, 
    WIFI_BLE_EVT_CONNECT_STATUS 
} wifi_ble_evt_type_t; 
 
#define MAX_WIFI_SCAN_RESULTS 10 
 
// — Payload for Wi‑Fi → BLE events — 
typedef struct { 
    wifi_ble_evt_type_t type; 
    uint8_t ssid_count; 
    char ssid_list[MAX_WIFI_SCAN_RESULTS][32]; 
} wifi_ble_evt_t;

void queues_init(void);
 
#endif // COMMON_VARIABLES_H