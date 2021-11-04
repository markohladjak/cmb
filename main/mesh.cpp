#include "mesh.hpp"  
#include <string>
extern "C" {
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh_internal.h"
#include "mdns.h"
}

#include <cstring>

using namespace std;

QueueHandle_t _send_mutex = xSemaphoreCreateMutex();

mesh::status_t mesh::_status = mesh::status_t::UNKNOWN;


// string mesh::_router_ssid = "Tenda_0AD898";
// string mesh::_router_pw = "12345678";
string mesh::_router_ssid = "DiamandNC";
string mesh::_router_pw = "nodesAdmin";
// string mesh::_router_ssid = "TP-Link_982C";
// string mesh::_router_pw = "33188805";

int	mesh::_router_chennel = 1;

string mesh::_mesh_pw = "34578456";

bool mesh::_is_mesh_connected = false;
bool mesh::_is_mesh_initialized = false;

int mesh::_max_device_count = 100;
int mesh::_max_connection = 4;  // 10 MAX

int mesh::_mesh_layer = -1;

const uint8_t mesh::MESH_ID[6] = { 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 };

_layer_changed_callback_funct_t mesh::_layer_changed_callback_funct = NULL;
_is_root_callback_funct_t mesh::_is_root_callback_funct = NULL;

bool mesh::_is_running = false;

bool mesh::_is_root = false;
bool mesh::_is_root_fixed = true;

#define RX_SIZE          (1500)
#define TX_SIZE          (1460)

static uint8_t tx_buf[TX_SIZE] = { 0, };
static uint8_t rx_buf[RX_SIZE] = { 0, };

static esp_netif_t *netif_sta = NULL;
static mesh_addr_t mesh_parent_addr;

#define MESH_TAG "mesh"


mesh::mesh() {
}

mesh::~mesh() {
}

void mesh::start() {
	if (_is_mesh_initialized) {
	    ESP_LOGI(MESH_TAG, "mesh running\n");

	    return;
	}

    init();
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s<%d>%s, ps:%d\n",  esp_get_minimum_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());

    print_route_table();
}

void mesh::stop() {
    esp_mesh_deinit();

    deinit();
}

void mesh::send(uint8_t *data, uint16_t len) {
	// xSemaphoreTake(_send_mutex, portMAX_DELAY);

    if (!is_connected()) {
        // vTaskDelay(1);
        // xSemaphoreGive(_send_mutex);
        return;
    }
    static mesh_data_t msg;
    msg.data = data;
    msg.size = len;
    msg.proto = MESH_PROTO_BIN;
    msg.tos = MESH_TOS_P2P;
    
    // static mesh_addr_t addrmc = {
    //     .addr = {0x01, 0x00, 0x5E, 0x7F, 0xFF, 0xFF}
    //     };

    static mesh_addr_t addr = {
        .addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
        };

    // static mesh_addr_t addr2 = {
    //     .addr = {0x3c, 0x61, 0x05, 0x16, 0xd0, 0x61}
    // };

    // static bool done = false;
    // static mesh_opt_t opt;
    // if (!done) {
    //     ESP_ERROR_CHECK(esp_mesh_set_group_id(&addr2, 1));
    //     opt.type = MESH_OPT_SEND_GROUP;
    //     opt.val = (uint8_t*)(&addr2);
    //     opt.len = 1;

    //     done = true;

    // }


    // ESP_LOGE("TAG", "send error: %d", 
    // auto err = esp_mesh_send(NULL, &msg, MESH_DATA_P2P, NULL, 0);
    auto err = esp_mesh_send(&addr, &msg, MESH_DATA_P2P | MESH_DATA_NONBLOCK, NULL, 0);

    // auto err = esp_mesh_send(&addrmc, &msg, MESH_DATA_GROUP, &opt, 1);

    if (err) {
        // ESP_LOGE(MESH_TAG, "esp_mesh_send error: %s", esp_err_to_name(err));
        // vTaskDelay(1);
    }

// MESH_MPS
// esp_mesh_recv()
	// xSemaphoreGive(_send_mutex);
}

