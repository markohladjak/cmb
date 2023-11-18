#include "can.hpp"  
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "map"

#define ALERTS_TAG "Alerts"

twai_timing_config_t *can::t_config = nullptr;
twai_filter_config_t can::f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
twai_general_config_t can::g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_NC, GPIO_NUM_NC, TWAI_MODE_NORMAL);

can::transceiver_config_t can::transceiver_config = { GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, false };

can::can(gpio_num_t tx, gpio_num_t rx)
{
    init(tx, rx);
}

can::can(gpio_num_t tx, gpio_num_t rx, gpio_num_t err, gpio_num_t stb, gpio_num_t en)
{
    init(tx, rx, true, err, stb, en);
}

void can::init(gpio_num_t tx, gpio_num_t rx, bool ft, gpio_num_t err, gpio_num_t stb, gpio_num_t en)
{
    can::transceiver_config.tx = tx;
    can::transceiver_config.rx = rx;

    g_config.tx_io = tx;
    g_config.rx_io = rx;

    if (ft) {
        can::transceiver_config.ft = true;
        can::transceiver_config.err = err;
        can::transceiver_config.stb = stb;
        can::transceiver_config.en = en;
    }

    g_config.tx_queue_len = 46;
    g_config.rx_queue_len = 46;

    ft_init();
}

void can::ft_init()
{
    if (!transceiver_config.ft)
        return;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << transceiver_config.err),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = ((1ULL << transceiver_config.stb) | (1ULL << transceiver_config.en));
    io_conf.mode = GPIO_MODE_OUTPUT;

    gpio_config(&io_conf);

//         {
//         gpio_config_t io_conf = {
//             .pin_bit_mask = (1ULL << GPIO_ERR),
//             .mode = GPIO_MODE_INPUT,
//             .pull_up_en = GPIO_PULLUP_DISABLE,
//             .pull_down_en = GPIO_PULLDOWN_DISABLE,
//             .intr_type = GPIO_INTR_DISABLE
//         };
//         gpio_config(&io_conf);
//         }
//         {
//         gpio_config_t io_conf = {
//             .pin_bit_mask = (1ULL << GPIO_STB),
//             .mode = GPIO_MODE_OUTPUT,
//             .pull_up_en = GPIO_PULLUP_DISABLE,
//             .pull_down_en = GPIO_PULLDOWN_DISABLE,
//             .intr_type = GPIO_INTR_DISABLE
//         };
//         gpio_config(&io_conf);
//         }
//         {
//         gpio_config_t io_conf = {
//             .pin_bit_mask = (1ULL << GPIO_EN),
//             .mode = GPIO_MODE_OUTPUT,
//             .pull_up_en = GPIO_PULLUP_ENABLE,
//             .pull_down_en = GPIO_PULLDOWN_DISABLE,
//             .intr_type = GPIO_INTR_DISABLE
//         };
//         gpio_config(&io_conf);
//         }

//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));

//         vTaskDelay(1 / portTICK_PERIOD_MS);

//         // Power-on Standby
//         gpio_set_level(GPIO_STB, 1);
//         gpio_set_level(GPIO_EN, 0);

//         vTaskDelay(1 / portTICK_PERIOD_MS);
//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));

//         // Normal Mode
//         gpio_set_level(GPIO_STB, 1);
//         gpio_set_level(GPIO_EN, 1);

//         vTaskDelay(4 / portTICK_PERIOD_MS);
//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));

//         // Go-to-Sleep
//         gpio_set_level(GPIO_STB, 0);
//         gpio_set_level(GPIO_EN, 1);

//         vTaskDelay(10 / portTICK_PERIOD_MS);
//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));

//         // Standby / Sleep
//         gpio_set_level(GPIO_STB, 0);
//         gpio_set_level(GPIO_EN, 0);

//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));

//         // Normal Mode
//         gpio_set_level(GPIO_STB, 1);
//         gpio_set_level(GPIO_EN, 1);

//         vTaskDelay(4 / portTICK_PERIOD_MS);
//         printf("RX: %d\n", gpio_get_level(GPIO_RX));
//         printf("ERR: %d\n", gpio_get_level(GPIO_ERR));


