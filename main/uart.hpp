#ifndef UART_H
#define UART_H
#pragma once

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#include <atomic>

// struct task_ctx {
// 	int port;
// 	QueueHandle_t _tx_queue;
// 	QueueHandle_t _rx_queue;
// };

class uart  
{
private:
	uart_config_t _config;

	int _port_num;
	int _tx_pin;
	int _rx_pin;
	
	static void tx_task(void *arg);
	static void rx_task(void *arg);
	std::atomic<bool> _tx_break;
	std::atomic<bool> _rx_break;

	QueueHandle_t _tx_queue;
	QueueHandle_t _rx_queue;

public:

	uart(int port_num, int rx_pin, int tx_pin);
	~uart();

	void start(int tx_task_pri = 11, int rx_task_pri = 10);
	void stop();

	void send(uint8_t *data, size_t size);
	void read(uint8_t *data);
};
#endif