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

#include "Script.h"

#define RX_SIZE          (1500)

#define DATA_PATH   "/storage/ui/"

httpd_handle_t http::_server = nullptr;
SemaphoreHandle_t http::_send_mutex  = xSemaphoreCreateMutex();
SemaphoreHandle_t http::_send_task_sem  = xSemaphoreCreateMutex();
QueueHandle_t http::_rx_task_queue = xQueueCreate(1, RX_SIZE);
http::_upload_handler http::upload_handler { nullptr, nullptr };

uint8_t http::_rx_buf[RX_SIZE] = { 0 };

std::map<int, bool> http::_wscs;

int http::_sockfd = -1;

bool http::_is_ready = false;
std::atomic<int32_t> works_count(0);

// httpd_ws_frame_t http::_ws_pkt = {
//     .final = 0,
//     .fragmented = 0,
//     .type = HTTPD_WS_TYPE_BINARY,
//     .payload = _rx_buf,
//     .len = 18
// };

const httpd_uri_t http::_ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = http::request_handler_ws,
    .user_ctx   = NULL,
    .is_websocket = true
};

const httpd_uri_t http::_http_uri = {
    .uri        = "/",
    .method     = HTTP_GET,
    .handler    = http::request_handler_http,
    .user_ctx   = NULL,
    .is_websocket = false
};

const httpd_uri_t http::_ico_uri = {
    .uri        = "/favicon.ico",
    .method     = HTTP_GET,
    .handler    = http::request_handler_ico,
    .user_ctx   = NULL,
    .is_websocket = false
};

const httpd_uri_t http::_file_upload = {
    .uri       = "/upload/FFFF",   // Match all URIs of type /upload/path/to/file
    .method    = HTTP_POST,
    .handler   = http::request_handler_upload,
    .user_ctx  = NULL    // Pass server data as context
};

const httpd_uri_t http::_restart = {
    .uri       = "/upload/Restart",
    .method    = HTTP_GET,
    .handler   = http::request_handler_restart,
    .user_ctx  = NULL    // Pass server data as context
};

const httpd_uri_t http::_jquery = {
    .uri       = "/jquery-3.3.1.min.js",
    .method    = HTTP_GET,
    .handler   = http::request_handler_jquery,
    .user_ctx  = NULL    // Pass server data as context
};

const httpd_uri_t http::_w3 = {
    .uri       = "/w3.js",
    .method    = HTTP_GET,
    .handler   = http::request_handler_w3,
    .user_ctx  = NULL    // Pass server data as context
};

static const char *TAG = "cmb";

void http::RegisterUploadHandler(IFileUploadHandler *obj, UploadHandler_t handler)
{
    upload_handler.obj = obj;
    upload_handler.handler = handler;
}

esp_err_t http::httpd_open_func(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "httpd_open_func ID: %d", sockfd);
    
    _wscs[sockfd] = false;

    return ESP_OK;
}

void http::httpd_close_func(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "httpd_close_func ID: %d%s", sockfd, _wscs[sockfd] ? "ws" : "");

    _wscs.erase(sockfd);
    if (sockfd == _sockfd)
        _sockfd = -1;
        
    update_ready_state();
    
    close(sockfd);
}

void http::start_webserver()
{
    _is_ready = false;
    _sockfd = -1;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    config.open_fn = httpd_open_func;
    config.close_fn = httpd_close_func;

    config.stack_size = 0x2000;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting server!");
        _server = nullptr;
        return;
    }

    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(_server, &_ws_uri);
    httpd_register_uri_handler(_server, &_http_uri);
    httpd_register_uri_handler(_server, &_ico_uri);
    httpd_register_uri_handler(_server, &_file_upload);
    httpd_register_uri_handler(_server, &_restart);
    httpd_register_uri_handler(_server, &_jquery);
    httpd_register_uri_handler(_server, &_w3);
}

