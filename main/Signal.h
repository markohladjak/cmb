#ifndef SIGNAL_H
#define SIGNAL_H
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"

class Signal
{
    rmt_symbol_word_t _raw_symbols[64];
    rmt_rx_done_event_data_t _rx_data;

    rmt_channel_handle_t _rx_channel = NULL;
    gpio_num_t _gpio_num;
    uint32_t _resolution_hz;
    rmt_receive_config_t _receive_config;
    
    QueueHandle_t _receive_queue;

    static bool rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx);

    void init();

public:
    Signal(gpio_num_t gpio_num, uint32_t resolution_hz, uint32_t signal_range_min_ns, uint32_t signal_range_max_ns);
    ~Signal();

    void init(gpio_num_t gpio_num, uint32_t resolution_hz, uint32_t signal_range_min_ns, uint32_t signal_range_max_ns);
    void reset();

    // rmt_rx_done_event_data_t* receive(TickType_t ticks_to_wait);
    bool receive(rmt_symbol_word_t *buffer, size_t *data_len, TickType_t ticks_to_wait);
};

#endif