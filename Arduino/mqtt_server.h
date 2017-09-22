#ifndef _MQTT_SERVER_H_
#define _MQTT_SERVER_H_

#include "user_interface.h"
extern "C" {

typedef void (*MqttDataCallback)(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t lengh);
typedef bool (*MqttAuthCallback)(const char* username, const char *password, struct espconn *pesp_conn);
typedef bool (*MqttConnectCallback)(struct espconn *pesp_conn);

bool MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics);
void MQTT_server_onData(MqttDataCallback dataCb);
void MQTT_server_onAuth(MqttAuthCallback authCb);
void MQTT_server_onConnect(MqttConnectCallback connectCb);

bool MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain);
bool MQTT_local_subscribe(uint8_t* topic, uint8_t qos);
bool MQTT_local_unsubscribe(uint8_t* topic);

}

#endif /* _MQTT_SERVER_H_ */
