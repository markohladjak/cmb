#ifndef SCRIPT_H
#define SCRIPT_H
#pragma once

#include "vector"
#include "dtp_message.hpp"

using namespace std;

struct entrie
{
    // int32_t count;
    int32_t delay; // us

    uint32_t identifier;
    uint8_t data[TWAI_FRAME_MAX_DLC]; 
    uint8_t data_length_code;
    uint8_t repeat;
};

class Script
{
    int _position;
    vector<entrie> entries;

public:
    Script(int buffer_size);

    void add_entrie(msg_can_data &msg);
};

#endif