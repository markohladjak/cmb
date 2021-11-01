#include "can.hpp"  
// #include <stdio.h>
// #include <stdlib.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"

twai_timing_config_t can::t_config = TWAI_TIMING_CONFIG_100KBITS();
twai_filter_config_t can::f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
twai_general_config_t can::g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);

can::can()
{
	
}
	
can::~can()
{
	
}

void can::start()
{
    //Install TWAI driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI("", "Driver installed");

    ESP_ERROR_CHECK(twai_start());
}

void can::stop()
{
    ESP_ERROR_CHECK(twai_stop());

    //Uninstall TWAI driver
    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI("", "Driver uninstalled");
}

void can::transmit(twai_message_t &msg)
{
    twai_transmit(&msg, portMAX_DELAY);
}

esp_err_t can::receive(twai_message_t &msg)
{
    return twai_receive(&msg, portMAX_DELAY);
}
