#ifndef DTP_MESSAGE_H
#define DTP_MESSAGE_H
#pragma once

#include <inttypes.h>
#include <driver/twai.h>

enum dtp_message_t {
     DTPT_CAN_DATA = 8,
};


class dtp_message {
    int64_t _time_stamp;
    int32_t _id = -1;

protected:
    uint8_t _type;
    uint8_t _dev_id = 0;

public:
    dtp_message() {}

    dtp_message(int32_t id)
    {
        set_id(id);
    }

    void set_id(int32_t id) {
        _id = id;
        set_time_stamp();
    }

    uint8_t dev_id() { return _dev_id; }
    void set_dev_id(uint8_t dev_id) { _dev_id = dev_id; }
    uint8_t get_dev_id() { return _dev_id; }
    void set_time_stamp() { _time_stamp = 0; }
    void set_type(uint8_t type) { _type = type; }
    uint8_t get_type() { return _type; }
    int32_t get_id() { return _id; }
};

class msg_can_data: public dtp_message 
{
public:
    twai_message_t twai_msg;    

    msg_can_data() {
        _type = DTPT_CAN_DATA;
    }

    msg_can_data(int32_t id)
        : dtp_message { id }
    {
        _type = DTPT_CAN_DATA;
    }
};

#endif