#ifndef __MQTT_CONFIG_H__
#define __MQTT_CONFIG_H__

/*DEFAULT CONFIGURATIONS*/

#define MQTT_PORT     1883
#define MQTT_BUF_SIZE   1024
#define MQTT_KEEPALIVE    120  /*second*/
#define MQTT_RECONNECT_TIMEOUT  5 /*seconds*/

#define MQTT_MAX_SUBSCRIPTIONS		30
#define MQTT_MAX_RETAINED_TOPICS	30


#define TCP_MAX_CONNECTIONS	10
#define STA_SSID "SSID"
#define STA_PASS "PASSWD"


#define PROTOCOL_NAMEv31  /*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311     /*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

typedef enum {SIG_DO_NOTHING=0, SIG_UART0, SIG_CONSOLE_RX, SIG_CONSOLE_TX} USER_SIGNALS;

#endif // __MQTT_CONFIG_H__
