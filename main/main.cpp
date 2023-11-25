// #include "mesh.hpp"
#include "can.hpp"
#include "http.hpp"
#include "uart.hpp"
#include "thread_ctl.hpp"

#include <map>
#include <atomic>

#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "MeshNet.hpp"
#include "TmpNet.hpp"
#include "mdns.h"

#include "driver/gpio.h"

#include "driver/twai.h"

#include "inttypes.h"

#include "twai_filter.hpp"
#include "dtp_message.hpp"

#include "work/work.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

#include "esp_spiffs.h"
#include "FirmwareUpdate.hpp"
#include "indication.hpp"
#include "Signal.h"

extern "C" void app_main(void);

using namespace rutils;

#define DEVICE_ID 28

#define CF_UART_ENABLED 1
#define CF_MESH_ENABLED 1

#define MSG_SIZE 32
#define TX_WEB_QUEUE_SIZE 64
#define TX_MESH_QUEUE_SIZE 32
#define SEND_WORK_TIME 100

#define TASK_CONTROL_DELAY 10

#define UART_TXD_PIN (GPIO_NUM_4)
#define UART_RXD_PIN (GPIO_NUM_5)


enum class app_mode_source_t {
     APP_MODE_SOURCE_GPIO,
     APP_MODE_SOURCE_NVS
};

app_mode_source_t app_mode_source = app_mode_source_t::APP_MODE_SOURCE_NVS;

#define MODE_SELECT_PIN0 GPIO_NUM_4
#define MODE_SELECT_PIN1 GPIO_NUM_12

enum class app_mode_t { 
    APP_MODE_NONE = -1,
    APP_MODE_DIAGNOSTIC,
    APP_MODE_DIAGNOSTIC_MESH,
    APP_MODE_APPLICATION
};

const char * app_mode_names[] = { "APP_MODE_NONE", "APP_MODE_DIAGNOSTIC", "APP_MODE_DIAGNOSTIC_MESH", "APP_MODE_APPLICATION" };

app_mode_t app_mode = app_mode_t::APP_MODE_NONE;

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
#define TEST_MSG_SEND_PERIOD 300000
std::atomic<uint32_t> _test_msg_send_period(TEST_MSG_SEND_PERIOD);

uint32_t _can_msg_id = 0;
uint8_t _msgs_buff[MESH_MPS];

void twai_rx(void *arg = nullptr);

const esp_timer_create_args_t periodic_timer_args = {
        .callback = &twai_rx,
        .name = "periodic"
};

esp_timer_handle_t periodic_timer;

uart *_uart;
INetwork *net;

#ifdef SIGNAL_MONITOR
Signal *raw_signal;
thread_ctl signal_thread(signal_rx_task, false, "SignalRX", NULL, 0x1000, false, 9);
#endif

indication ind(GPIO_NUM_2);

static const char *LTAG = "cmb";


void run_mdns();
void stop_mdns();

void on_self_is_root();
void on_net_connected();
void on_net_disconnected();
void print_twai_msg(twai_message_t &msg);
void print_message(dtp_message &data);
char* sprint_twai_msg(twai_message_t &msg, char* out_buf);

void PrintTasksState();

void twai_rx_task(thread_funct_args& args);
void signal_rx_task(thread_funct_args& args);


thread_ctl twai_thread(twai_rx_task, false, "TwaiRX", NULL, 16384, false, 9);


void TaskDelay(uint16_t ms)
{
    vTaskDelay(1 * ms / portTICK_PERIOD_MS);
    // vTaskDelay(1 * 1000 / ( ( TickType_t ) 1000 / 100 ));
    
}

