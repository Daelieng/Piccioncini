#include "common_variables.h"
#include "wifi_handler.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <string.h>


#define WIFI_TAG "WIFI_TAG"


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    //Connesso
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(WIFI_TAG, "Evento: STA_CONNECTED"); 

        // Ottengo le info dell'AP a cui sono connesso
        wifi_ap_record_t ap_info;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
        if (ret == ESP_OK) {
            // ap_info.ssid è una stringa non terminata da NULL, garantiamo terminatore
            char ssid[33] = { 0 };
            memcpy(ssid, ap_info.ssid, sizeof(ap_info.ssid)); 
            ssid[32] = '\0';

            ESP_LOGI(WIFI_TAG, "Connesso all'AP SSID: %s", ssid);

            g_wifi_connected = true;

        } else {
            ESP_LOGE(WIFI_TAG, "Errore esp_wifi_sta_get_ap_info: %s",
                     esp_err_to_name(ret));
        }

    }
    //Connessione persa
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {

        g_wifi_connected = false;

        // Estraggo il dettaglio del motivo (opzionale)
        wifi_event_sta_disconnected_t* dis = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI("WIFI_EVT", "STA_DISCONNECTED, reason=%d", dis->reason);  
                                               
        esp_wifi_connect();                     // tenta subito la riconnessione:contentReference[oaicite:7]{index=7}  
    }
}

static void wifi_scan_start(void) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    ESP_LOGI(WIFI_TAG, "Avvio scansione WiFi...");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
    if (ap_num == 0) {
        ESP_LOGI(WIFI_TAG, "Nessuna rete WiFi trovata");
        return;
    }

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!ap_list) {
        ESP_LOGE(WIFI_TAG, "Memoria esaurita per ap_list");
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_list));

    wifi_ble_evt_t evt = {
        .type = WIFI_BLE_EVT_SCAN_DONE,
        .ssid_count = (ap_num < MAX_WIFI_SCAN_RESULTS) ? ap_num : MAX_WIFI_SCAN_RESULTS
    };

    for (int i = 0; i < evt.ssid_count; i++) {
        strncpy(evt.ssid_list[i], (char *)ap_list[i].ssid, sizeof(evt.ssid_list[i]) - 1);
        evt.ssid_list[i][sizeof(evt.ssid_list[i]) - 1] = '\0';
        ESP_LOGI(WIFI_TAG, "Rete trovata: %s", evt.ssid_list[i]);
    }

    xQueueSend(wifi_to_ble_q, &evt, portMAX_DELAY);
    free(ap_list);
}


static void wifi_task(void *arg) {
    ble_wifi_evt_t evt;
    while (1) {
        if (xQueueReceive(ble_to_wifi_q, &evt, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(WIFI_TAG, "Comando ble_wifi ricevuto: %u", evt.type);
            switch (evt.type) {
                case BLE_WIFI_EVT_BTN_PRESS:
                    ESP_LOGI(WIFI_TAG, "Comando scan da BLE ricevuto");
                    wifi_scan_start();
                    break;
                case BLE_WIFI_EVT_CONNECT:
                    ESP_LOGI(WIFI_TAG, "Comando connect da BLE: SSID=%s", evt.ssid);
                    
                    wifi_config_t wifi_config = {0};
                    strncpy((char*)wifi_config.sta.ssid, evt.ssid, sizeof(wifi_config.sta.ssid) - 1);
                    strncpy((char*)wifi_config.sta.password, evt.password, sizeof(wifi_config.sta.password) - 1);
                    
                    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK ||
                        esp_wifi_connect() != ESP_OK) {
                        ESP_LOGE(WIFI_TAG, "Errore durante connessione WiFi");
                    }
                    break;
                default:
                    ESP_LOGW(WIFI_TAG, "Evento BLE->WiFi sconosciuto: %d", evt.type);
                    break;
            }
        }
    }
}

void wifi_init_sta(UBaseType_t task_priority) {


    ESP_LOGI(WIFI_TAG, "Inizializzazione WiFi in modalità Station...");

    // Inizializza rete e loop eventi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    // Configurazione station senza credenziali
    wifi_config_t wifi_config = { 0 };
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Crea il task per gestire comandi BLE->WiFi
    xTaskCreate(wifi_task, "WIFI_TASK", 4096, NULL, task_priority, NULL);
    ESP_LOGI(WIFI_TAG, "WiFi station avviata");
}
