#include "can.hpp"  
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define ALERTS_TAG "Alerts"

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
    case speed_t::_83_3KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_83_3KBITS(); break;
    case speed_t::_100KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_100KBITS(); break;
    case speed_t::_125KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_125KBITS(); break;
    case speed_t::_250KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_250KBITS(); break;
    case speed_t::_500KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_500KBITS(); break;
    case speed_t::_800KBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_800KBITS(); break;
    case speed_t::_1MBITS : t_config = new twai_timing_config_t TWAI_TIMING_CONFIG_1MBITS(); break;

    default:
        break;
    }

// #define TWAI_TIMING_CONFIG_83_3KBITS()   {.brp = 181, .tseg_1 = 15, .tseg_2 = 4, .sjw = 3, .triple_sampling = false}

    // t_config = TWAI_TIMING_CONFIG_83_3KBITS();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, t_config, &f_config));
    ESP_LOGI("", "Driver installed");

    ESP_ERROR_CHECK(twai_start());

    xTaskCreatePinnedToCore(alerts_task, "alerts_task", 4096, NULL, 10, NULL, tskNO_AFFINITY);
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
    if (err)
        printf("twai_transmit: %s\n", esp_err_to_name(err));
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

    while (1) {
        uint32_t alerts;
        twai_read_alerts(&alerts, portMAX_DELAY);

        if (alerts & TWAI_ALERT_TX_IDLE             ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_TX_IDLE             ");
        if (alerts & TWAI_ALERT_TX_SUCCESS          ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_TX_SUCCESS          ");
        if (alerts & TWAI_ALERT_RX_DATA             ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_RX_DATA             ");
        if (alerts & TWAI_ALERT_BELOW_ERR_WARN      ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_BELOW_ERR_WARN      ");
        if (alerts & TWAI_ALERT_ERR_ACTIVE          ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_ERR_ACTIVE          ");
        if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_RECOVERY_IN_PROGRESS");
        if (alerts & TWAI_ALERT_BUS_RECOVERED       ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_BUS_RECOVERED       ");
        if (alerts & TWAI_ALERT_ARB_LOST            ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_ARB_LOST            ");
        if (alerts & TWAI_ALERT_ABOVE_ERR_WARN      ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_ABOVE_ERR_WARN      ");
        if (alerts & TWAI_ALERT_BUS_ERROR           ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_BUS_ERROR           ");
        if (alerts & TWAI_ALERT_TX_FAILED           ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_TX_FAILED           ");
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL       ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_RX_QUEUE_FULL       ");
        if (alerts & TWAI_ALERT_ERR_PASS            ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_ERR_PASS            ");
        if (alerts & TWAI_ALERT_BUS_OFF             ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_BUS_OFF             ");
        if (alerts & TWAI_ALERT_RX_FIFO_OVERRUN     ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_RX_FIFO_OVERRUN     ");
        if (alerts & TWAI_ALERT_TX_RETRIED          ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_TX_RETRIED          ");
        if (alerts & TWAI_ALERT_PERIPH_RESET        ) ESP_LOGE(ALERTS_TAG, "TWAI_ALERT_PERIPH_RESET        ");

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
