#ifndef HTTP_H
#define HTTP_H
#pragma once

#include <cstddef>
#include <inttypes.h>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
}

#include <esp_http_server.h>
#include <esp_event.h>
#include <map>

typedef void* httpd_handle_t;

class http  
{
private:
	static httpd_ws_frame_t _ws_pkt;
	static const httpd_uri_t _ws_uri;

	static uint8_t _rx_buf[];
	static std::map<int, bool> _wscs;

	static bool _is_ready;

	static httpd_handle_t _server;
	static SemaphoreHandle_t _send_mutex;
	static SemaphoreHandle_t _send_task_sem;

	static QueueHandle_t _rx_task_queue;

	static esp_err_t httpd_open_func(httpd_handle_t hd, int sockfd);
	static void httpd_close_func(httpd_handle_t hd, int sockfd);
	static void start_webserver();
	static void stop_webserver();

	static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
	static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);

	static esp_err_t request_handler(httpd_req_t *req);
	static void update_ready_state();

	static void ws_async_send(void *arg);
public:

	http();
	~http();

	static void start();
	static void stop();
	static bool is_ready();
	static bool send(uint8_t *data, size_t len);
	static void send_async(uint8_t *data, size_t len);
	static void receive(uint8_t *data, size_t *len);

	static esp_err_t queue_work(void (*work_fn)(void *arg));
};
#endif