#include "indication.hpp"
#include <rom/ets_sys.h>

int indication::old_count = 0;

indication::indication(gpio_num_t led_gpio_num)
{
    _gpio_num = led_gpio_num;

    gpio_reset_pin(led_gpio_num);
    gpio_set_direction(led_gpio_num, GPIO_MODE_OUTPUT);	
}

indication::~indication()
{
    gpio_reset_pin(_gpio_num);
}

void indication::blink_config(int count)
{
	static int _count = 0;
	static TaskHandle_t task = NULL;;
	static bool exit_request = false;
    static gpio_num_t gpio_num = _gpio_num;

    old_count = _count;
	_count = count;

    exit_request = count == 0;
	
    if (task)
		return;


	xTaskCreatePinnedToCore([](void *) {
		while(!exit_request) {
			for (int i=0; i<_count; i++) {
				vTaskDelay((1000 / 2 - 1000 / 4) / portTICK_PERIOD_MS);
                gpio_set_level(gpio_num, 1);
				vTaskDelay((1000 / 4) / portTICK_PERIOD_MS);
                gpio_set_level(gpio_num, 0);
			}

			vTaskDelay(2000 / portTICK_PERIOD_MS);
		}
        exit_request = false;
        task = NULL;
		vTaskDelete(NULL);
	}, "MT1", 1000, NULL, 1, &task, tskNO_AFFINITY);

}

void indication::flash()
{
    static TaskHandle_t task = NULL;
    static gpio_num_t gpio_num = _gpio_num;
    static bool run_blink = false;

    static esp_timer_handle_t periodic_timer;
    static const esp_timer_create_args_t periodic_timer_args = {
            .callback = [](void *o) { if (run_blink) ((indication*)o)->blink_config(old_count); run_blink = true; },
            .arg = this,
            .name = "indication_tm",
    };

	// static bool exit_request = false;

    if (task) {
        xTaskNotifyGive(task);
        run_blink = false;
        return;
    } 
    else {
        blink_config(0);
    }

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 3000));

	xTaskCreatePinnedToCore([](void *) {
		while(1) {
                ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
                gpio_set_level(gpio_num, 1);
				vTaskDelay(1);
                // ets_delay_us(1000);
                gpio_set_level(gpio_num, 0);
		}

		vTaskDelete(NULL);
	}, "MT2", 1000, NULL, tskIDLE_PRIORITY, &task, tskNO_AFFINITY);
}