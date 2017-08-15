#include "c_types.h"
#include "mem.h"
#include "osapi.h"
#include "user_config.h"

#include "lang.h"
#include "pub_list.h"

typedef struct _pub_entry {
    char *topic;
    char *data;
    uint32_t data_len;
    bool local;
    struct _pub_entry *next;
} pub_entry;

static pub_entry *pub_list = NULL;

void ICACHE_FLASH_ATTR pub_insert(const char* topic, uint32_t topic_len, const char *data, uint32_t data_len, bool local)
{
    pub_entry *pub = (pub_entry *)os_malloc(sizeof(pub_entry));
    if (pub == NULL)
	return;
    pub->topic = (char*)os_malloc(topic_len+1);
    if (pub->topic == NULL) {
	os_free(pub);
	return;
    }
    pub->data = (char*)os_malloc(data_len+1);
    if (pub->data == NULL) {
	os_free(pub->topic);
	os_free(pub);
	return;
    }
    os_memcpy(pub->topic, topic, topic_len);
    pub->topic[topic_len] = '\0';
    os_memcpy(pub->data, data, data_len);
    pub->data_len = data_len;
    pub->data[data_len] = '\0';
    pub->local = local;

    pub->next = pub_list;
    pub_list = pub;
}


void ICACHE_FLASH_ATTR pub_process()
{
    pub_entry **pre_last, *last;

    while (pub_list != NULL) {
	pre_last = &pub_list;
	while ((*pre_last)->next != NULL)
	    pre_last = &((*pre_last)->next);

	last = *pre_last;
	*pre_last = NULL;

	interpreter_topic_received(last->topic, last->data, last->data_len, last->local);

	os_free(last->topic);
	os_free(last->data);
	os_free(last);
    }
}
