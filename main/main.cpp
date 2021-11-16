#include "mesh.hpp"
#include "can.hpp"
#include "http.hpp"
#include "uart.hpp"
#include <map>
#include <atomic>

#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "mdns.h"

#include "driver/twai.h"

#include "inttypes.h"

#include "twai_filter.hpp"
#include "dtp_message.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

extern "C" void app_main(void);

#define MSG_SIZE 32
#define TX_WEB_QUEUE_SIZE 32
#define TX_MESH_QUEUE_SIZE 32
#define SEND_WORK_TIME 500

#define DEVICE_ID 48

#define TASK_CONTROL_DELAY 10

#define UART_TXD_PIN (GPIO_NUM_4)
#define UART_RXD_PIN (GPIO_NUM_5)

// static twai_message_t ping_message = {.flags = 0, .identifier = 0x180, .data_length_code = 8,
//                                             .data = {0xAA, 0x02 , 0 , 0x40 ,0x10 ,0 ,0 ,0}};
// static twai_message_t ping_message = {.flags = 0, .identifier = 0x184, .data_length_code = 8,
//                                             .data = {0x48, 0x53 , 0x60 , 0x00 ,0x00 , 0x00 , 0x00 , 0x00}};

QueueHandle_t _tx_web_queue;
// QueueHandle_t _tx_mesh_queue;

twai_filter twai_in_filter;
twai_filter twai_out_filter;

std::atomic<bool> _web_send_work_done(true);
std::atomic<bool> _web_rx_task_run(true);
std::atomic<bool> _net_rx_task_run(true);
std::atomic<bool> _twai_rx_task_run(true);

// Test Mode Members
bool _can_test_mode = 0;
#define TEST_MSG_SEND_PERION 5000
std::atomic<uint32_t> _test_msg_send_period(TEST_MSG_SEND_PERION);

uint32_t _can_msg_id = 0;

void twai_rx(void *arg = nullptr);

const esp_timer_create_args_t periodic_timer_args = {
        .callback = &twai_rx,
        .name = "periodic"
};

esp_timer_handle_t periodic_timer;

uart *_uart;

static const char *TAG = "cmb";


void run_mdns();
void stop_mdns();

void on_self_is_root();
void on_net_connected();
void on_net_disconnected();
void print_twai_msg(twai_message_t &msg);
void print_message(dtp_message &data);
char* sprint_twai_msg(twai_message_t &msg, char* out_buf);

void TaskDelay(uint16_t ms)
{
    vTaskDelay(1 * ms / portTICK_RATE_MS);
    // vTaskDelay(1 * 1000 / ( ( TickType_t ) 1000 / 100 ));
    
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
    if (http::queue_work(web_send_work) != ESP_OK)
        _web_send_work_done = true;

    // _web_send_work_done = true;
}

void web_tx(dtp_message& msg)
{
    if (xQueueSend(_tx_web_queue, &msg, 0/*portMAX_DELAY*/) != pdPASS)
        // ESP_LOGE(TAG, "_tx_web_queue full");
    ;

    if (_web_send_work_done) {
        _web_send_work_done = false;
        // while (http::queue_work(web_send_work) != ESP_OK) {
        if (http::queue_work(web_send_work) != ESP_OK) {
            _web_send_work_done = true;

            // ESP_LOGE(TAG, "work start failer");
            // vTaskDelay(1);
        }
    }
}

void net_tx(dtp_message& msg)
{
    mesh::send((uint8_t*)&msg, MSG_SIZE);
    // if (xQueueSend(_tx_mesh_queue, &msg, portMAX_DELAY) != pdPASS)
        // ESP_LOGE(TAG, "_tx_web_queue full");
    ;

    _uart->send((uint8_t*)&msg, MSG_SIZE);
}

void twai_rx_task(void* arg) 
{
    while (_twai_rx_task_run) 
    {
        twai_rx();
    }
            
    vTaskDelete(NULL);
}

