#ifndef CAN_H
#define CAN_H
#pragma once
	
#include "driver/twai.h"

#define MY 1

#ifdef MY
#define TX_GPIO_NUM             GPIO_NUM_22
#define RX_GPIO_NUM             GPIO_NUM_21
#else
#define TX_GPIO_NUM             GPIO_NUM_21
#define RX_GPIO_NUM             GPIO_NUM_22
#endif

class can  
{
	private:

	public:

		can();
		~can();
		
		static twai_timing_config_t t_config;
		static twai_filter_config_t f_config;
		static twai_general_config_t g_config;

		static void start();
		static void stop();
		static void transmit(twai_message_t &msg);
		static esp_err_t receive(twai_message_t &msg);
};
#endif