void http::stop_webserver()
{
    ESP_LOGI(TAG, "Stopping webserver");
    _is_ready = false;
    _sockfd = -1;

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

esp_err_t http::request_handler_ws(httpd_req_t *req)
{
    ESP_LOGE("ws URI", "   %s", req->uri);

    if (req->method == HTTP_GET) {
        _sockfd = httpd_req_to_sockfd(req);
        _wscs[_sockfd] = true;
        
        update_ready_state();

        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = _rx_buf;
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    // ws_pkt.payload = new uint8_t[ws_pkt.len];
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    // esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, RX_SIZE);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ws_pkt.len);
        return ret;
    }

    ESP_LOGI(TAG, "Got packet type: %d  len: %d  final:%d", ws_pkt.type, ws_pkt.len, ws_pkt.final);

    // xQueueSend(_rx_task_queue, _rx_buf, 0/*portMAX_DELAY*/);
    xQueueSend(_rx_task_queue, ws_pkt.payload, 0/*portMAX_DELAY*/);

    // delete ws_pkt.payload;

    if (((dtp_message*)ws_pkt.payload)->get_type() == DTPT_RUN_SCRIPT)
    {
        ESP_LOGE("RUN SCRIPT", "len: %d", ws_pkt.len);
        int i = 0;

        printf("\n");
        for (int i=0; i<ws_pkt.len; i++)
            printf("%0x ", ws_pkt.payload[i]);
        printf("\n");
        while (14 + 20 * i < ws_pkt.len) {
            auto e = ((entrie*)(ws_pkt.payload + 14 + 20 * i++));
            ESP_LOGE("    delay", "%ld", e->delay);
            ESP_LOGE("    repeat", "%d", e->repeat);
            ESP_LOGE("    identifier", "%lx", e->identifier);
            ESP_LOGE("    data_length_code", "%d", e->data_length_code);
            ESP_LOGE("    data", "%0X %0X %0X %0X %0X %0X %0X %0X", e->data[0], e->data[1], e->data[2], e->data[3], e->data[4], e->data[5], e->data[6], e->data[7]);
        }
    }

    return ret;
}

esp_err_t http::request_handler_file_get(httpd_req_t *req, char* file_name)
{
    ESP_LOGE("http URI", "   %s", req->uri);

    // httpd_resp_set_type(req, "text/html");
    // httpd_resp_set_status(req, );

    FILE* f = fopen(file_name, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading \"%s\"", file_name);
        return ESP_FAIL;
    }
    char buf[0x1000];
    int len = 0;
    
    do
    {
        len = fread(buf, 1, sizeof(buf), f);
        httpd_resp_send_chunk(req, buf, len);

        // httpd_resp_send(req, "<h1>Hello Secure World!</h1>", HTTPD_RESP_USE_STRLEN);
    }
    while (len > 0);

    fclose(f);

    return ESP_OK;
}


esp_err_t http::request_handler_http(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    return request_handler_file_get(req, DATA_PATH "ws_test.html");
}

esp_err_t http::request_handler_ico(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/ico");

    return request_handler_file_get(req, DATA_PATH "favicon.ico");
}

esp_err_t http::request_handler_jquery(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    return request_handler_file_get(req, DATA_PATH "jquery-3.3.1.min.js");
}

esp_err_t http::request_handler_w3(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    return request_handler_file_get(req, DATA_PATH "w3.js");
}

#define FILE_PATH_MAX (15 + CONFIG_SPIFFS_OBJ_NAME_LEN)

esp_err_t http::request_handler_upload(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    // FILE *fd = NULL;
    // struct stat file_stat;

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    // const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
    //                                          req->uri + sizeof("/upload") - 1, sizeof(filepath));
    // if (!filename) {
    //     /* Respond with 500 Internal Server Error */
    //     httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
    //     return ESP_FAIL;
    // }

    /* Filename cannot have a trailing '/' */
    // if (filename[strlen(filename) - 1] == '/') {
    //     ESP_LOGE(TAG, "Invalid filename : %s", filename);
    //     httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
    //     return ESP_FAIL;
    // }

    // if (stat(filepath, &file_stat) == 0) {
    //     ESP_LOGE(TAG, "File already exists : %s", filepath);
    //     /* Respond with 400 Bad Request */
    //     httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
    //     return ESP_FAIL;
    // }

    /* File cannot be larger than a limit */
    // if (req->content_len > MAX_FILE_SIZE) {
    //     ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
    //     /* Respond with 400 Bad Request */
    //     httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
    //                         "File size must be less than "
    //                         MAX_FILE_SIZE_STR "!");
    //     /* Return failure to close underlying connection else the
    //      * incoming file content will keep the socket busy */
    //     return ESP_FAIL;
    // }

    // fd = fopen(filepath, "w");
    // if (!fd) {
    //     ESP_LOGE(TAG, "Failed to create file : %s", filepath);
    //     /* Respond with 500 Internal Server Error */
    //     httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
    //     return ESP_FAIL;
    // }

    ESP_LOGI(TAG, "Receiving file : %s...", req->uri);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char buf[0x1000];
    int received;
    static int id = 0;
    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;
    
    id++;
    while (remaining > 0) {

        // ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, sizeof(buf))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            // fclose(fd);
            // unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }
        
        (upload_handler.obj->*upload_handler.handler)(id, req->content_len, received, (uint8_t*)buf);

        /* Write buffer content to file on storage */
        // if (received && (received != fwrite(buf, 1, received, fd))) {
        //     /* Couldn't write everything to file!
        //      * Storage may be full? */
        //     fclose(fd);
        //     unlink(filepath);

        //     ESP_LOGE(TAG, "File write failed!");
        //     /* Respond with 500 Internal Server Error */
        //     httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
        //     return ESP_FAIL;
        // }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    // fclose(fd);
    ESP_LOGI(TAG, "File reception complete");

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
// #ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
// #endif
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

esp_err_t http::request_handler_restart(httpd_req_t *req)
{
    esp_restart();
}

void http::update_ready_state()
{
    _is_ready = _sockfd > -1;
    
    // for (auto &fd: _wscs)
        // _is_ready = _is_ready || fd.second;
}

void http::ws_async_send(void *arg)
{
    // httpd_ws_send_frame_async(_server, _sockfd, &_ws_pkt);
}

void http::start(void)
{
    if (_server)
        return;

    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &_server));
    // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &_server));

    start_webserver();
}

