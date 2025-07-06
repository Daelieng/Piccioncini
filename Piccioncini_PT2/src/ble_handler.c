/* src/ble_handler.c */

#include "ble_handler.h"
#include "common_variables.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include <string.h>

static const char* BLE_TAG = "BLE_HANDLER";

// BLE connection/context
static uint16_t ble_conn_id = 0;
static esp_gatt_if_t global_ble_gatts_if = 0;

// GATT characteristic handles
static uint16_t service_handle = 0;
static uint16_t wifi_scan_handle = 0;
static uint16_t command_handle = 0;
static uint16_t wifi_config_handle = 0;
static uint16_t wifi_status_handle = 0;

typedef struct {
    uint16_t char_handle;
    uint16_t cccd_handle;
} ble_characteristic_t;

ble_characteristic_t wifi_scan_characteristic;

// UUIDs
#define DEVICE_NAME           "Piccioncino_Ila"
#define SERVICE_UUID          0x00FF
#define WIFI_STATUS_UUID      0xFF10
#define COMMAND_UUID          0xFF11
#define WIFI_SCAN_LIST_UUID   0xFF20
#define WIFI_CONFIG_UUID      0xFF21

// Task prototype
static void ble_task(void* arg);


// Forward declarations for GATT callbacks
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void advertizer_config(void);

void ble_handler_init(UBaseType_t task_priority) {

    // Initialize BLE controller and Bluedroid
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Set device name and register callbacks
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(DEVICE_NAME));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    // Create BLE task
    xTaskCreate(ble_task, "BLE_TASK", 4096, NULL, task_priority, NULL);
    ESP_LOGI(BLE_TAG, "BLE handler initialized");
}


