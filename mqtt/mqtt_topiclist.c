#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"

#include <string.h>
#include "user_config.h"

#include "mqtt_topiclist.h"
#include "mqtt_topics.h"

static topic_entry *topic_list = NULL;
static uint16_t max_entry;

bool ICACHE_FLASH_ATTR create_topiclist(uint16_t num_entires)
{
  max_entry = num_entires;
  topic_list = (topic_entry *)os_zalloc(num_entires * sizeof(topic_entry));
  return topic_list != NULL;
}

bool ICACHE_FLASH_ATTR add_topic(MQTT_ClientCon *clientcon, uint8_t *topic, uint8_t qos)
{
uint16_t i;

  if (topic_list == NULL) return false;
  if (!Topics_isValidName(topic)) return false;

  for (i=0; i<max_entry; i++) {
    if (topic_list[i].clientcon == NULL) {
      topic_list[i].topic = (uint8_t*)os_malloc(os_strlen(topic)+1);
      if (topic_list[i].topic == NULL)
        return false;
      os_strcpy(topic_list[i].topic, topic);
      topic_list[i].clientcon = clientcon;
      topic_list[i].qos = qos;
      return true;
    }
  }
  return false;
}

bool ICACHE_FLASH_ATTR delete_topic(MQTT_ClientCon *clientcon, uint8_t *topic){
uint16_t i;

  if (topic_list == NULL) return false;

  for (i=0; i<max_entry; i++) {
    if (topic_list[i].clientcon != NULL && (clientcon == NULL || topic_list[i].clientcon == clientcon)) {
      if (topic == NULL || (topic_list[i].topic != NULL && strcmp(topic, topic_list[i].topic)==0)) {
        topic_list[i].clientcon = NULL;
        os_free(topic_list[i].topic);
        topic_list[i].qos = 0;
      }
    }
  }
  return true;
}

bool ICACHE_FLASH_ATTR find_topic(uint8_t *topic, find_topic_cb cb, uint8_t *data, uint16_t data_len)
{
uint16_t i;
bool retval = false;

  if (topic_list == NULL) return false;

  for (i=0; i<max_entry; i++) {
    if (topic_list[i].clientcon != NULL) {
      if (Topics_matches(topic_list[i].topic, 1, topic)) {
        (*cb) (&topic_list[i], topic, data, data_len);
        retval = true;
      }
    }
  }
  return retval;
}

void ICACHE_FLASH_ATTR iterate_topics(iterate_topic_cb cb, void *user_data)
{
uint16_t i;

  if (topic_list == NULL) return;

  for (i=0; i<max_entry; i++) {
    if (topic_list[i].clientcon != NULL) {
      if ((*cb) (&topic_list[i], user_data) == true)
	return;
    }
  }
}
