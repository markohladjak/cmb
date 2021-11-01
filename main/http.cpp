#include "http.hpp"  

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include <atomic>

#define RX_SIZE          (1500)

httpd_handle_t http::_server = nullptr;
SemaphoreHandle_t http::_send_mutex  = xSemaphoreCreateMutex();
SemaphoreHandle_t http::_send_task_sem  = xSemaphoreCreateMutex();
QueueHandle_t http::_rx_task_queue = xQueueCreate(1, RX_SIZE);

uint8_t http::_rx_buf[RX_SIZE] = { 0 };

std::map<int, bool> http::_wscs;
bool http::_is_ready = false;
std::atomic<int32_t> works_count(0);

httpd_ws_frame_t http::_ws_pkt = {
    .final = 0,
    .fragmented = 0,
    .type = HTTPD_WS_TYPE_BINARY,
    .payload = _rx_buf,
    .len = 18
};

const httpd_uri_t http::_ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = http::request_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static const char *TAG = "cmb";

esp_err_t http::httpd_open_func(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "httpd_open_func ID: %d", sockfd);
    
    _wscs[sockfd] = false;

    return ESP_OK;
}

void http::httpd_close_func(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "httpd_close_func ID: %d", sockfd);

    _wscs.erase(sockfd);
    update_ready_state();
}

void http::start_webserver()
{
    _is_ready = false;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    config.open_fn = httpd_open_func;
    config.close_fn = httpd_close_func;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting server!");
        _server = nullptr;
        return;
    }

    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(_server, &_ws_uri);
}

void http::stop_webserver()
{
    ESP_LOGI(TAG, "Stopping webserver");
    _is_ready = false;

    httpd_stop(_server);

    _server = nullptr;
}

void http::connect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (_server) return;

    start_webserver();
}

void http::disconnect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (!_server) return;

    stop_webserver();
}

esp_err_t http::request_handler(httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = _rx_buf;
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, RX_SIZE);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Got packet type: %d  with message: %s", ws_pkt.type, ws_pkt.payload);
    
    _wscs[httpd_req_to_sockfd(req)] = true;
    update_ready_state();

    xQueueSend(_rx_task_queue, _rx_buf, 0/*portMAX_DELAY*/);

    return ret;
}

void http::update_ready_state()
{
    _is_ready = false;
    
    for (auto &fd: _wscs)
        _is_ready = _is_ready || fd.second;
}

void http::ws_async_send(void *arg)
{
    // httpd_ws_send_frame_async(_server, _sockfd, &_ws_pkt);
}

void http::start(void)
{
    if (_server)
        return;

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &_server));

    start_webserver();
}

void http::stop()
{
    if (!_server)
        return;
        
    stop_webserver();

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler));
}

bool http::is_ready()
{
    return _is_ready;
}

bool http::send(uint8_t *data, size_t len)
{
    xSemaphoreTake(_send_mutex, portMAX_DELAY);

    if (!is_ready() ||
        !_server ||
        !_wscs.size() ||
        httpd_ws_get_fd_info(_server, _wscs.begin()->first) != HTTPD_WS_CLIENT_WEBSOCKET)
    {
        xSemaphoreGive(_send_mutex);
        return false;
    }

    esp_err_t err = httpd_queue_work(_server, [](void *arg){
        auto pkt = new httpd_ws_frame_t {
            .final = 0,
            .fragmented = 0,
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = (uint8_t*)arg,
            .len = 128
        };

        // auto pkt = (httpd_ws_frame_t*)arg;

        for (int i=0; i<1000; i++) {
            auto t0 = esp_timer_get_time();
            static int64_t tl = t0;

            esp_err_t e = httpd_ws_send_frame_async(_server, _wscs.begin()->first, pkt);

            if (t0 - tl > 1000000){
                auto t1 = esp_timer_get_time();
                ESP_LOGW("MMM", "httpd_ws_send_frame_async delta: %" PRIu64 "  wk: %d", t1 - t0, (int32_t)works_count);
                tl = t0;
            }

            if (e) {
                ESP_LOGE("MMM", "httpd_ws_send_frame_async error: %d", e);
                break;
            }
        }

        works_count = works_count - 1;

        // delete[] pkt->payload;
        delete pkt;
    }, data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_queue_work error: %d", err);
        // delete pkt;
    } else{
        works_count = works_count + 1;
    }

    xSemaphoreGive(_send_mutex);

    return err == ESP_OK;
}

void http::send_async(uint8_t *data, size_t len)
{
    static auto pkt = new httpd_ws_frame_t {
        .final = 0,
        .fragmented = 0,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = data,
        .len = len
    };

    esp_err_t err = httpd_ws_send_frame_async(_server, _wscs.begin()->first, pkt);

    if (err != ESP_OK) 
        ESP_LOGE("cmb", "send_async error %d", err);
}

void http::receive(uint8_t *data, size_t *len)
{
    xQueueReceive(_rx_task_queue, data, portMAX_DELAY);
}

esp_err_t http::queue_work(void (*work_fn)(void *arg))
{
    if (!is_ready() ||
        !_server ||
        !_wscs.size() ||
        httpd_ws_get_fd_info(_server, _wscs.begin()->first) != HTTPD_WS_CLIENT_WEBSOCKET) {
            return ESP_FAIL;
        }

    return httpd_queue_work(_server, work_fn, nullptr);
}
