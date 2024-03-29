#ifndef DTP_MESSAGE_H
#define DTP_MESSAGE_H
#pragma once

#include <inttypes.h>
#include <driver/twai.h>

enum dtp_message_t {
    DTPT_INFO = 1,
    DTPT_REQUEST = 2,
    DTPT_CAN_DATA = 8,
    DTPT_SIGNAL_DATA = 9,
    DTPT_RUN_SCRIPT = 10,
};

class msg_can_data;

struct net_frame
{
    uint16_t size;
    uint8_t* data;
};

class dtp_message {
public:
    int64_t _time_stamp;
    int32_t _id = -1;

    uint8_t _type;
    uint8_t _dev_id = 0;

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
    uint8_t get_dev_id() const { return _dev_id; }
    void set_time_stamp() { _time_stamp = 0xF8F8F8F8F8F8F8F8; }
    void set_type(uint8_t type) { _type = type; }
    uint8_t get_type() const { return _type; }
    int32_t get_id() const { return _id; }
};

class msg_info: public dtp_message
{
public:
    msg_info() {
        _type = DTPT_INFO;
    }
};

class msg_request : public dtp_message
{
    msg_request() {
        _type = DTPT_REQUEST;
    }
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

struct msg_signal: public dtp_message
{
    uint16_t num_words;  
    uint8_t bytes_ptr;
    
    msg_signal() {
        _type = DTPT_SIGNAL_DATA;
    }
};

struct msg_run_script: public dtp_message
{
    char script_name[256];
};

struct msg_upload_file: public dtp_message
{

};

#endif