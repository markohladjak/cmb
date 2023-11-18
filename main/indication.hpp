#ifndef INDICATION_H
#define INDICATION_H
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

class indication
{
    gpio_num_t _gpio_num;
    static int old_count;

public:
    indication(gpio_num_t led_gpio_num);
    ~indication();
    void blink_config(int count);
    void flash();
};

#endif