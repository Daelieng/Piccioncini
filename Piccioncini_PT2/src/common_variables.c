#include "common_variables.h"

// definizione della flag globale
bool g_wifi_connected = false;

// definizione delle queue
QueueHandle_t ble_to_wifi_q = NULL;
QueueHandle_t wifi_to_ble_q = NULL;

void queues_init(void) {
    // crea la queue BLE→Wi‑Fi
    ble_to_wifi_q = xQueueCreate(
        /*depth*/      10, 
        /*item size*/  sizeof(ble_wifi_evt_t)
    );
    configASSERT(ble_to_wifi_q != NULL);

    // crea la queue Wi‑Fi→BLE
    wifi_to_ble_q = xQueueCreate(
        /*depth*/      10, 
        /*item size*/  sizeof(wifi_ble_evt_t)
    );
    configASSERT(wifi_to_ble_q != NULL);
}