#include "Script.h"

Script::Script(int buffer_size)
{
    _position = 0;
    entries.reserve(buffer_size);
}

void Script::add_entrie(msg_can_data &msg)
{
    entrie e { (int32_t)msg._time_stamp, msg.twai_msg.identifier, *msg.twai_msg.data, msg.twai_msg.data_length_code };

    entries[_position++] = e;

    if (_position == entries.capacity())
        _position = 0;

}
