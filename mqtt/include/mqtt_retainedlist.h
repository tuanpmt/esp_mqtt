#ifndef _MQTT_RETAINEDLIST_H_
#define _MQTT_RETAINEDLIST_H_

#include "mqtt_server.h"

typedef struct _retained_entry {
    uint8_t *topic;
    uint8_t *data;
    uint16_t data_len;
    uint8_t qos;
} retained_entry;

typedef bool (*iterate_retainedtopic_cb)(retained_entry *topic, void *user_data);
typedef bool (*find_retainedtopic_cb)(retained_entry *topic, void *user_data);
typedef void (*on_retainedtopic_cb)(retained_entry *topic);

bool create_retainedlist(uint16_t num_entires);
void clear_retainedtopics();
bool update_retainedtopic(uint8_t *topic, uint8_t *data, uint16_t data_len, uint8_t qos);
bool find_retainedtopic(uint8_t *topic, find_retainedtopic_cb cb, void *user_data);
void iterate_retainedtopics(iterate_retainedtopic_cb cb, void *user_data);

int serialize_retainedtopics(char *buf, int len);
bool deserialize_retainedtopics(char *buf, int len);

void set_on_retainedtopic_cb(on_retainedtopic_cb cb);

#endif /* _MQTT_RETAINEDLIST_H_ */
