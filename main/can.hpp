#ifndef CAN_H
#define CAN_H
#pragma once
	
#include "driver/twai.h"

class can  
{
	static gpio_num_t _tx_gpio_num;
	static gpio_num_t _rx_gpio_num;

	static twai_timing_config_t *t_config;
	static twai_filter_config_t f_config;
	static twai_general_config_t g_config;

public:
	enum speed_t {
		_25KBITS,
		_50KBITS,
		_100KBITS,
		_125KBITS,
		_250KBITS,
		_500KBITS,
		_800KBITS,
		_1MBITS
	};

	can(gpio_num_t tx_gpio_num, gpio_num_t rx_gpio_num);
	~can();

	static void start(speed_t speed = speed_t::_500KBITS);
	static void stop();
	static void transmit(twai_message_t &msg);
	static esp_err_t receive(twai_message_t &msg);
};
#endif