void http::stop()
{
    if (!_server)
        return;
        
    stop_webserver();

    // ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler));
    // ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler));
}

bool http::is_ready()
{
    return _is_ready;
}

// bool http::send(uint8_t *data, size_t len)
// {
//     xSemaphoreTake(_send_mutex, portMAX_DELAY);

//     if (!is_ready() ||
//         !_server ||
//         !_wscs.size() ||
//         httpd_ws_get_fd_info(_server, _wscs.begin()->first) != HTTPD_WS_CLIENT_WEBSOCKET)
//     {
//         xSemaphoreGive(_send_mutex);
//         return false;
//     }

//     esp_err_t err = httpd_queue_work(_server, [](void *arg){
//         auto pkt = new httpd_ws_frame_t {
//             .final = 0,
//             .fragmented = 0,
//             .type = HTTPD_WS_TYPE_BINARY,
//             .payload = (uint8_t*)arg,
//             .len = 128
//         };

//         // auto pkt = (httpd_ws_frame_t*)arg;

//         for (int i=0; i<1000; i++) {
//             auto t0 = esp_timer_get_time();
//             static int64_t tl = t0;

//             esp_err_t e = httpd_ws_send_frame_async(_server, _wscs.begin()->first, pkt);

//             if (t0 - tl > 1000000){
//                 auto t1 = esp_timer_get_time();
//                 ESP_LOGW("MMM", "httpd_ws_send_frame_async delta: %" PRIu64 "  wk: %ld", t1 - t0, (int32_t)works_count);
//                 tl = t0;
//             }

//             if (e) {
//                 ESP_LOGE("MMM", "httpd_ws_send_frame_async error: %d", e);
//                 break;
//             }
//         }

//         works_count = works_count - 1;

//         // delete[] pkt->payload;
//         delete pkt;
//     }, data);

//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "httpd_queue_work error: %d", err);
//         // delete pkt;
//     } else{
//         works_count = works_count + 1;
//     }

//     xSemaphoreGive(_send_mutex);

//     return err == ESP_OK;
// }

void http::send_async(uint8_t *data, size_t len)
{
    httpd_ws_frame_t pkt = {
        .final = 0,
        .fragmented = 0,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = data,
        .len = len
    };

    if (_sockfd < 0)
    {        
        ESP_LOGE("cmb", "send_async error %s", "no WebSocket connections active");
        return;
    }

    // ESP_LOGE("cmb", "ms type %d, sock_fd:%d", data[12], _sockfd);
    // std::for_each(pkt.payload, pkt.payload + pkt.len, [](const uint8_t&b) { printf("%x ", b); });
    // printf("\n");

    esp_err_t err = httpd_ws_send_frame_async(_server, _sockfd, &pkt);

    if (err != ESP_OK) 
        ESP_LOGE("cmb", "send_async error %s", esp_err_to_name(err));
}

bool http::receive(uint8_t *data, size_t *len, TickType_t wait)
{
    return xQueueReceive(_rx_task_queue, data, wait);
}

esp_err_t http::queue_work(void (*work_fn)(void *arg))
{
    // int sockfd = -1;
    // for (auto wscs: _wscs)
    //     if (wscs.second)
    //     {
    //         sockfd = wscs.first;
    //         break;
    //     }

    // if (!is_ready() 
    // ||
        // !_server ||
        // !_wscs.size() ||
        // httpd_ws_get_fd_info(_server, _sockfd) != HTTPD_WS_CLIENT_WEBSOCKET
        // ) 
        // {
        //     return ESP_FAIL;
        // }

    return httpd_queue_work(_server, work_fn, nullptr);
}
