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

#include "esp_mesh.h"
#include "mdns.h"

extern "C" void app_main(void);

#define MSG_SIZE 32
#define TX_WEB_QUEUE_SIZE 32
#define TX_MESH_QUEUE_SIZE 32
#define SEND_WORK_TIME 5000
#define DEVICE_ID 11


enum dtp_message_t {
     DTPT_CAN_DATA = 8,
};

// static twai_message_t ping_message = {.flags = 0, .identifier = 0x180, .data_length_code = 8,
//                                             .data = {0xAA, 0x02 , 0 , 0x40 ,0x10 ,0 ,0 ,0}};
static twai_message_t ping_message = {.flags = 0, .identifier = 0x184, .data_length_code = 8,
                                            .data = {0x48, 0x53 , 0x60 , 0x00 ,0x00 , 0x00 , 0x00 , 0x00}};

uint8_t _rx_buf[3000] = { 0 };

QueueHandle_t _tx_web_queue;
QueueHandle_t _tx_mesh_queue;

std::atomic<bool> _web_send_work_done(true);
bool _can_test_mode = 0;
uint32_t _can_msg_id = 0;

void twai_process(void *);

const esp_timer_create_args_t periodic_timer_args = {
        .callback = &twai_process,
        .name = "periodic"
};

esp_timer_handle_t periodic_timer;

static const char *TAG = "cmb";

class dtp_message {
    int64_t _time_stamp;
    int32_t _id = -1;

protected:
    uint8_t _type;
    uint8_t _dev_id = 0;

public:
    dtp_message() {}

    dtp_message(int32_t id)
    {
        set_id(id);
    }

    void set_id(int32_t id) {
        _id = id;
        set_time_stamp();
    }

    uint8_t dev_id() { return _dev_id; }
    void set_dev_id(uint8_t dev_id) { _dev_id = dev_id; }
    uint8_t get_dev_id() { return _dev_id; }
    void set_time_stamp() { _time_stamp = esp_timer_get_time(); }
    void set_type(uint8_t type) { _type = type; }
    uint8_t get_type() { return _type; }
    int32_t get_id() { return _id; }
};

class msg_can_data: public dtp_message 
{
public:
    twai_message_t twai_msg;    

    msg_can_data() {
        _type = DTPT_CAN_DATA;
    }

    msg_can_data(int32_t id)
        : dtp_message { id }
    {
        _type = DTPT_CAN_DATA;
    }
};

void run_mdns() {
	static bool started = false;

	if (started)
		return;
	started = true;

    esp_err_t err = mdns_init();
    if (err) {
		ESP_LOGE(TAG, "Error setting up MDNS responder!");
        return;
    }

    mdns_hostname_set("cmb");
    mdns_instance_name_set("CMB");

	ESP_LOGE(TAG, "MDNS Started.");
}

void on_self_is_root();

void on_init()
{
    ESP_LOGW("on_init", "size of msg_can_data: %d", sizeof(msg_can_data));
    ESP_LOGI("on_init", "size of dtp_message: %d", sizeof(dtp_message));
    ESP_LOGI("on_init", "size of twai_message_t: %d", sizeof(twai_message_t));
    ESP_LOGI("on_init", "portTICK_RATE_MS: %d", 1 * 1000 / portTICK_RATE_MS);
    ESP_LOGI("", "Task priorities: %d", configMAX_PRIORITIES);
    // ESP_LOGI("", "Task priorities: %d", esp_meh);
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


    // ESP_ERROR_CHECK(esp_mesh_set_6m_rate(true));

    ESP_ERROR_CHECK(esp_wifi_start());

    _tx_web_queue = xQueueCreate(MSG_SIZE, TX_WEB_QUEUE_SIZE);
    _tx_mesh_queue = xQueueCreate(MSG_SIZE, TX_MESH_QUEUE_SIZE);

    mesh::OnIsRootCallbackRegister(on_self_is_root);
}

void TaskDelay(uint16_t ms)
{
    vTaskDelay(1 * ms / portTICK_RATE_MS);
    // vTaskDelay(1 * 1000 / ( ( TickType_t ) 1000 / 100 ));
    
}

void print_twai_msg(twai_message_t &msg)
{
    printf("%8" PRIX32 "  ", msg.identifier);
    printf("%8" PRIX32 "  ", msg.flags);
    printf("    %" PRIX8 "   ", msg.data_length_code);


    for (int i = 0; i < msg.data_length_code; i++)
    	printf("%02" PRIX8 " ", msg.data[i]);
    printf("\n");
}