void web_send_work(void *) {
    static uint8_t data[MSG_SIZE];

    int64_t count_down = SEND_WORK_TIME * 1000;

    while (count_down > 0) 
    {
        auto t0 = esp_timer_get_time();

        if ( xQueueReceive(_tx_web_queue, data, count_down / 1000 / portTICK_PERIOD_MS ) == pdPASS)
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
        // ESP_LOGE(LTAG, "_tx_web_queue full");
    ;

    if (http::is_ready() && _web_send_work_done) {
        _web_send_work_done = false;
        // while (http::queue_work(web_send_work) != ESP_OK) {
        if (http::queue_work(web_send_work) != ESP_OK) {
            _web_send_work_done = true;

            ESP_LOGE(LTAG, "work start failed");
            // vTaskDelay(1);
        }
    }
}

void net_tx(dtp_message& msg)
{
    net->send((uint8_t*)&msg, MSG_SIZE);
    // if (xQueueSend(_tx_mesh_queue, &msg, portMAX_DELAY) != pdPASS)
        // ESP_LOGE(LTAG, "_tx_web_queue full");
    ;

    // _uart->send((uint8_t*)&msg, MSG_SIZE);
}

void twai_rx_task(thread_funct_args& args) 
{
    while (!args._exit_signal) 
    {
        twai_rx();
    }
}

#ifdef SIGNAL_MONITOR
void signal_rx_task(thread_funct_args& args) 
{
    uint8_t buf[sizeof(msg_signal) + 64 * sizeof(rmt_symbol_word_t)];
    msg_signal &msg = *new(buf) msg_signal;
    rmt_symbol_word_t *words = (rmt_symbol_word_t*)&msg.bytes_ptr;

    msg.set_dev_id(DEVICE_ID);

    size_t data_len;

    while (!args._exit_signal) 
    {
        data_len = sizeof(buf);

        if (!raw_signal->receive((rmt_symbol_word_t*)&msg.bytes_ptr, &data_len, portMAX_DELAY)) {
            vTaskDelay(1);
            continue;
        }

        printf("NEC frame start---\r\n");
        for (size_t i = 0; i < data_len; i++) {
            printf("{%d:%d},{%d:%d}\r\n", words[i].level0, words[i].duration0,
                words[i].level1, words[i].duration1);
        }
        printf("---NEC frame end: \r\n");
        // uint8_t *buf = new uint8_t[sizeof(msg_signal) + data->num_symbols * sizeof(rmt_symbol_word_t)]; 
        // msg.num_bytes = data->num_symbols;
        // msg.
        msg.num_bytes = data_len;
        net->send((uint8_t*)&msg, MSG_SIZE);

    }
}
#endif

void ui_rx_task(void *arg)
{
    uint8_t _rx_buf[MESH_MPS] = { 0 };

    while (1)
    {
        size_t len;
        if (!http::receive(_rx_buf, &len, portMAX_DELAY /*TASK_CONTROL_DELAY*/))
            continue;

        net_tx(*((dtp_message*)_rx_buf));
        ESP_LOGI("web_rx", "msg: %s", (char *)_rx_buf);
    }
    
    vTaskDelete(NULL);
}

void proccess_can_data(const msg_can_data *message)
{
    static int data_size = 0, pcount = 0;
    static int pkt_lost = 0;
    static std::map<uint8_t, int32_t> last_pkt_ids;

    msg_can_data msg = *message;
    
    auto dev_id = msg.get_dev_id();
    auto id = msg.get_id();

    // printf("mt: %d    dev_id: %d\n", msg->get_type(), dev_id);
    
    msg.twai_msg.data[7] = ((uint8_t*)&(msg.twai_msg.flags))[3];
    msg.twai_msg.flags &= 1;
    msg.twai_msg.ss = 1;;

{        
    // msg->twai_msg.extd = 1;
    // msg.twai_msg.identifier = 0x22;
}
    can::transmit(msg.twai_msg);

{
    pkt_lost += (id - last_pkt_ids[dev_id] - 1);
    last_pkt_ids[dev_id] = id;

    data_size += MSG_SIZE;
    pcount++;

    auto t0 = esp_timer_get_time();
    static int64_t tl = 0;

    if (t0 - tl > 1000000){
        ESP_LOGW("proccess_can_data", " count: %d     rate: %d      pkt_lost: %d", pcount, data_size, pkt_lost);
        data_size = pcount = pkt_lost = 0;
        tl = t0;
    }        
}
}

void send_status()
{
    msg_info minfo;
    minfo.set_dev_id(DEVICE_ID);
    net->send((uint8_t*)&minfo, MSG_SIZE);
}

void proccess_message(const dtp_message *msg)
{
    bool unknown_type = false;

    switch (msg->get_type())
    {
    case DTPT_CAN_DATA:
        proccess_can_data((msg_can_data*)msg);
        break;
    case DTPT_REQUEST: 
        send_status();
        break;
    case DTPT_INFO:
        break;
    default:
        unknown_type = true;
        ESP_LOGW("net_rx_task", "unknown message type");
        break;
    }
}

void net_rx_task(void *arg)
{
    uint16_t len;
    uint8_t msg_buf[MESH_MPS];

    while (1) {
        len = MESH_MPS;
        while (!net->receive(msg_buf, &len)) 
            vTaskDelay(1);
    
        int msg_count = len / MSG_SIZE;
        int msg_n = 0;

        while (msg_count--) 
        {
            auto msg = (dtp_message*)(msg_buf + msg_n++ * MSG_SIZE);

            if (msg->get_dev_id() != DEVICE_ID)
                proccess_message(msg);

            auto t = msg->get_type();
            if (t == DTPT_CAN_DATA || t == DTPT_INFO)
                if (net->is_root())
                    web_tx(*msg);
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

// ---------------------
        // if (err == ESP_OK)
        // {
        //     auto t0 = esp_timer_get_time();
        //     static int64_t tl = t0;

        //     printf("%8lld    ", t0 - tl);
        //     print_twai_msg(msg);

        //     tl = t0;
        // }
// ---------------------

        return err == ESP_OK || err == ESP_ERR_TIMEOUT;
    }
}

void twai_rx(void* arg)
{
    int i = 0;
    for (int n = 1; n--;)
    {
        msg_can_data &msg = *new(_msgs_buff + i * MSG_SIZE) msg_can_data();

        if (!twai_msg_receive(msg.twai_msg))
            break;

        if (!i)
            n = _can_test_mode ? 0 : can::get_msgs_to_rx();

        if (!twai_in_filter(msg.twai_msg))
            continue;

        if (!(_can_msg_id % 10))
            ind.flash();

        msg.set_dev_id(DEVICE_ID);
        msg.set_id(_can_msg_id++);

        if (msg.twai_msg.data_length_code == 8)
            ((uint8_t*)(&msg.twai_msg.flags))[3] = msg.twai_msg.data[7];
        
        i++;
    }

    if (!i) {
        vTaskDelay(1);
        return;
    }

    net->send((uint8_t*)_msgs_buff, i * MSG_SIZE);

{
        auto t0 = esp_timer_get_time();
        static int64_t tl = t0;
        static int pcount = 0;
        static int msg_count = 0;
        static int out_of_buff = 0;
        static int buff_msg_max = 0;
        pcount++;
        msg_count += i;
        out_of_buff += i == 46 ? 1 : 0;
        if (buff_msg_max < i)
            buff_msg_max = i;

        if (t0 - tl > 1000000){
            ESP_LOGW("twai_rx", "pkt_count: %d   msg_count: %d  buff_msg_max: %d   out_of_buff: %d   [%s]", pcount, msg_count, buff_msg_max, out_of_buff, sprint_twai_msg(((msg_can_data*)_msgs_buff)->twai_msg, nullptr));
            pcount = 0;
            tl = t0;
            out_of_buff = 0;
            msg_count = 0;
            buff_msg_max = 0;
        }
}
}

// void mesh_send_task(void *arg)
// {
//     uint8_t data[MSG_SIZE];

//     while (1)
//     {
//         if ( xQueueReceive(_tx_mesh_queue, data,  portMAX_DELAY ) == pdPASS) {
//             net->send(data, MSG_SIZE);
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
        // xTaskCreatePinnedToCore(twai_rx_task, "TwaiRX", 4096, NULL, 7, NULL, tskNO_AFFINITY);
        twai_thread.run();
}

void start_web_rx_task()
{
    // _web_rx_task_run = true;
    xTaskCreatePinnedToCore(ui_rx_task, "WebRX", 8192, NULL, 7, NULL, tskNO_AFFINITY);
}

void stop_web_rx_task()
{
    // _web_rx_task_run = false;
}

void start_net_rx_task()
{
    xTaskCreatePinnedToCore(net_rx_task, "NetRX", 16384, NULL, 7, NULL, tskNO_AFFINITY);
}

void start_uart_rx_task()
{
    xTaskCreatePinnedToCore(uart_rx_task, "UartRX", 4096, NULL, 8, NULL, tskNO_AFFINITY);
}

void on_self_is_root()
{
}

void on_net_connected()
{
    ind.blink_config(net->is_root()?1:2);

    if (!net->is_root()) {
        send_status();
        return;
    }

    http::start();
    run_mdns();

    // start_web_rx_task();
}

void on_net_disconnected()
{
    ind.blink_config(0);

    if (!net->is_root())
        return;

    http::stop();
    stop_mdns();

    // stop_web_rx_task();
}

void run_mdns() {
    esp_err_t err = mdns_init();
    if (err) {
		ESP_LOGE(LTAG, "Error setting up MDNS responder!");
        return;
    }

    mdns_hostname_set("cmb");
    mdns_instance_name_set("CMB");

	ESP_LOGE(LTAG, "MDNS Started.");
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
        ESP_LOGW(LTAG, "heep size: %lu    is ISR: %d       rate: %d", esp_get_free_heap_size(), xPortInIsrContext(), c);
        c = 0;
        tl = t0;
    }

    //     auto t0 = esp_timer_get_time();
    // static int64_t tl = t0;

    // if (t0 - tl > 1000000){
    //     ESP_LOGW(LTAG, "heep size: %d    messages: %d"
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
    ESP_LOGI("on_init", "portTICK_RATE_MS: %lu", 1 * 1000 / portTICK_PERIOD_MS);
    ESP_LOGI("on_init", "Task priorities: %d", configMAX_PRIORITIES);
    // ESP_LOGI("", "Task priorities: %d", esp_meh);
}

