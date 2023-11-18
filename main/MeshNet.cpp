#include "MeshNet.hpp"

void MeshNet::start() 
    { mesh::start(); }
void MeshNet::stop() 
    { mesh::stop(); };
void MeshNet::send(uint8_t *data, uint16_t len) 
    { mesh::send(data, len); };
bool MeshNet::receive(uint8_t *data, uint16_t *len)
    { return mesh::receive(data, len); }
void MeshNet::OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct) 
    { mesh::OnLayerChangedCallbackRegister(funct); }
void MeshNet::OnIsRootCallbackRegister(_is_root_callback_funct_t funct)
    { mesh::OnIsRootCallbackRegister(funct); }
void MeshNet::OnIsConnectedCallbackRegister(_is_connected_callback_funct_t funct)
    { mesh::OnIsConnectedCallbackRegister(funct); }
void MeshNet::OnIsDisconnectedCallbackRegister(_is_disconnected_callback_funct_t funct)
    { mesh::OnIsDisconnectedCallbackRegister(funct); }

bool MeshNet::is_root()
    { return mesh::is_root(); }
bool MeshNet::is_connected()
    { return mesh::is_connected(); }