void print_message(dtp_message &data)
{
    for (int i1 = 0; i1 < sizeof(msg_can_data); ++i1)
        printf("%02" PRIX8 " ", ((uint8_t*)&data)[i1]);

    printf("\n");
}

void web_send_work(void *) {
    static uint8_t data[MSG_SIZE];

    int64_t count_down = SEND_WORK_TIME * 1000;

    while (count_down > 0) 
    {
        auto t0 = esp_timer_get_time();

        if ( xQueueReceive(_tx_web_queue, data, count_down / 1000 / portTICK_RATE_MS ) == pdPASS)
            http::send_async(data, MSG_SIZE);

        count_down -= esp_timer_get_time() - t0;
    }

    // http::queue_work(web_send_work);

    _web_send_work_done = true;
}

void web_tx(dtp_message& msg)
{
    if (_web_send_work_done) {
        _web_send_work_done = false;
        // while (http::queue_work(web_send_work) != ESP_OK) {
        if (http::queue_work(web_send_work) != ESP_OK) {
            _web_send_work_done = true;

            // ESP_LOGE(TAG, "work start failer");
            // vTaskDelay(1);
        }
    }
    if (xQueueSend(_tx_web_queue, &msg, 0/*portMAX_DELAY*/) != pdPASS)
        // ESP_LOGE(TAG, "_tx_web_queue full");
    ;
}

void mesh_tx(dtp_message& msg)
{
    if (xQueueSend(_tx_mesh_queue, &msg, portMAX_DELAY) != pdPASS)
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

        data.set_dev_id(DEVICE_ID);
        data.twai_msg.identifier = rand() % 40;
        auto len = rand() % 9;
        for (int i=0; i<len; ++i) {
            data.twai_msg.data_length_code = i+1;
            data.twai_msg.data[i] = 0xF0 + i;//rand() % 256;
        }
        
        // if (can::receive(data.twai_msg) == ESP_OK) 
        {
            data.set_id(mid++);
            ((uint8_t*)(&data.twai_msg.flags))[3] = data.twai_msg.data[7];

            // print_message(data);

            // mesh_tx(data);
            // if (mesh::is_root())
                ;//web_tx(data);
            // else
                mesh_tx(data);

            // if (mesh::is_root())
            //     web_tx(data);
        }

        auto t0 = esp_timer_get_time();
        static int64_t tl = t0;

        if (t0 - tl > 1000000){
            ESP_LOGW(TAG, "heep size: %d    messages: %d"
                , esp_get_free_heap_size()
                , uxQueueMessagesWaiting(_tx_web_queue));
            
            tl = t0;
        }
        // vTaskDelay(1);
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

        mesh_tx(*((dtp_message*)_rx_buf));
        ESP_LOGI("Received", "message: %s", (char *)_rx_buf);
    }
    vTaskDelete(NULL);
}

void mesh_process_task(void *arg)
{
    uint8_t data[MSG_SIZE];
    uint16_t size = 32;

    while (1)
    {
        if (mesh::receive(data, &size))
        {
            dtp_message &msg = *((dtp_message*)(data));
            // ESP_LOGE(TAG, "data from mesh: size: %d    dev_id: %d", size, msg.dev_id());

            if (mesh::is_root())
                web_tx(*((dtp_message*)data));
        }
    }

    vTaskDelete(NULL);
}

void mesh_send_task(void *arg)
{
    uint8_t data[MSG_SIZE];

    while (1)
    {
        if ( xQueueReceive(_tx_mesh_queue, data,  portMAX_DELAY ) == pdPASS) {
            mesh::send(data, MSG_SIZE);
        }
    }

    vTaskDelete(NULL);
}


void start_twai_process_task()
{
    xTaskCreatePinnedToCore(twai_process_task, "TwaiProc", 4096, NULL, 4, NULL, tskNO_AFFINITY);
}

void start_web_process_task()
{
    xTaskCreatePinnedToCore(web_process_task, "WebProc", 4096, NULL, 1, NULL, tskNO_AFFINITY);
}

