#include "user_interface.h"
#include "mqtt_server.h"
#include "user_config.h"

void ICACHE_FLASH_ATTR user_init() {
    struct station_config stationConf;

    // Initialize the UART
    uart_div_modify(0, UART_CLK_FREQ / 115200);

    os_printf("\r\n\r\nMQTT Broker starting\r\n", espconn_tcp_get_max_con());

    // Setup STA
    wifi_set_opmode(STATIONAP_MODE);
    stationConf.bssid_set = 0;
    os_strcpy(&stationConf.ssid, STA_SSID);
    os_strcpy(&stationConf.password, STA_PASS);
    wifi_station_set_config(&stationConf);
    wifi_station_set_auto_connect(1);

    // Allow larger number of TCP (=MQTT) clients
    espconn_tcp_set_max_con(TCP_MAX_CONNECTIONS);
    os_printf("Max number of TCP clients: %d\r\n", espconn_tcp_get_max_con());

    //Start MQTT broker
    MQTT_server_start(MQTT_PORT, MQTT_MAX_SUBSCRIPTIONS, MQTT_MAX_RETAINED_TOPICS);
}
