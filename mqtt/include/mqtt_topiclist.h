#ifndef _MQTT_TOPICLIST_H_
#define _MQTT_TOPICLIST_H_

#include "mqtt_server.h"

typedef struct _topic_entry {
    MQTT_ClientCon *clientcon;
    uint8_t *topic;
    uint8_t qos;
} topic_entry;

typedef bool (*iterate_topic_cb)(topic_entry *topic, void *user_data);
typedef bool (*find_topic_cb)(topic_entry *topic_e, uint8_t *topic, uint8_t *data, uint16_t data_len);

bool create_topiclist(uint16_t num_entires);
bool add_topic(MQTT_ClientCon *clientcon, uint8_t *topic, uint8_t qos);
bool delete_topic(MQTT_ClientCon *clientcon, uint8_t *topic);
bool find_topic(uint8_t *topic, find_topic_cb cb, uint8_t *data, uint16_t data_len);
void iterate_topics(iterate_topic_cb cb, void *user_data);

#endif /* _MQTT_TOPICLIST_H_ */