void start_mesh_process_task()
{
    // uxTaskGetNumberOfTasks;
    // vTaskGetRunTimeStats();
    // uxTaskGetSystemState
    xTaskCreatePinnedToCore(mesh_process_task, "MeshProc", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_mesh_send_task()
{
    // uxTaskGetNumberOfTasks;
    // uxTaskGetSystemState
    xTaskCreatePinnedToCore(mesh_send_task, "MeshSend", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void on_self_is_root()
{
    http::start();
    run_mdns();
    // start_twai_process_task();
    // start_mesh_send_task();
    // start_web_process_task();
}

void init_can_msg(msg_can_data &msg, bool test = false)
{
    memset((void*)&msg, 0, sizeof(msg_can_data));

    msg.set_dev_id(DEVICE_ID);
    msg.set_type(DTPT_CAN_DATA);

    if (!test)
        return;

    msg.twai_msg.identifier = rand() % 40;
    auto len = rand() % 9;
    for (int i=0; i<len; ++i) {
        msg.twai_msg.data_length_code = i+1;
        msg.twai_msg.data[i] = 0xF0 + i;//rand() % 256;
    }
}

void twai_process(void* args)
{
    msg_can_data msg;

    init_can_msg(msg, _can_test_mode);

    if (_can_test_mode || can::receive(msg.twai_msg) == ESP_OK)
    {
        // printf("can -> :  ");
        // print_twai_msg(msg.twai_msg);

        ((uint8_t*)(&msg.twai_msg.flags))[3] = msg.twai_msg.data[7];
        msg.set_id(_can_msg_id++);

        mesh::send((uint8_t*)&msg, MSG_SIZE);
        static int c;
        c++;

        auto t0 = esp_timer_get_time();
        static int64_t tl = t0;

        if (t0 - tl > 1000000){
            ESP_LOGW(TAG, "heep size: %d    is ISR: %d       rate: %d", esp_get_free_heap_size(), xPortInIsrContext(), c);
            c = 0;
            tl = t0;
        }
    }
}

void app_main(void)
{
    init();

    mesh::start();
    can::start();

    // start_mesh_process_task();
#define SENDER 1
#ifdef SENDER
    if (_can_test_mode) {
        ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 100000));
    } else
        xTaskCreatePinnedToCore([](void*) { while (1) { twai_process(nullptr); }}, "TwaiRX", 4096, NULL, 25, NULL, tskNO_AFFINITY);

// #else
    xTaskCreatePinnedToCore([](void*) {
        uint16_t len = 32;
        int data_size = 0, pcount = 0;
        uint8_t msg_buf[MESH_MPS];
        int pkt_lost = 0;
        std::map<uint8_t, int32_t> last_pkt_ids;

        while (1) {
            len = MESH_MPS;
            while (!mesh::receive(msg_buf, &len)) 
                vTaskDelay(1);
        
            auto msg = (msg_can_data*)msg_buf;
            auto dev_id = msg->get_dev_id();
            auto id = msg->get_id();

            // printf("mt: %d    dev_id: %d\n", msg->get_type(), dev_id);

            if (msg->get_type() == DTPT_CAN_DATA && dev_id != DEVICE_ID) {
                msg->twai_msg.data[7] = ((uint8_t*)&(msg->twai_msg.flags))[3];
                msg->twai_msg.flags &= 1;
        
                // printf("can <- :  ");
                // print_twai_msg(msg->twai_msg);

                can::transmit(msg->twai_msg);

                pkt_lost += (id - last_pkt_ids[dev_id] - 1);
                last_pkt_ids[dev_id] = id;
            }

            if (mesh::is_root())
                web_tx(*msg);

            data_size += len;
            pcount++;

            auto t0 = esp_timer_get_time();
            static int64_t tl = t0;

            if (t0 - tl > 1000000){
                ESP_LOGW(TAG, "rate: %d   count: %d   pkt_lost: %d", data_size, pcount, pkt_lost);
                data_size = pcount = pkt_lost = 0;
                tl = t0;
            }        
        }
        vTaskDelete(NULL);
    }, "MeshRX", 4096, NULL, 7, NULL, tskNO_AFFINITY);
#endif

    xTaskCreatePinnedToCore([](void*) { 
        // TaskDelay(10000);
        // can::transmit(ping_message);
        // ESP_LOGI("", " Transmited");
        vTaskDelete(NULL);
    }, "CMBLOOP", 1024, NULL, 25, NULL, tskNO_AFFINITY);


    // vTaskGetRunTimeStats((char*)_rx_buf);
    // printf((char*)_rx_buf);

    // vTaskDelay(portMAX_DELAY);

}

