#include "Signal.h"

static const char *TAG = "cmb";


Signal::Signal(gpio_num_t gpio_num, uint32_t resolution_hz, uint32_t signal_range_min_ns, uint32_t signal_range_max_ns):
    _gpio_num(gpio_num),
    _resolution_hz(resolution_hz)
{
    _receive_config.signal_range_min_ns = signal_range_min_ns;
    _receive_config.signal_range_max_ns = signal_range_max_ns;
    
    init();
}

Signal::~Signal()
{
    reset();
}

void Signal::reset()
{
    rmt_del_channel(_rx_channel);
    vQueueDelete(_receive_queue);
}

void Signal::init(gpio_num_t gpio_num, uint32_t resolution_hz, uint32_t signal_range_min_ns, uint32_t signal_range_max_ns)
{
    _gpio_num = gpio_num;
    _resolution_hz = resolution_hz;
    _receive_config.signal_range_min_ns = signal_range_min_ns;
    _receive_config.signal_range_max_ns = signal_range_max_ns;

    init();
}

void Signal::init()
{
    ESP_LOGI(TAG, "create RMT RX channel");
    rmt_rx_channel_config_t rx_channel_cfg = {
        .gpio_num = _gpio_num,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = _resolution_hz,
        .mem_block_symbols = 128,
        // .flags.with_dma = 1
    };
    
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &_rx_channel));

    ESP_LOGI(TAG, "register RX done callback");
    _receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    assert(_receive_queue);
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(_rx_channel, &cbs, _receive_queue));

    ESP_LOGI(TAG, "enable RMT TX and RX channels");
    ESP_ERROR_CHECK(rmt_enable(_rx_channel));
}

// rmt_rx_done_event_data_t* Signal::receive(TickType_t ticks_to_wait)
// {
//     if (!uxQueueMessagesWaiting(_receive_queue))
//         ESP_ERROR_CHECK(rmt_receive(_rx_channel, _raw_symbols, sizeof(_raw_symbols), &_receive_config));

//     if (xQueueReceive(_receive_queue, &_rx_data, ticks_to_wait) == pdPASS)
//         return &_rx_data;

//     return nullptr;
// }

bool Signal::receive(rmt_symbol_word_t *buffer, size_t *data_len, TickType_t ticks_to_wait)
{
    for (int i=0; i<32; i++) {
        buffer[i].level0 = 0;
        buffer[i].level1 = 1;
        buffer[i].duration0 =  4 * (rand() % 5 + 1) * 10;
        buffer[i].duration1 =  4 * (rand() % 5 + 1) * 10;
    }

    *data_len = 32;
    return true;

    if (!uxQueueMessagesWaiting(_receive_queue))
        ESP_ERROR_CHECK(rmt_receive(_rx_channel, buffer, *data_len, &_receive_config));

    if (xQueueReceive(_receive_queue, &_rx_data, ticks_to_wait) == pdPASS) {
        *data_len = _rx_data.num_symbols;
        return true;
    }

    return false;
}

bool Signal::rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_ctx;

    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    
    return high_task_wakeup == pdTRUE;
}
