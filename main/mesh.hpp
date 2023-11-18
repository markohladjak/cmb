#ifndef MESH_H
#define MESH_H
#pragma once

#include <string>
#include "esp_event_base.h"

typedef void (*_layer_changed_callback_funct_t)(int);
typedef void (*_is_root_callback_funct_t)();
typedef void (*_is_connected_callback_funct_t)();
typedef void (*_is_disconnected_callback_funct_t)();

class mesh  
{
	enum status_t {
		UNKNOWN,
		STARTED,
		CONNECTED,
		DISCONNECTED,
		STOPPED
	};

	static std::string _router_ssid;
	static std::string _router_pw;
	static int	_router_chennel;

	static std::string _mesh_pw;
	static const uint8_t MESH_ID[6];

	static bool _is_mesh_initialized;
	static bool _is_mesh_connected;
	static int _max_device_count;
	static int _max_connection;
	static bool _allow_channel_switch;
	static int _mesh_layer;

	static uint64_t _node_addr_mask;

	static bool _is_running;

	static _layer_changed_callback_funct_t _layer_changed_callback_funct;
	static _is_root_callback_funct_t _is_root_callback_funct;
	static _is_connected_callback_funct_t _is_connected_callback_funct;
	static _is_disconnected_callback_funct_t _is_disconnected_callback_funct;
	
	static void init();
	static void deinit();
	static void set_status(status_t status);

	// static void run_mdns();

	static void on_this_became_root();
	static void on_layer_changed(int layer);
	static void on_routing_table_changed();
	static void on_root_address();

	static uint64_t self_address(uint16_t device_num);

	static void print_route_table();

public:
	static status_t _status;
	static bool _is_root;
	static bool _is_root_fixed;

	mesh();
	~mesh();

	static void start();
	static void stop();

	static void send(uint8_t *data, uint16_t len);
	static bool receive(uint8_t *data, uint16_t *len);

	static void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
	static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

	static void OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct);
	static void OnIsRootCallbackRegister(_is_root_callback_funct_t funct);
	static void OnIsConnectedCallbackRegister(_is_connected_callback_funct_t funct);
	static void OnIsDisconnectedCallbackRegister(_is_disconnected_callback_funct_t funct);

	static bool is_root() { return _is_root; }
	static bool is_connected() { return _is_mesh_connected; }

private:
	void send_msg(std::string &msg);
};		

#endif