#include "uart.hpp"  

#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "string.h"
#include "driver/gpio.h"

static const int TX_BUF_SIZE = 1024;
static const int RX_BUF_SIZE = 1024;

#define MSG_SIZE 32
#define TX_QUEUE_SIZE 32
#define RX_QUEUE_SIZE 32

uart::uart(int port_num, int tx_pin, int rx_pin)
    : _port_num(port_num)
    , _tx_pin(rx_pin)
    , _rx_pin(tx_pin)
{
    _tx_break = false;
    _rx_break = false;

    // _config.baud_rate = 115200;
    _config.baud_rate = 921600;
    _config.data_bits = UART_DATA_8_BITS;
    _config.parity = UART_PARITY_DISABLE;
    _config.stop_bits = UART_STOP_BITS_1;
    _config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    _config.source_clk = UART_SCLK_APB;

    _tx_queue = xQueueCreate(MSG_SIZE, TX_QUEUE_SIZE);
    _rx_queue = xQueueCreate(MSG_SIZE, RX_QUEUE_SIZE);

    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
}
	
uart::~uart()
{
	
}

void uart::start(int tx_task_pri, int rx_task_pri)
{
    ESP_ERROR_CHECK(uart_driver_install(_port_num, RX_BUF_SIZE * 2, TX_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(_port_num, &_config));
    ESP_ERROR_CHECK(uart_set_pin(_port_num, _tx_pin, _rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(tx_task, "uart_tx_task", 1024*4, this, tx_task_pri, NULL);
    xTaskCreate(rx_task, "uart_rx_task", 1024*4, this, rx_task_pri, NULL);
}

void uart::stop()
{
    _tx_break = true;
    _rx_break = true;

    uart_driver_delete(_port_num);
}

void uart::send(uint8_t *data, size_t size)
{
    xQueueSend(_tx_queue, data, 0);
}

void uart::read(uint8_t *data)
{
    xQueueReceive(_rx_queue, data, portMAX_DELAY);
}

void uart::tx_task(void *arg)
{
    auto o = (uart*)arg;
    uint8_t msg[MSG_SIZE];
    int bytes_left;

    while (!o->_tx_break) {
        if (xQueueReceive(o->_tx_queue, msg, 100 / portTICK_RATE_MS ) == pdPASS)
        {
            bytes_left = MSG_SIZE;

            while( bytes_left > 0) {
                int b = uart_write_bytes(o->_port_num, msg, bytes_left);

                bytes_left -= b;
                ESP_LOGE("MUART", "write %d", b);
            }
        } 
        else
            vTaskDelay(1);
    }

    o->_tx_break = false;

    vTaskDelete(NULL);
}

void uart::rx_task(void *arg)
{
    auto o = (uart*)arg;
    auto msg = new uint8_t[RX_BUF_SIZE + MSG_SIZE + 1];
    int bytes = 0;
    int bytes_left = 0;
    int pkts = 0;

    while (!o->_rx_break) {
        int c = uart_read_bytes(o->_port_num, msg + bytes, RX_BUF_SIZE, 100 / portTICK_RATE_MS);

        if (c <= 0) {
            vTaskDelay(1);
            continue;
        }

        bytes += c;

        ESP_LOGE("MUART", "read %d", bytes);


        pkts = bytes / MSG_SIZE;
        bytes_left %= MSG_SIZE;

        for (int i = 0; i < pkts; ++i)
            xQueueSend(o->_rx_queue, msg + MSG_SIZE * 0, 0);

        if (pkts) {
            memcpy(msg, msg + (bytes - bytes_left), bytes_left);
            bytes = bytes_left;
        }
            // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            // ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
    }

    delete[] msg;

    o->_rx_break = false;

    vTaskDelete(NULL);
}