//     }
}

can::~can()
{
    if (t_config) 
        delete t_config;

    t_config = nullptr;
}

void can::ft_start()
{
    if (!transceiver_config.ft)
        return;
    
    // Normal Mode
    gpio_set_level(transceiver_config.stb, 1);
    gpio_set_level(transceiver_config.en, 1);
}

void can::start(speed_t speed, twai_mode_t mode)
{
    if (t_config)
        stop();

    ft_start();

    switch (speed)
    {
    case speed_t::_25KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_25KBITS(); break;
    case speed_t::_50KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_50KBITS(); break;
    case speed_t::_83_3KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_83_3KBITS(); break;
    case speed_t::_80KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_80KBITS(); break;
    case speed_t::_100KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_100KBITS(); break;
    case speed_t::_125KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_125KBITS(); break;
    case speed_t::_250KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_250KBITS(); break;
    case speed_t::_500KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_500KBITS(); break;
    case speed_t::_800KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_800KBITS(); break;
    case speed_t::_1MBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_1MBITS(); break;

    default:
        break;
    }

    g_config.mode = mode;

    ESP_ERROR_CHECK(twai_driver_install(&g_config, t_config, &f_config));
    ESP_LOGI("CAN", "Driver installed");

    ESP_ERROR_CHECK(twai_start());

    xTaskCreatePinnedToCore(alerts_task, "alerts_task", 4096, NULL, 10, NULL, tskNO_AFFINITY);
}

void can::stop()
{
    ESP_ERROR_CHECK(twai_stop());
    ESP_LOGI("", "TWAI stoped");

    // ESP_ERROR_CHECK(twai_driver_uninstall());
    // ESP_LOGI("", "Driver uninstalled");
}

