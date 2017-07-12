#ifndef _USER_CONFIG_
#define _USER_CONFIG_

typedef enum {SIG_DO_NOTHING=0, SIG_START_SERVER=1, SIG_SEND_DATA, SIG_UART0, SIG_CONSOLE_RX, SIG_CONSOLE_TX, SIG_GPIO_INT} USER_SIGNALS;

#define WIFI_SSID            "ssid"
#define WIFI_PASSWORD        "password"

#define WIFI_AP_SSID         "MyAP"
#define WIFI_AP_PASSWORD     "none"

#define MAX_CLIENTS	     8

//
// Here the MQTT stuff
//

// Define this if you want to have it work as a MQTT client
#define MQTT_CLIENT 	1	

#define MQTT_BUF_SIZE   1024
#define MQTT_KEEPALIVE    120  /*seconds*/
#define MQTT_RECONNECT_TIMEOUT  5 /*seconds*/
//#define PROTOCOL_NAMEv31  /*MQTT version 3.1 compatible with Mosquitto v0.15*/
#define PROTOCOL_NAMEv311     /*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define MQTT_ID "ESPBroker"

//
// Define this if you want to have NTP support.
//
#define NTP	  1

//
// Size of the console buffers
//
#define MAX_CON_SEND_SIZE    1024
#define MAX_CON_CMD_SIZE     80

//
// Define this to support the "scan" command for AP search
//
#define ALLOW_SCANNING      1

//
// Define this if you want to have access to the config console via TCP.
// Ohterwise only local access via serial is possible
//
#define REMOTE_CONFIG      1
#define CONSOLE_SERVER_PORT  7777

#endif
