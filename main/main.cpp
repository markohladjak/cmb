#include "mesh.hpp"
#include "can.hpp"
#include "http.hpp"
#include <map>
#include <atomic>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include <esp_log.h>

extern "C" {
#include "twai.h"

#include "esp_log.h"
#include "inttypes.h"
#include "esp_timer.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
}

extern "C" void app_main(void);

#define MSG_SIZE 32
#define TX_WEB_QUEUE_SIZE 32
#define SEND_WORK_TIME 100

enum dtp_message_t {
     DTPT_CAN_DATA = 8,
};

static twai_message_t ping_message = {.flags = 0, .identifier = 0x180, .data_length_code = 8,
                                            .data = {0xAA, 0x02 , 0 , 0x40 ,0x10 ,0 ,0 ,0}};

uint8_t _rx_buf[3000] = { 0 };

QueueHandle_t _tx_web_queue;

std::atomic<bool> _web_send_work_done(true);

static const char *TAG = "cmb";

class dtp_message {
    int64_t _created_time;
    int32_t _id = -1;

protected:
    uint8_t _type;

public:
    dtp_message() {}

    dtp_message(int32_t id)
    {
        set_id(id);
    }

    void set_id(int32_t id) {
        _id = id;
        _created_time = esp_timer_get_time();
    }
};

class msg_can_data: public dtp_message 
{
public:
    twai_message_t twai_msg;    

    msg_can_data() {}

    msg_can_data(int32_t id)
        : dtp_message { id }
    {
        _type = DTPT_CAN_DATA;
    }
};

void on_init()
{
    ESP_LOGI("on_init", "size of msg_can_data: %d", sizeof(msg_can_data));
    ESP_LOGI("on_init", "size of dtp_message: %d", sizeof(dtp_message));
    ESP_LOGI("on_init", "size of twai_message_t: %d", sizeof(twai_message_t));
    ESP_LOGI("on_init", "portTICK_RATE_MS: %d", 1 * 1000 / portTICK_RATE_MS);
}

void init()
{
    on_init();

    esp_log_level_set("*", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    _tx_web_queue = xQueueCreate(MSG_SIZE, TX_WEB_QUEUE_SIZE);
}

void TaskDelay(uint16_t ms)
{
    vTaskDelay(1 * ms / portTICK_RATE_MS);
    // vTaskDelay(1 * 1000 / ( ( TickType_t ) 1000 / 100 ));
    
}

void print_twai_msg(twai_message_t &msg)
{
    printf("0x%04" PRIX32 "  ", msg.identifier);
    printf("%04" PRIX32 "     ", msg.flags);

    for (int i = 0; i < msg.data_length_code; i++)
    	printf("%02" PRIX8 " ", msg.data[i]);
    printf("\n");
}

void web_send_work(void *) {
    uint8_t data[MSG_SIZE];

    int64_t count_down = SEND_WORK_TIME * 1000;

    while (count_down > 0) 
    {
        auto t0 = esp_timer_get_time();

        if ( xQueueReceive(_tx_web_queue, data, count_down / 1000 / portTICK_RATE_MS ) == pdPASS)
            http::send_async(data, MSG_SIZE);

        count_down -= esp_timer_get_time() - t0;
    }

    _web_send_work_done = true;
}

void web_tx(dtp_message& msg)
{
    if (xQueueSend(_tx_web_queue, &msg, 0 /*portMAX_DELAY*/) != pdPASS)
        // ESP_LOGE(TAG, "_tx_web_queue full");
    ;
}

void twai_process_task(void* arg) 
{
    uint32_t mid = 0;
    while (1) 
    {
        msg_can_data data;

        memset((void*)&data, 0, sizeof(msg_can_data));

        data.twai_msg.identifier = rand() % 2048;
        auto len = rand() % 9;
        for (int i=0; i<len; ++i) {
            data.twai_msg.data_length_code = i+1;
            data.twai_msg.data[i] = 0xF0 + i;//rand() % 256;
        }


        printf("\n");
        
        // if (can::receive(data.twai_msg) == ESP_OK) 
        {
            data.set_id(mid++);
            ((uint8_t*)(&data.twai_msg.flags))[3] = data.twai_msg.data[7];

            for (int i1 = 0; i1 < sizeof(msg_can_data); ++i1)
            printf("%02" PRIX8 " ", ((uint8_t*)&data)[i1]);

            web_tx(data);

            if (_web_send_work_done) {
                _web_send_work_done = false;
                if (http::queue_work(web_send_work) != ESP_OK) {
                    _web_send_work_done = true;

                    // ESP_LOGE(TAG, "work start failer");
                    TaskDelay(10);
                }
            }
        }

        auto t0 = esp_timer_get_time();
        static int64_t tl = t0;

        if (t0 - tl > 1000000){
            ESP_LOGW(TAG, "heep size: %d    messages: %d"
                , esp_get_free_heap_size()
                , uxQueueMessagesWaiting(_tx_web_queue));
            
            tl = t0;
        }

        vTaskDelay(10);
    }
            
    vTaskDelete(NULL);
}

    // auto t0 = esp_timer_get_time();
    // static int64_t tl = t0;

    // else if (t0 - tl > 1000000){
    //     auto t1 = esp_timer_get_time();
    //     ESP_LOGW("", "send time delta: %" PRIu64 "    heep size: %d", t1 - t0, esp_get_free_heap_size());
    //     tl = t0;
    // }
        // vTaskDelay(1);
    // }

void web_process_task(void *arg)
{
    while (1)
    {
        size_t len;
        http::receive(_rx_buf, &len);

        ESP_LOGI("Received", "message: %s", (char *)_rx_buf);
    }
    vTaskDelete(NULL);
}

void mesh_process_task(void *arg)
{
    while (1)
    {
        auto buff = new uint8_t[1400];
        for (int i=0; i<1400; ++i)
            buff[i] = rand() % 200;

        if (!http::send(buff, 1400)) {
            delete []buff;
            TaskDelay(200);
        }
            // TaskDelay(1000);
    }
    vTaskDelete(NULL);
}

void start_twai_process_task()
{
    xTaskCreatePinnedToCore(twai_process_task, "TwaiProc", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_web_process_task()
{
    xTaskCreatePinnedToCore(web_process_task, "WebProc", 4096, NULL, 0, NULL, tskNO_AFFINITY);
}

void start_mesh_process_task()
{
    // uxTaskGetNumberOfTasks;
    // vTaskGetRunTimeStats();
    // uxTaskGetSystemState
    xTaskCreatePinnedToCore(mesh_process_task, "MeshProc", 4096, NULL, 0, NULL, tskNO_AFFINITY);
}

void app_main(void)
{
    ESP_LOGE("", "Task priorities: %d", configMAX_PRIORITIES);
    init();

    mesh::start();
    can::start();
    http::start();

    start_twai_process_task();
    start_web_process_task();
}

