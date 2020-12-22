#ifndef __MQTT_CONFIG_H__
#define __MQTT_CONFIG_H__

#include "mqtt.h"

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST      "192.168.0.101" //or "mqtt.yourdomain.com"
#define MQTT_CLIENT_ID "CLIENT_1234"
#define MQTT_USER      "USER"
#define MQTT_PASS      "PASS"

#define MQTT_BUF_SIZE  1024
#define MQTT_SSL_SIZE  3072
#define MQTT_KEEPALIVE 120  /*second*/
#define MQTT_CLEAN_SESSION 1
#define MQTT_RECONNECT_TIMEOUT 5 /*second*/
#define QUEUE_BUFFER_SIZE 2048


#define STA_SSID "SSID"
#define STA_PASS "PASS"


//#define DEFAULT_SECURITY SEC_NONSSL // disable SSL/TLS
#define DEFAULT_SECURITY SEC_SSL_WITHOUT_AUTH // enable SSL/TLS, but there is no a certificate verify
//#define DEFAULT_SECURITY SEC_SSL_ONE_WAY_AUTH // enable SSL/TLS, ESP8266 would verify the SSL server certificate at the same time
//#define DEFAULT_SECURITY SEC_SSL_TWO_WAY_AUTH // enable SSL/TLS, ESP8266 would verify the SSL server certificate and SSL server would verify ESP8266 certificate

#if DEFAULT_SECURITY == SEC_NONSSL
#define MQTT_PORT 1883
#else
#define MQTT_PORT 8883
#define MQTT_SSL_ENABLE
#define CA_CERT_FLASH_ADDRESS 0x77 // CA certificate address in flash to read, 0x77 means address 0x77000
#define CLIENT_CERT_FLASH_ADDRESS 0x78 // client certificate and private key address in flash to read, 0x78 means address 0x78000
// also add mbedtls to the Makefile LIBS
#endif

//#define PROTOCOL_NAMEv31  /*MQTT version 3.1 compatible with Mosquitto v0.15*/
#define PROTOCOL_NAMEv311     /*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#if defined(DEBUG_ON)
#define INFO( format, ... ) os_printf( format, ## __VA_ARGS__ )
#else
#define INFO( format, ... )
#endif

#endif // __MQTT_CONFIG_H__
