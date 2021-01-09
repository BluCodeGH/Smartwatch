#ifndef bluWiFi_h
#define bluWiFi_h
#include "esp_err.h"
extern "C" {
  #include "esp_wifi.h"
}

class bluWiFiClass {
public:
  bluWiFiClass();
  ~bluWiFiClass();
  void init();
  void scan();
  void connect(char* ssid, char* password);
  void connect(const char* ssid, const char* password);
  void disconnect();
  void deinit();
  static esp_ip4_addr_t ip;

  enum State {
    bw_init = 1,
    bw_scan_next = 1 << 1,
    bw_scanned = 1 << 2,
    bw_connect_next = 1 << 3,
    bw_connected = 1 << 4,
    bw_connecting = 1 << 5,
    bw_scanning = 1 << 6,
    bw_initing = 1 << 7
  };
  static State state;
  static wifi_scan_config_t _scan_config;

  static void _event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

extern bluWiFiClass bluWiFi;
#endif
