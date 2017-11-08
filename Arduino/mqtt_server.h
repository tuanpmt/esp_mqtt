#ifndef _MQTT_SERVER_H_
#define _MQTT_SERVER_H_

#include "user_interface.h"
extern "C" {

// Interface for starting the broker

bool MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics);

// Callbacks for message reception, username/password authentication, and client connection

typedef void (*MqttDataCallback)(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t lengh);
typedef bool (*MqttAuthCallback)(const char* username, const char *password, struct espconn *pesp_conn);
typedef bool (*MqttConnectCallback)(struct espconn *pesp_conn, uint16_t client_count);

void MQTT_server_onData(MqttDataCallback dataCb);
void MQTT_server_onAuth(MqttAuthCallback authCb);
void MQTT_server_onConnect(MqttConnectCallback connectCb);

// Interface for local pub/sub interaction with the broker

bool MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain);
bool MQTT_local_subscribe(uint8_t* topic, uint8_t qos);
bool MQTT_local_unsubscribe(uint8_t* topic);

// Interface to cleanup after STA disconnect

void MQTT_server_cleanupClientCons();

// Interface for persistence of retained topics
// Topics can be serialized to a buffer and reinitialized later after reboot
// Application is responsible for saving and restoring that buffer (i.e. to/from flash)

void clear_retainedtopics();
int serialize_retainedtopics(char *buf, int len);
bool deserialize_retainedtopics(char *buf, int len);
}

#endif /* _MQTT_SERVER_H_ */