static void ble_task(void* arg) {
    wifi_ble_evt_t evt;
    while (1) {
        if (xQueueReceive(wifi_to_ble_q, &evt, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(BLE_TAG, "WIFI_BLE recived");
            if (evt.type == WIFI_BLE_EVT_SCAN_DONE) {
                ESP_LOGI(BLE_TAG, "Risultati scansione ricevuti");
                char wifi_list[256] = "";
                int max = evt.ssid_count;
                if (max > 6) max = 6;
                for (int i = 0; i < max; i++) {
                    strncat(wifi_list, evt.ssid_list[i], sizeof(wifi_list) - strlen(wifi_list) - 1);
                    if (i < max - 1) {
                        strncat(wifi_list, ", ", sizeof(wifi_list) - strlen(wifi_list) - 1);
                    }
                }
                ESP_LOGI(BLE_TAG, "Notifying WiFi list: %s", wifi_list);
                esp_ble_gatts_set_attr_value(wifi_scan_handle, strlen(wifi_list), (uint8_t*)wifi_list);
                esp_ble_gatts_send_indicate(global_ble_gatts_if, ble_conn_id, wifi_scan_handle, strlen(wifi_list), (uint8_t*)wifi_list, false);
            }        
        }
    }
}

static void advertizer_config(void) {
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .appearance = ESP_BLE_APPEARANCE_UNKNOWN,
        .service_uuid_len = 16,
        .p_service_uuid = (uint8_t[]){0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00},
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT)
    };
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
        });
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                .id = {.uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = SERVICE_UUID}},
                .is_primary = true
            }, 20);
            break;

        case ESP_GATTS_CREATE_EVT:
            service_handle = param->create.service_handle;

            // 1) WiFi Status (read)
            {
            esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_16,
                                    .uuid.uuid16 = WIFI_STATUS_UUID };
            esp_ble_gatts_add_char(
                service_handle, &uuid,
                ESP_GATT_PERM_READ,
                ESP_GATT_CHAR_PROP_BIT_READ,
                NULL, NULL
            );
            }

            // 2) Command (write o writeWithoutResponse)
            {
            esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_16,
                                    .uuid.uuid16 = COMMAND_UUID };
            esp_ble_gatts_add_char(
                service_handle, &uuid,
                ESP_GATT_PERM_WRITE,
                ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                NULL, NULL
            );
            }

            // 3) WiFi Scan List (read + notify)
            {
            esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_16,
                                    .uuid.uuid16 = WIFI_SCAN_LIST_UUID };
            esp_attr_value_t att = {
                .attr_max_len = 256,
                .attr_len     = 0,
                .attr_value   = NULL
            };
            esp_ble_gatts_add_char(
                service_handle, &uuid,
                ESP_GATT_PERM_READ,
                ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                &att, NULL
            );
            // descriptor per il notify
            esp_ble_gatts_add_char_descr(
                service_handle,
                &(esp_bt_uuid_t){ .len=ESP_UUID_LEN_16,
                                .uuid.uuid16=ESP_GATT_UUID_CHAR_CLIENT_CONFIG },
                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                NULL, NULL
            );
            }

            // 4) WiFi Config (write)
            {
            esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_16,
                                    .uuid.uuid16 = WIFI_CONFIG_UUID };
            esp_ble_gatts_add_char(
                service_handle, &uuid,
                ESP_GATT_PERM_WRITE,
                ESP_GATT_CHAR_PROP_BIT_WRITE,
                NULL, NULL
            );
            }

    // Solo dopo aver aggiunto TUTTE le caratteristiche:
    esp_ble_gatts_start_service(service_handle);
    break;


        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(BLE_TAG, "Caratteristica aggiunta, avviando servizio...");
            if (param->add_char.char_uuid.uuid.uuid16 == WIFI_STATUS_UUID) {
                wifi_status_handle = param->add_char.attr_handle;
                ESP_LOGI("GATTS_DEMO", "WiFi_Status aggiunta, handle = %d", wifi_status_handle);
            } else if (param->add_char.char_uuid.uuid.uuid16 == COMMAND_UUID) {
                command_handle = param->add_char.attr_handle;
                ESP_LOGI("GATTS_DEMO", "Command aggiunta, handle = %d", command_handle);
            } else if (param->add_char.char_uuid.uuid.uuid16 == WIFI_SCAN_LIST_UUID) {
                wifi_scan_handle = param->add_char.attr_handle;
                ESP_LOGI(BLE_TAG, "WiFi_Scan aggiunta, handle = %d", wifi_scan_handle);
            } else if (param->add_char.char_uuid.uuid.uuid16 == WIFI_CONFIG_UUID) {
                wifi_config_handle = param->add_char.attr_handle;
                ESP_LOGI(BLE_TAG, "WiFi_Config aggiunta, handle = %d", wifi_config_handle);
            }
            advertizer_config();
            break;

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            if (param->add_char_descr.descr_uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                wifi_scan_characteristic.cccd_handle = param->add_char_descr.attr_handle;
                ESP_LOGI(BLE_TAG, "CCCD descriptor handle: %d", wifi_scan_characteristic.cccd_handle);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ble_conn_id = param->connect.conn_id;
            global_ble_gatts_if = gatts_if;
            ESP_LOGI(BLE_TAG, "Client connected: conn_id=%d", ble_conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(BLE_TAG, "Client disconnected");
            ble_conn_id = 0;
            global_ble_gatts_if = 0;
            esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
                .adv_int_min=0x20, .adv_int_max=0x40, .adv_type=ADV_TYPE_IND, .own_addr_type=BLE_ADDR_TYPE_PUBLIC, .channel_map=ADV_CHNL_ALL, .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
            });
            break;

        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(BLE_TAG, "Evento scrittura ricevuto %s", param->write.value);

            if(param->write.handle == wifi_scan_characteristic.cccd_handle && param->write.len == 2){
                ESP_LOGI(BLE_TAG, "Conferma iscrizione notifiche");
                esp_ble_gatts_send_response(
                    gatts_if,
                    param->write.conn_id,
                    param->write.trans_id,
                    ESP_GATT_OK,
                    NULL
                    );
            }

            if (param->write.handle == command_handle) {
                // Risposta immediata
                esp_ble_gatts_send_response( gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                ESP_LOGI(BLE_TAG, "Risposta inviata");
                // Elabora il comando dopo aver inviato la risposta
                if (param->write.len > 0) {
                    char cmd[param->write.len + 1];
                    memcpy(cmd, param->write.value, param->write.len);
                    cmd[param->write.len] = '\0';
                    
                    ble_wifi_evt_t evt = {
                        .type = BLE_WIFI_EVT_BTN_PRESS,
                    };
                    xQueueSend(ble_to_wifi_q, &evt, portMAX_DELAY);
                    ESP_LOGI(BLE_TAG, "Comando in queue");
                }
            }
            else if (param->write.handle == wifi_config_handle) {
                // Nuovo codice per gestione WiFi Config
                ESP_LOGI(BLE_TAG, "credenziali: %s", (char *)param->write.value);
                    // Nuovo codice per gestione WiFi Config
                    esp_ble_gatts_send_response(
                        gatts_if,
                        param->write.conn_id,
                        param->write.trans_id,
                        ESP_GATT_OK,
                        NULL
                    );
                    
                    if (param->write.len > 0) {
                        // Alloca memoria e aggiungi terminatore null
                        const char *to_decode=(char *)param->write.value;

                        char *start1 = strstr(to_decode, "%%");
                        if (start1==NULL){
                            ESP_LOGI(BLE_TAG, "Errore all'inizio");
                        }
                        start1+=2;
                        char *end1=strstr(start1, "%%");
                        if (end1==NULL){
                            ESP_LOGI(BLE_TAG, "Non trovo l fine di ssid");
                        }
                        size_t len1=end1-start1;
                        char *ssid=malloc(len1+1);
                        strncpy(ssid, start1, len1);
                        ssid[len1]='\0';

                        char *start2 = end1 + 2;
    
                        // Trova la fine di string2
                        char *end2 = strstr(start2, "%%");
                        if (end2 == NULL) {
                            printf("Formato non valido per string2!\n");
                        }
                        
                        // Estrae string2
                        size_t len2 = end2 - start2;
                        char *password = malloc(len2 + 1);
                        strncpy(password, start2, len2);
                        password[len2] = '\0';

                        ESP_LOGI(BLE_TAG, "SSID: %s, Password: %s", ssid, password);
                        
                        if (ssid != NULL && password != NULL) {
                            ESP_LOGI(BLE_TAG, "SSID: %s, Password: %s", ssid, password);
                            
                            ble_wifi_evt_t evt = {
                                .type = BLE_WIFI_EVT_CONNECT,
                            };

                            // Copia le stringhe negli array della struct
                            strncpy(evt.ssid, ssid, sizeof(evt.ssid) - 1);
                            evt.ssid[sizeof(evt.ssid) - 1] = '\0';

                            strncpy(evt.password, password, sizeof(evt.password) - 1);
                            evt.password[sizeof(evt.password) - 1] = '\0';

                            xQueueSend(ble_to_wifi_q, &evt, portMAX_DELAY);

                            // Libera la memoria allocata
                            free(ssid);
                            free(password);
                        }
                    }
                }
            break;

        default:
            break;
    }
}
