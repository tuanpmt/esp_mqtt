#ifndef _MQTT_SERVER_H_
#define _MQTT_SERVER_H_

#include "c_types.h"
#include "osapi.h"
#include "os_type.h"
#include "ip_addr.h"
#include "espconn.h"
//#include "lwip/ip.h"
//#include "lwip/app/espconn.h"
//#include "lwip/app/espconn_tcp.h"

#include "mqtt.h"

#define LOCAL_MQTT_CLIENT ((void*)-1)

typedef bool (*MqttAuthCallback)(const char* username, const char *password, struct espconn *pesp_conn);
typedef bool (*MqttConnectCallback)(struct espconn *pesp_conn, uint16_t client_count);

typedef struct _MQTT_ClientCon {
  struct espconn *pCon;
//  uint8_t security;
//  uint32_t port;
//  ip_addr_t ip;
  mqtt_state_t  mqtt_state;
  mqtt_connect_info_t connect_info;
//  MqttCallback connectedCb;
//  MqttCallback disconnectedCb;
//  MqttCallback publishedCb;
//  MqttCallback timeoutCb;
//  MqttDataCallback dataCb;
  ETSTimer mqttTimer;
  uint32_t sendTimeout;
  tConnState connState;
  QUEUE msgQueue;
  uint8_t protocolVersion;
  void* user_data;
  struct _MQTT_ClientCon *next;
} MQTT_ClientCon;

extern MQTT_ClientCon *clientcon_list;

uint16_t MQTT_server_countClientCon();
void MQTT_server_disconnectClientCon(MQTT_ClientCon *mqttClientCon);
bool MQTT_server_deleteClientCon(MQTT_ClientCon *mqttClientCon);

bool MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics);
void MQTT_server_onConnect(MqttConnectCallback connectCb);
void MQTT_server_onAuth(MqttAuthCallback authCb);
void MQTT_server_onData(MqttDataCallback dataCb);

bool MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain);
bool MQTT_local_subscribe(uint8_t* topic, uint8_t qos);
bool MQTT_local_unsubscribe(uint8_t* topic);

#endif /* _MQTT_SERVER_H_ */
