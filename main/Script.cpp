#include "Script.h"
#include "can.hpp"

Script::Script(int buffer_size) 
{
    _position = 0;
    entries.reserve(buffer_size);
}

void Script::on_timer()
{
    twai_message_t m { .flags = 0, .identifier = 0x111, .data_length_code = 3, .data = { 1, 2, 3 } };

    can::transmit(m);
}

void Script::AddEntrie(msg_can_data &msg)
{
    entrie e { (int32_t)msg._time_stamp, msg.twai_msg.identifier, *msg.twai_msg.data, msg.twai_msg.data_length_code };

    entries[_position++] = e;

    if (_position == entries.capacity())
        _position = 0;

}

void Script::Run()
{
    const esp_timer_create_args_t timer_args {
            .callback = [](void *arg) { ((Script*)arg)->on_timer(); },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "periodic",
            .skip_unhandled_events = false
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
}

void Script::Stop()
{
    esp_timer_delete(timer);
}