void can::transmit(twai_message_t &msg)
{
    auto err = twai_transmit(&msg, 0);
    if (err) {
        printf("twai_transmit: %s\n", esp_err_to_name(err));

        // printf("twai_transmit: %s\n", esp_err_to_name(err));

        if (err == ESP_ERR_INVALID_STATE)
        {
            twai_status_info_t s;
            twai_get_status_info(&s);

            ESP_LOGW("STATE", "s:%d, qtx:%lu, qrx:%lu, tx_ec:%lu, rx_ec:%lu, tx_fc:%lu, rx_mi:%lu, rx_orc:%lu, al:%lu, be:%lu", 
                s.state, s.msgs_to_tx, s.msgs_to_rx, s.tx_error_counter, s.rx_error_counter,
                    s.tx_failed_count, s.rx_missed_count, s.rx_overrun_count, s.arb_lost_count, s.bus_error_count);
        }

    }
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

void can::alerts_task(void *arg)
{
    // xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
    // ESP_ERROR_CHECK(twai_start());
    // ESP_LOGI(EXAMPLE_TAG, "Driver started");
    // ESP_LOGI(EXAMPLE_TAG, "Starting transmissions");
    // xSemaphoreGive(tx_task_sem);    //Start transmit task

    //Prepare to trigger errors, reconfigure alerts to detect change in error state

    // twai_reconfigure_alerts(TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF, NULL);
    twai_reconfigure_alerts(TWAI_ALERT_ALL & 0x1FFF8, NULL);

    // for (int i = 3; i > 0; i--) {
    //     ESP_LOGW(EXAMPLE_TAG, "Trigger TX errors in %d", i);
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
    // ESP_LOGI(EXAMPLE_TAG, "Trigger errors");
    // trigger_tx_error = true;

    std::map<int, int> alerts_map;
    std::map<int, char*> alerts_name;

    alerts_name[TWAI_ALERT_TX_IDLE             ] = "IDL";                 // "TX_IDLE";
    alerts_name[TWAI_ALERT_TX_SUCCESS          ] = "SCC";                 // "TX_SUCCESS";
    alerts_name[TWAI_ALERT_RX_DATA             ] = "RXD";                 // "RX_DATA";
    alerts_name[TWAI_ALERT_BELOW_ERR_WARN      ] = "BEW";                 // "BELOW_ERR_WARN";
    alerts_name[TWAI_ALERT_ERR_ACTIVE          ] = "RRA";                 // "ERR_ACTIVE";
    alerts_name[TWAI_ALERT_RECOVERY_IN_PROGRESS] = "RIP";                 // "RECOVERY_IN_PROGRESS";
    alerts_name[TWAI_ALERT_BUS_RECOVERED       ] = "BRD";                 // "BUS_RECOVERED";
    alerts_name[TWAI_ALERT_ARB_LOST            ] = "AL" ;                 // "ARB_LOST";
    alerts_name[TWAI_ALERT_ABOVE_ERR_WARN      ] = "AEW";                 // "ABOVE_ERR_WARN";
    alerts_name[TWAI_ALERT_BUS_ERROR           ] = "BE" ;                 // "BUS_ERROR";
    alerts_name[TWAI_ALERT_TX_FAILED           ] = "TF" ;                 // "TX_FAILED";
    alerts_name[TWAI_ALERT_RX_QUEUE_FULL       ] = "RQF";                 // "RX_QUEUE_FULL";
    alerts_name[TWAI_ALERT_ERR_PASS            ] = "RRP";                 // "ERR_PASS";
    alerts_name[TWAI_ALERT_BUS_OFF             ] = "BFF";                 // "BUS_OFF";
    alerts_name[TWAI_ALERT_RX_FIFO_OVERRUN     ] = "RFO";                 // "RX_FIFO_OVERRUN";
    alerts_name[TWAI_ALERT_TX_RETRIED          ] = "TR" ;                 // "TX_RETRIED";
    alerts_name[TWAI_ALERT_PERIPH_RESET        ] = "RST";                 // "PERIPH_RESET";

    char buf[256];

    while (1) {
        uint32_t alerts;
        twai_read_alerts(&alerts, portMAX_DELAY);

        for (int i=0; i<alerts_name.size(); i++)
            if ((1 << i) & alerts)
                alerts_map[1 << i]++;

        auto t0 = esp_timer_get_time();
        static int64_t tl = 0;

        if (t0 - tl > 1000000) {
            if (alerts_map.size()) 
            {
                int bp = 0;
                for (auto &a : alerts_map)
                    bp = sprintf(buf + bp, "[%s: %d] ", alerts_name[a.first], a.second);

                ESP_LOGE("alerts", "%s", buf);

                alerts_map.clear();
            }
            tl = t0;
        }        

        // if (alerts & TWAI_ALERT_ABOVE_ERR_WARN) {
        //     ESP_LOGE(ALERTS_TAG, "Surpassed Error Warning Limit");
        // }
        // if (alerts & TWAI_ALERT_ERR_PASS) {
        //     ESP_LOGE(ALERTS_TAG, "Entered Error Passive state");
        // }
        // if (alerts & TWAI_ALERT_BUS_OFF) {
        //     ESP_LOGE(ALERTS_TAG, "Bus Off state");
        //     //Prepare to initiate bus recovery, reconfigure alerts to detect bus recovery completion
            // twai_reconfigure_alerts(TWAI_ALERT_BUS_RECOVERED, NULL);
        //     // for (int i = 3; i > 0; i--) {
        //     //     ESP_LOGW(ALERTS_TAG, "Initiate bus recovery in %d", i);
        //     //     vTaskDelay(pdMS_TO_TICKS(1000));
        //     // }
            // twai_initiate_recovery();    //Needs 128 occurrences of bus free signal
        //     ESP_LOGI(ALERTS_TAG, "Initiate bus recovery");
        // }
        // if (alerts & TWAI_ALERT_BUS_RECOVERED) {
        //     //Bus recovery was successful, exit control task to uninstall driver
        //     ESP_LOGI(ALERTS_TAG, "Bus Recovered");
        //     // break;
        // }
    }
    //No need call twai_stop(), bus recovery will return to stopped state
    // xSemaphoreGive(ctrl_task_sem);
    vTaskDelete(NULL);
}