app_mode_t get_app_mode()
{
    uint8_t mode = 0;

    if (app_mode_source == app_mode_source_t::APP_MODE_SOURCE_NVS)
        return app_mode_t::APP_MODE_DIAGNOSTIC_MESH;

    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << MODE_SELECT_PIN0) | (1ULL << MODE_SELECT_PIN1)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    if (!gpio_get_level(MODE_SELECT_PIN0)) mode |= 1 << 0;
    if (!gpio_get_level(MODE_SELECT_PIN1)) mode |= 1 << 1;

    gpio_reset_pin(MODE_SELECT_PIN0);
    gpio_reset_pin(MODE_SELECT_PIN1);

    switch (mode)
    {
    case 0: return app_mode_t::APP_MODE_APPLICATION;
    case 1: return app_mode_t::APP_MODE_DIAGNOSTIC;
    case 2: return app_mode_t::APP_MODE_DIAGNOSTIC_MESH;    
    default:
        return app_mode_t::APP_MODE_DIAGNOSTIC;
    }
}

void init_spiffs()
{
    ESP_LOGI(LTAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/httpsrc",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(LTAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(LTAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(LTAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
}

void init()
{
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    app_mode = get_app_mode();

    while (!DEVICE_ID) {
        ESP_LOGE(LTAG, "DEVICE_ID not set...");
        TaskDelay(1000);
    }

    on_init();

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void init_net()
{
    ESP_ERROR_CHECK(esp_netif_init());

    switch (app_mode)
    {
    case app_mode_t::APP_MODE_DIAGNOSTIC:
        net = new TmpNet;
        break;
    case app_mode_t::APP_MODE_DIAGNOSTIC_MESH:
        net = new MeshNet;
        break;
    default:
        break;
    }

                                    // ??????? Wrong parameter order
    _tx_web_queue = xQueueCreate(TX_WEB_QUEUE_SIZE, MSG_SIZE);

    // _tx_mesh_queue = xQueueCreate(MSG_SIZE, TX_MESH_QUEUE_SIZE);

    net->OnIsRootCallbackRegister(on_self_is_root);
    net->OnIsConnectedCallbackRegister(on_net_connected);
    net->OnIsDisconnectedCallbackRegister(on_net_disconnected);
}

void run_application()
{
    work::run();
}

void app_main(void)
{
    init();

#ifndef SIGNAL_MONITOR
    can can(GPIO_NUM_21, GPIO_NUM_22);
    // can can(GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19);

    can::start(can::speed_t::_100KBITS, TWAI_MODE_NORMAL);
#endif

    ESP_LOGI(LTAG, "application mode %s\n", app_mode_names[(int)app_mode + 1]);

    if (app_mode == app_mode_t::APP_MODE_APPLICATION)
    {
        run_application();

        return;
    }

#ifdef SIGNAL_MONITOR
    raw_signal = new Signal(GPIO_NUM_22, 10000000, 9000, 51000);
    signal_thread.run();
#endif

    init_spiffs();
    init_net();

    http *http_server = new http();
    FirmwareUpdate *update = new FirmwareUpdate(http_server);

    net->start();

    // _uart = new uart(UART_NUM_1, UART_RXD_PIN, UART_TXD_PIN);
    // _uart->start();

    start_net_rx_task();
    // start_uart_rx_task();

#ifndef SIGNAL_MONITOR
    start_twai_rx_task();
#endif

    start_web_rx_task();

}
