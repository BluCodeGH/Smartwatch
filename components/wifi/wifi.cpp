#include "wifi.h"

#include <cstring>
#include <stdio.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include "esp_event.h"
extern "C" {
  #include "esp_wifi.h"
}
#include "nvs_flash.h"

const char* bw_system_event_names[] = { "WIFI_READY", "SCAN_DONE", "STA_START", "STA_STOP", "STA_CONNECTED", "STA_DISCONNECTED", "STA_AUTHMODE_CHANGE", "STA_GOT_IP", "STA_LOST_IP", "STA_WPS_ER_SUCCESS", "STA_WPS_ER_FAILED", "STA_WPS_ER_TIMEOUT", "STA_WPS_ER_PIN", "AP_START", "AP_STOP", "AP_STACONNECTED", "AP_STADISCONNECTED", "AP_PROBEREQRECVED", "GOT_IP6", "ETH_START", "ETH_STOP", "ETH_CONNECTED", "ETH_DISCONNECTED", "ETH_GOT_IP", "MAX"};
const char* bw_system_event_reasons[] = { "UNSPECIFIED", "AUTH_EXPIRE", "AUTH_LEAVE", "ASSOC_EXPIRE", "ASSOC_TOOMANY", "NOT_AUTHED", "NOT_ASSOCED", "ASSOC_LEAVE", "ASSOC_NOT_AUTHED", "DISASSOC_PWRCAP_BAD", "DISASSOC_SUPCHAN_BAD", "UNSPECIFIED", "IE_INVALID", "MIC_FAILURE", "4WAY_HANDSHAKE_TIMEOUT", "GROUP_KEY_UPDATE_TIMEOUT", "IE_IN_4WAY_DIFFERS", "GROUP_CIPHER_INVALID", "PAIRWISE_CIPHER_INVALID", "AKMP_INVALID", "UNSUPP_RSN_IE_VERSION", "INVALID_RSN_IE_CAP", "802_1X_AUTH_FAILED", "CIPHER_SUITE_REJECTED", "BEACON_TIMEOUT", "NO_AP_FOUND", "AUTH_FAIL", "ASSOC_FAIL", "HANDSHAKE_TIMEOUT" };
const char* bw_system_auth_names[] = { "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "WPA2_ENTERPRISE", "MAX" };
#define reason2str(r) ((r>176)?bw_system_event_reasons[r-176]:bw_system_event_reasons[r-1])

bluWiFiClass::bluWiFiClass() {
}

bluWiFiClass::~bluWiFiClass() {
  esp_wifi_disconnect();
  esp_wifi_stop();
  //esp_wifi_deinit();
}

void bluWiFiClass::init() {
  if (!(state & bw_init) || !(state & bw_initing)) {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    state = (bluWiFiClass::State)(state | bw_initing);
    // tcpip_adapter_init();
    // esp_event_loop_init(_event_handler, NULL); //Don't ESP_ERROR_CHECK as we may already have an event loop.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_event_handler, NULL));

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //Disable brownout detector
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // int8_t pow;
    // esp_wifi_get_max_tx_power(&pow);
    // printf("%d\n", pow);
    esp_wifi_set_max_tx_power(82);
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1); //Enable brownout detector
  }
}

void bluWiFiClass::scan() {
  if (!(state & bw_scanning)) {
    state = (bluWiFiClass::State)(state & ~bw_scanned);
    state = (bluWiFiClass::State)(state | bw_scanning);
    _scan_config = {};
    _scan_config.ssid = NULL;
    _scan_config.bssid = NULL;
    _scan_config.channel = 0;
    _scan_config.show_hidden = true;
    if (state & bw_init) {
      ESP_ERROR_CHECK(esp_wifi_scan_start(&_scan_config, false));
    } else {
      state = (bluWiFiClass::State)(state | bw_scan_next);
    }
  }
}

void bluWiFiClass::connect(char* ssid, char* password) {
  if (!(state & bw_connected) || !(state & bw_connecting)) {
    state = (bluWiFiClass::State)(state | bw_connecting);
    wifi_config_t wifi_config = { };
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    if (state & bw_init) {
      esp_wifi_connect();
    } else {
      state = (bluWiFiClass::State)(state | bw_connect_next);
    }
  }
}

void bluWiFiClass::connect(const char* ssid, const char* password) {
  bluWiFiClass::connect((char*)ssid, (char*)password);
}

void bluWiFiClass::disconnect() {
  ESP_ERROR_CHECK(esp_wifi_disconnect());
}

void bluWiFiClass::deinit() {
  ESP_ERROR_CHECK(esp_wifi_stop());
  //ESP_ERROR_CHECK(esp_wifi_deinit());
}

bluWiFiClass::State bluWiFiClass::state;
wifi_scan_config_t bluWiFiClass::_scan_config = {};
esp_ip4_addr_t bluWiFiClass::ip;

void bluWiFiClass::_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  printf("Got event: %s\n", bw_system_event_names[event_id]);
  if (event_id == WIFI_EVENT_SCAN_DONE) {
    uint16_t apCount = 0;
    esp_wifi_scan_get_ap_num(&apCount);
    printf("Found %d access points.\n", apCount);
    wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));
    for (int i = 0; i < apCount; i++) {
      printf("%s: %d, %s", list[i].ssid, list[i].rssi, bw_system_auth_names[list[i].authmode]);
    }
    free(list);
    state = (bluWiFiClass::State)(state | bw_scanned);
    state = (bluWiFiClass::State)(state & ~bw_scanning);
  } else if (event_id == WIFI_EVENT_STA_START) {
    state = (bluWiFiClass::State)(state | bw_init);
    state = (bluWiFiClass::State)(state & ~bw_initing);
    if (state & bw_connect_next) {
      esp_wifi_connect();
      state = (bluWiFiClass::State)(state & ~bw_connect_next);
    }
    if (state & bw_scan_next) {
      ESP_ERROR_CHECK(esp_wifi_scan_start(&_scan_config, false));
      state = (bluWiFiClass::State)(state & ~bw_scan_next);
    }
  } else if (event_id == IP_EVENT_STA_GOT_IP) {
    state = (bluWiFiClass::State)(state | bw_connected);
    state = (bluWiFiClass::State)(state & ~bw_connecting);
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ip = *(&event->ip_info.ip);
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
    uint8_t reason = event->reason;
    printf("Reason: %u - %s\n", reason, reason2str(reason));
    if (state & bw_connecting) {
      if (reason == WIFI_REASON_UNSPECIFIED || reason == WIFI_REASON_AUTH_EXPIRE || reason == 205) { //Common issue, reconnect
        esp_wifi_connect();
      } else {
        state = (bluWiFiClass::State)(state & ~bw_connecting); //Signal that connect failed.
      }
    } else {
      state = (bluWiFiClass::State)(state & ~bw_connected);
    }
  }
}

bluWiFiClass bluWiFi;
