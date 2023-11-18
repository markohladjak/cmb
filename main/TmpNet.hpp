#ifndef TMP_NET_H
#define TMP_NET_H
#pragma once

#include "net.hpp"

class TmpNet: public INetwork
{
    void start() override;
    void stop() override;

    void send(uint8_t *data, uint16_t len) override;
    bool receive(uint8_t *data, uint16_t *len) override;

    void OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct) override;
    void OnIsRootCallbackRegister(_is_root_callback_funct_t funct) override;
    void OnIsConnectedCallbackRegister(_is_connected_callback_funct_t funct) override;
    void OnIsDisconnectedCallbackRegister(_is_disconnected_callback_funct_t funct) override;

    bool is_root()  override { return true; }
    bool is_connected() override { return true; }
};


#endif