void ui_rx_task(void *arg)
{
    uint8_t _rx_buf[MESH_MPS] = { 0 };

    while (1)
    {
        size_t len;
        if (!http::receive(_rx_buf, &len, TASK_CONTROL_DELAY))
            continue;

        net_tx(*((dtp_message*)_rx_buf));
        ESP_LOGI("web_rx", "msg: %s", (char *)_rx_buf);
    }

    vTaskDelete(NULL);
}

void net_rx_task(void *arg)
{
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

        if (msg->get_type() != DTPT_CAN_DATA)
            continue;

        auto dev_id = msg->get_dev_id();
        auto id = msg->get_id();

        // printf("mt: %d    dev_id: %d\n", msg->get_type(), dev_id);

        if (mesh::is_root())
            web_tx(*msg);

        if (msg->get_type() == DTPT_CAN_DATA && dev_id != DEVICE_ID) {
            msg->twai_msg.data[7] = ((uint8_t*)&(msg->twai_msg.flags))[3];
            msg->twai_msg.flags &= 1;
    
            can::transmit(msg->twai_msg);

            pkt_lost += (id - last_pkt_ids[dev_id] - 1);
            last_pkt_ids[dev_id] = id;

{
            data_size += len;
            pcount++;

            auto t0 = esp_timer_get_time();
            static int64_t tl = t0;

            if (t0 - tl > 1000000){
                ESP_LOGW("net_rx", " count: %d     rate: %d      pkt_lost: %d", pcount, data_size, pkt_lost);
                data_size = pcount = pkt_lost = 0;
                tl = t0;
            }        
}

        }
    }
    vTaskDelete(NULL);
}

void uart_rx_task(void *arg)
{
    uint8_t msg[MSG_SIZE];

    while (1) 
    {
        _uart->read(msg);

        printf("uart -> ");
        print_message(*((dtp_message*)msg));
    }
}

bool twai_msg_receive(twai_message_t &msg)
{
    memset((void*)&msg, 0, sizeof(twai_message_t));

    if (_can_test_mode) {
        msg.identifier = rand() % 40;
        msg.flags = 0;
        msg.data_length_code = rand() % 9;
        for (int i = 0; i < msg.data_length_code; ++i) {
            msg.data[i] = 0xF0 + i;//rand() % 256;
        }
        return true;
    }
    else {
        auto err = can::receive(msg);
        return err == ESP_OK || err == ESP_ERR_TIMEOUT;
    }
}

void twai_rx(void* arg)
{
    msg_can_data msg;
    msg.set_dev_id(DEVICE_ID);

    if (twai_msg_receive(msg.twai_msg))
    {
        if (msg.twai_msg.data_length_code == 8)
            ((uint8_t*)(&msg.twai_msg.flags))[3] = msg.twai_msg.data[7];
        msg.set_id(_can_msg_id++);

        if (twai_in_filter(msg.twai_msg))
            mesh::send((uint8_t*)&msg, MSG_SIZE);


{
        auto t0 = esp_timer_get_time();
        static int64_t tl = t0;
        static int pcount = 0;
        pcount++;

        if (t0 - tl > 1000000){
            ESP_LOGW("twai_rx", "count: %d       [%s]", pcount, sprint_twai_msg(msg.twai_msg, nullptr));
            pcount = 0;
            tl = t0;
        }
}

    } 
    else
        vTaskDelay(1);
}

// void mesh_send_task(void *arg)
// {
//     uint8_t data[MSG_SIZE];

//     while (1)
//     {
//         if ( xQueueReceive(_tx_mesh_queue, data,  portMAX_DELAY ) == pdPASS) {
//             mesh::send(data, MSG_SIZE);
//         }
//     }

//     vTaskDelete(NULL);
// }


