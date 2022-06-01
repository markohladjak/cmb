#include "can.hpp"  
#include "esp_err.h"
#include "esp_log.h"

twai_timing_config_t *can::t_config = nullptr;
twai_filter_config_t can::f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
twai_general_config_t can::g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_NC, GPIO_NUM_NC, TWAI_MODE_NORMAL);

gpio_num_t can::_tx_gpio_num = GPIO_NUM_NC;
gpio_num_t can::_rx_gpio_num = GPIO_NUM_NC;

can::can(gpio_num_t tx_gpio_num, gpio_num_t rx_gpio_num)
{
    _tx_gpio_num = tx_gpio_num;
    _rx_gpio_num = rx_gpio_num;

    g_config.tx_io = tx_gpio_num;
    g_config.rx_io = rx_gpio_num;

    g_config.tx_queue_len = 46;
    g_config.rx_queue_len = 46;
}
	
can::~can()
{
    if (t_config) 
        delete t_config;

    t_config = nullptr;
}
		
void can::start(speed_t speed)
{
    if (t_config)
        stop();

    switch (speed)
    {
    case speed_t::_25KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_25KBITS(); break;
    case speed_t::_50KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_50KBITS(); break;
    case speed_t::_100KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_100KBITS(); break;
    case speed_t::_125KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_125KBITS(); break;
    case speed_t::_250KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_250KBITS(); break;
    case speed_t::_500KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_500KBITS(); break;
    case speed_t::_800KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_800KBITS(); break;
    case speed_t::_1MBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_1MBITS(); break;

    default:
        break;
    }

    ESP_ERROR_CHECK(twai_driver_install(&g_config, t_config, &f_config));
    ESP_LOGI("", "Driver installed");

    ESP_ERROR_CHECK(twai_start());
}

void can::stop()
{
    ESP_ERROR_CHECK(twai_stop());

    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI("", "Driver uninstalled");
}

void can::transmit(twai_message_t &msg)
{
    auto err = twai_transmit(&msg, 0);
    // if (err)
    //     printf("twai_transmit: %s\n", esp_err_to_name(err));
}

esp_err_t can::receive(twai_message_t &msg)
{
    return twai_receive(&msg, portMAX_DELAY);
}

int can::get_msgs_to_rx()
{
    twai_status_info_t info;
    twai_get_status_info(&info);

    return info.msgs_to_rx;
}