bool mesh::receive(uint8_t *data, uint16_t *len)
{
    esp_err_t err;
    mesh_addr_t from;
//    int send_count = 0;
    mesh_data_t msg;
    msg.data = data;
    msg.size = *len;//MSG_SIZE;
    int flags = 0;

    err = esp_mesh_recv(&from, &msg, portMAX_DELAY, &flags, NULL, 0);

    *len = msg.size;

    if (err != ESP_OK) {
        ESP_LOGE(MESH_TAG, "esp_mesh_send error: %s", esp_err_to_name(err));
        return false;
        // vTaskDelay(1);
    }

    // ESP_LOGE(MESH_TAG, "data from mesh: size: %d", msg.size);

    return true;
}

void mesh::init() {
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    if (!netif_sta)
        ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    /*  wifi initialization */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &mesh::ip_event_handler, NULL));
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh::mesh_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(25));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#endif
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = _router_chennel;
    cfg.router.ssid_len = _router_ssid.size();
    memcpy((uint8_t *) &cfg.router.ssid, _router_ssid.c_str(), cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, _router_pw.c_str(), _router_pw.size());
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(wifi_auth_mode_t::WIFI_AUTH_WPA_WPA2_PSK));
    cfg.mesh_ap.max_connection = _max_connection;
    memcpy((uint8_t *) &cfg.mesh_ap.password, _mesh_pw.c_str(), _mesh_pw.size());
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    _is_mesh_initialized = true;
}

void mesh::deinit() {
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &mesh::ip_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh::mesh_event_handler));

    esp_netif_dhcpc_stop(netif_sta);

    // disconnect_and_destroy(netif_sta);
    // esp_wifi_clear_default_wifi_driver_and_handlers(netif_sta);
    // netif_sta = NULL;

 
    _is_mesh_initialized = false;
}


void mesh::set_status(status_t status)
{
    if (status == CONNECTED)
        _is_mesh_connected = true;
    if (status == STARTED)
        _is_mesh_connected = false;
    if (status == DISCONNECTED)
        _is_mesh_connected = false;
    if (status == STOPPED)
        _is_mesh_connected = false;
    
    // _mesh_layer = esp_mesh_get_layer();

}

void mesh::mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:" MACSTR "", MAC2STR(id.addr));

        set_status(status_t::STARTED);
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        
        set_status(status_t::STOPPED);
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, " MACSTR "",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, " MACSTR "",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, _mesh_layer);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, _mesh_layer);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        _mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d, parent:" MACSTR "%s, ID:" MACSTR ", duty:%d",
                 _mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (_mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);

        set_status(status_t::CONNECTED);

        if (esp_mesh_is_root()) {
            esp_netif_dhcpc_start(netif_sta);
        }
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);

        set_status(status_t::DISCONNECTED);
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        _mesh_layer = layer_change->new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d", _mesh_layer);
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:" MACSTR "",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:" MACSTR "",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:" MACSTR "",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        /* new root */
        _mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:" MACSTR "", _mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>" MACSTR ", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:" MACSTR "",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, " MACSTR "",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    case MESH_EVENT_PS_PARENT_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    }
    break;
    case MESH_EVENT_PS_CHILD_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, " MACSTR ", duty:%d", ps_duty->child_connected.aid-1,
                MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
        break;
    }
}

void mesh::ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));

    if (esp_mesh_is_root()) {
        _is_root = true;
        if (_is_root_callback_funct)
            _is_root_callback_funct();
    }
}

uint64_t mesh::self_address(uint16_t device_num) {
    mesh_addr_t route_table[1];
    int route_table_size = 0;

    if (!_node_addr_mask) {
		esp_mesh_get_routing_table((mesh_addr_t *) &route_table, 6, &route_table_size);

		memcpy(((uint8_t*)&_node_addr_mask) + 2, (void*)(route_table[0].addr), 6);;

		_node_addr_mask &= 0xFFFFFFFFFFFF0000;
    }

	return _node_addr_mask + device_num;
}

void mesh::print_route_table() {
    mesh_addr_t route_table[_max_device_count];
    int route_table_size = 0;

	esp_mesh_get_routing_table((mesh_addr_t *) &route_table, _max_device_count * 6, &route_table_size);

	printf("Routing table:\n");

	for (int i = 0; i < route_table_size; ++i) {
		printf("%3d    " MACSTR "\n", i, MAC2STR(route_table[i].addr));
	}
}

void mesh::OnLayerChangedCallbackRegister(_layer_changed_callback_funct_t funct) {
	_layer_changed_callback_funct = funct;
}

void mesh::OnIsRootCallbackRegister(_is_root_callback_funct_t funct){
	_is_root_callback_funct = funct;
}