void start_twai_rx_task()
{
    if (_can_test_mode) {
        ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, _test_msg_send_period));
    }
    else
        xTaskCreatePinnedToCore(twai_rx_task, "TwaiRX", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_web_rx_task()
{
    _web_rx_task_run = true;
    xTaskCreatePinnedToCore(ui_rx_task, "WebRX", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_net_rx_task()
{
    xTaskCreatePinnedToCore(net_rx_task, "NetRX", 4096, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_uart_rx_task()
{
    xTaskCreatePinnedToCore(uart_rx_task, "UartRX", 4096, NULL, 8, NULL, tskNO_AFFINITY);
}

void stop_web_rx_task()
{
    _web_rx_task_run = true;
}

void on_self_is_root()
{
}

void on_net_connected()
{
    if (!mesh::is_root())
        return;

    http::start();
    run_mdns();

    start_web_rx_task();
}

void on_net_disconnected()
{
    if (!mesh::is_root())
        return;

    http::stop();
    stop_mdns();

    stop_web_rx_task();
}

void run_mdns() {
    esp_err_t err = mdns_init();
    if (err) {
		ESP_LOGE(TAG, "Error setting up MDNS responder!");
        return;
    }

    mdns_hostname_set("cmb");
    mdns_instance_name_set("CMB");

	ESP_LOGE(TAG, "MDNS Started.");
}

void stop_mdns()
{
    mdns_free();
}

void print_state_report()
{
    static int c;
    c++;

    auto t0 = esp_timer_get_time();
    static int64_t tl = t0;

    if (t0 - tl > 1000000){
        ESP_LOGW(TAG, "heep size: %d    is ISR: %d       rate: %d", esp_get_free_heap_size(), xPortInIsrContext(), c);
        c = 0;
        tl = t0;
    }

    //     auto t0 = esp_timer_get_time();
    // static int64_t tl = t0;

    // if (t0 - tl > 1000000){
    //     ESP_LOGW(TAG, "heep size: %d    messages: %d"
    //         , esp_get_free_heap_size()
    //         , uxQueueMessagesWaiting(_tx_web_queue));
        
    //     tl = t0;
    // }

}

void PrintTasksState()
{
    uint8_t _rx_buf[3000] = { 0 };

    // vTaskGetRunTimeStats((char*)_rx_buf);
    printf("Name          State   Priority  Stack  Num\n\
******************************************\n");

    vTaskList((char*)_rx_buf);
    printf((char*)_rx_buf);

    printf("\nTasks count: %d\n", uxTaskGetNumberOfTasks());
}

char* sprint_twai_msg(twai_message_t &msg, char* out_buf)
{
    static char buf[64];
    auto b = out_buf == nullptr ? buf : out_buf;

    b += sprintf(b, "%8" PRIX32 "  ", msg.identifier);
    b += sprintf(b, "%8" PRIX32 "  ", msg.flags);
    b += sprintf(b, "    %" PRIX8 "   ", msg.data_length_code);


    for (int i = 0; i < msg.data_length_code; i++)
    	b += sprintf(b, "%02" PRIX8 " ", msg.data[i]);

    return out_buf == nullptr ? buf : out_buf;
}

void print_twai_msg(twai_message_t &msg)
{
    printf("%s\n", sprint_twai_msg(msg, nullptr));
}

void print_message(dtp_message &data)
{
    for (int i1 = 0; i1 < sizeof(msg_can_data); ++i1)
        printf("%02" PRIX8 " ", ((uint8_t*)&data)[i1]);

    printf("\n");
}

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
    while (!DEVICE_ID) {
        ESP_LOGE(TAG, "DEVICE_ID not set...");
        TaskDelay(1000);
    }

    on_init();

    esp_log_level_set("*", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    // ESP_ERROR_CHECK(esp_mesh_set_6m_rate(true));

    ESP_ERROR_CHECK(esp_wifi_start());

    _tx_web_queue = xQueueCreate(MSG_SIZE, TX_WEB_QUEUE_SIZE);
    // _tx_mesh_queue = xQueueCreate(MSG_SIZE, TX_MESH_QUEUE_SIZE);

    mesh::OnIsRootCallbackRegister(on_self_is_root);
    mesh::OnIsConnectedCallbackRegister(on_net_connected);
    mesh::OnIsDisconnectedCallbackRegister(on_net_disconnected);
}

void app_main(void)
{
    init();

    mesh::start();
    can::start();

    _uart = new uart(UART_NUM_1, UART_RXD_PIN, UART_TXD_PIN);
    _uart->start();

    start_twai_rx_task();
    start_net_rx_task();
    start_uart_rx_task();
}
