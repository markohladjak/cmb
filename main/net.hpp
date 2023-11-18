#ifndef NET_H
#define NET_H
#pragma once

#include "stdint.h"
#include "mesh.hpp"

class INetwork
{
public:
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void send(uint8_t *data, uint16_t len) = 0;
    virtual bool receive(uint8_t *data, uint16_t *len) = 0;

    virtual void OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct) = 0;
    virtual void OnIsRootCallbackRegister(_is_root_callback_funct_t funct) = 0;
    virtual void OnIsConnectedCallbackRegister(_is_connected_callback_funct_t funct) = 0;
    virtual void OnIsDisconnectedCallbackRegister(_is_disconnected_callback_funct_t funct) = 0;

    virtual bool is_root() = 0;
    virtual bool is_connected() = 0;
};


#endif