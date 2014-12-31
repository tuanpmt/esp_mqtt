/*
 * at_mqtt.h
 *
 *  Created on: Nov 28, 2014
 *      Author: Minh Tuan
 */

#ifndef USER_AT_MQTT_H_
#define USER_AT_MQTT_H_
#include "mqtt_msg.h"
#include "str_queue.h"
typedef struct mqtt_event_data_t
{
  uint8_t type;
  const char* topic;
  const char* data;
  uint16_t topic_length;
  uint16_t data_length;
  uint16_t data_offset;
} mqtt_event_data_t;

typedef struct mqtt_state_t
{
  uint16_t port;
  int auto_reconnect;
  mqtt_connect_info_t* connect_info;
  uint8_t* in_buffer;
  uint8_t* out_buffer;
  int in_buffer_length;
  int out_buffer_length;
  uint16_t message_length;
  uint16_t message_length_read;
  mqtt_message_t* outbound_message;
  mqtt_connection_t mqtt_connection;
  uint16_t pending_msg_id;
  int pending_msg_type;
} mqtt_state_t;

typedef enum {
	WIFI_INIT,
	WIFI_CONNECTING,
	WIFI_CONNECTING_ERROR,
	WIFI_CONNECTED,
	DNS_RESOLVE,
	TCP_DISCONNECTED,
	TCP_RECONNECT_REQ,
	TCP_RECONNECT,
	TCP_CONNECTING,
	TCP_CONNECTING_ERROR,
	TCP_CONNECTED,
	MQTT_CONNECT_SEND,
	MQTT_CONNECT_SENDING,
	MQTT_SUBSCIBE_SEND,
	MQTT_SUBSCIBE_SENDING,
	MQTT_DATA,
	MQTT_PUBLISH_RECV,
	MQTT_PUBLISHING
} tConnState;

typedef void (*MqttCallback)(uint32_t *args);
typedef void (*MqttDataCallback)(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t lengh);

typedef struct  {
	struct espconn *pCon;
	uint8_t security;
	uint8_t* host;
	uint32_t port;
	ip_addr_t ip;
	mqtt_state_t  mqtt_state;
	mqtt_connect_info_t connect_info;
	MqttCallback connectedCb;
	MqttCallback disconnectedCb;
	MqttDataCallback dataCb;
	ETSTimer mqttTimer;
	uint32_t keepAliveTick;
	uint32_t reconnectTick;
	tConnState connState;
	STR_QUEUE topicQueue;
} MQTT_Client;

#define SEC_NONSSL 0
#define SEC_SSL	1

#define MQTT_FLAG_CONNECTED 	1
#define MQTT_FLAG_READY 		2
#define MQTT_FLAG_EXIT 			4

#define MQTT_EVENT_TYPE_NONE 			0
#define MQTT_EVENT_TYPE_CONNECTED 		1
#define MQTT_EVENT_TYPE_DISCONNECTED 	2
#define MQTT_EVENT_TYPE_SUBSCRIBED 		3
#define MQTT_EVENT_TYPE_UNSUBSCRIBED 	4
#define MQTT_EVENT_TYPE_PUBLISH 		5
#define MQTT_EVENT_TYPE_PUBLISHED 		6
#define MQTT_EVENT_TYPE_EXITED 			7
#define MQTT_EVENT_TYPE_PUBLISH_CONTINUATION 8

void MQTT_InitConnection(MQTT_Client *mqttClient, uint8_t* host, uint32 port, uint8_t security);
void MQTT_InitClient(MQTT_Client *mqttClient, uint8_t* client_id, uint8_t* client_user, uint8_t* client_pass, uint32_t keepAliveTime);
void MQTT_OnConnected(MQTT_Client *mqttClient, MqttCallback connectedCb);
void MQTT_OnDisconnected(MQTT_Client *mqttClient, MqttCallback disconnectedCb);
void MQTT_OnData(MQTT_Client *mqttClient, MqttDataCallback dataCb);
void MQTT_Subscribe(MQTT_Client *client, char* topic);
void MQTT_Connect(MQTT_Client *mqttClient);
void MQTT_Publish(MQTT_Client *client, const char* topic, const char* data, int data_length, int qos, int retain);

#endif /* USER_AT_MQTT_H_ */
