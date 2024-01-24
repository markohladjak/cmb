#ifndef SCRIPT_H
#define SCRIPT_H
#pragma once

#include "vector"
#include "dtp_message.hpp"

using namespace std;

struct alignas(4) entrie
{
    int32_t delay;
    uint32_t identifier;
    uint8_t data[8]; 
    uint8_t data_length_code;
    uint8_t reserver;
    uint16_t repeat;
};

class Script
{
    int _position;
    vector<entrie> entries;

    esp_timer_handle_t timer;

    void on_timer();

public:
    Script(int buffer_size);

    void Run();
    void Stop();

    void AddEntrie(msg_can_data &msg);
};

#endif