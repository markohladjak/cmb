#ifndef CAN_H
#define CAN_H
#pragma once
	
#include "driver/twai.h"

#define TWAI_TIMING_CONFIG_83_3KBITS()   {.brp = 48, .tseg_1 = 15, .tseg_2 = 4, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_80KBITS()   {.brp = 50, .tseg_1 = 15, .tseg_2 = 4, .sjw = 3, .triple_sampling = false}

class can  
{
	static struct transceiver_config_t
	{
		gpio_num_t tx, rx, err, stb, en;
		bool ft;
	} transceiver_config;
	
	static twai_timing_config_t *t_config;
	static twai_filter_config_t f_config;
	static twai_general_config_t g_config;

public:
	enum speed_t {
		_25KBITS,
		_50KBITS,
		_80KBITS,
		_83_3KBITS,
		_100KBITS,
		_125KBITS,
		_250KBITS,
		_500KBITS,
		_800KBITS,
		_1MBITS
	};

	can(gpio_num_t tx, gpio_num_t rx);
	can(gpio_num_t tx, gpio_num_t rx, gpio_num_t err, gpio_num_t stb, gpio_num_t en);
	~can();

	static void init(gpio_num_t tx, gpio_num_t rx, bool ft = false, 
					gpio_num_t err = GPIO_NUM_NC, gpio_num_t stb = GPIO_NUM_NC, gpio_num_t en = GPIO_NUM_NC);
	
	static void ft_init();
	static void ft_start();

	static void start(speed_t speed = speed_t::_500KBITS, twai_mode_t mode = TWAI_MODE_NORMAL);
	static void stop();
	static void transmit(twai_message_t &msg);
	static esp_err_t receive(twai_message_t &msg);

	static int get_msgs_to_rx();

	static void alerts_task(void *arg);
};
#endif