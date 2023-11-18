#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "TmpNet.hpp"


#include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
// #include "esp_event.h"
#include "esp_log.h"

#define EXAMPLE_ESP_WIFI_SSID      "CANxGT"
#define EXAMPLE_ESP_WIFI_PASS      "" 
#define EXAMPLE_ESP_WIFI_CHANNEL   8
#define EXAMPLE_MAX_STA_CONN       4


void wifi_init_softap(void);

QueueHandle_t msg_queue;

struct msg_data_t {
    size_t data_size;
    uint8_t *data;
};

void TmpNet::start()
{
    msg_queue = xQueueCreate(64, sizeof(msg_data_t));
    wifi_init_softap();
}

void TmpNet::stop()
{

}

void TmpNet::send(uint8_t *data, uint16_t len)
{
    msg_data_t d;
    d.data_size = len;
    d.data = new uint8_t[len];
    memcpy(d.data, data, len);

    xQueueSend(msg_queue, &d, 0);
}


bool TmpNet::receive(uint8_t *data, uint16_t *len)
{
    msg_data_t d;
    if (!xQueueReceive(msg_queue, &d, portMAX_DELAY))
        return false;

    memcpy(data, d.data, d.data_size);
    *len = d.data_size;

    delete [] d.data;

    return true;
}

void TmpNet::OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct) 
{}
void TmpNet::OnIsRootCallbackRegister(_is_root_callback_funct_t funct)
{}

static _is_connected_callback_funct_t is_connected_callback_funct_t;
void TmpNet::OnIsConnectedCallbackRegister(_is_connected_callback_funct_t funct)
{ is_connected_callback_funct_t = funct; }

void TmpNet::OnIsDisconnectedCallbackRegister(_is_disconnected_callback_funct_t funct)
{}


static const char *TAG = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);

    } else if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "soft_ap started");
        is_connected_callback_funct_t();
    }
}

void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = { 0 },
            .password = { 0 },
            .ssid_len = 0,
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .pmf_cfg = {
                    .required = false,
            },
        },
    };
    // memset()
    memcpy((uint8_t *) wifi_config.ap.ssid, EXAMPLE_ESP_WIFI_SSID, strlen(EXAMPLE_ESP_WIFI_SSID) + 1);
    wifi_config.ap.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID);
    
    // memcpy((uint8_t *) &wifi_config.router.password, _router_pw.c_str(), _router_pw.size());

    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}
