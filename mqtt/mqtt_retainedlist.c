#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"

#include <string.h>
//#include "user_config.h"

#include "mqtt_retainedlist.h"
#include "mqtt_topics.h"

static retained_entry *retained_list = NULL;
static uint16_t max_entry;

bool ICACHE_FLASH_ATTR create_retainedlist(uint16_t num_entires) {
    max_entry = num_entires;
    retained_list = (retained_entry *) os_zalloc(num_entires * sizeof(retained_entry));
    return retained_list != NULL;
}

bool update_retainedtopic(uint8_t * topic, uint8_t * data, uint16_t data_len, uint8_t qos) {
    uint16_t i;

    if (retained_list == NULL)
	return false;

    // look for topic in list
    for (i = 0; i < max_entry; i++) {
	if (retained_list[i].topic != NULL && os_strcmp(retained_list[i].topic, topic) == 0)
	    break;
    }

    // not yet in list
    if (i >= max_entry) {

	// if empty new data - no entry required
	if (data_len == 0)
	    return true;

	// find free
	for (i = 0; i < max_entry; i++) {
	    if (retained_list[i].topic == NULL)
		break;
	}
	if (i >= max_entry) {
	    // list full
	    return false;
	}
	retained_list[i].topic = (uint8_t *) os_malloc(os_strlen(topic) + 1);
	if (retained_list[i].topic == NULL) {
	    // out of mem
	    return false;
	}
	os_strcpy(retained_list[i].topic, topic);
    }
    // if empty new data - delete
    if (data_len == 0) {
	os_free(retained_list[i].topic);
	retained_list[i].topic = NULL;
	os_free(retained_list[i].data);
	retained_list[i].data = NULL;
	retained_list[i].data_len = 0;
	return true;
    }

    if (retained_list[i].data == NULL) {
	// no data till now, new memory allocation
	retained_list[i].data = (uint8_t *) os_malloc(data_len);
    } else {
	if (data_len != retained_list[i].data_len) {
	    // not same size as before, new memory allocation
	    os_free(retained_list[i].data);
	    retained_list[i].data = (uint8_t *) os_malloc(data_len);
	}
    }
    if (retained_list[i].data == NULL) {
	// out of mem
	os_free(retained_list[i].topic);
	retained_list[i].topic = NULL;
	retained_list[i].data_len = 0;
	return false;
    }

    os_memcpy(retained_list[i].data, data, data_len);
    retained_list[i].data_len = data_len;
    retained_list[i].qos = qos;

    return true;
}

bool ICACHE_FLASH_ATTR find_retainedtopic(uint8_t * topic, find_retainedtopic_cb cb, MQTT_ClientCon * clientcon) {
    uint16_t i;
    bool retval = false;

    if (retained_list == NULL)
	return false;

    for (i = 0; i < max_entry; i++) {
	if (retained_list[i].topic != NULL) {
	    if (Topics_matches(topic, 1, retained_list[i].topic)) {
		(*cb) (&retained_list[i], clientcon);
		retval = true;
	    }
	}
    }
    return retval;
}

void ICACHE_FLASH_ATTR iterate_retainedtopics(iterate_retainedtopic_cb cb, void *user_data) {
    uint16_t i;

    if (retained_list == NULL)
	return;

    for (i = 0; i < max_entry; i++) {
	if (retained_list[i].topic != NULL) {
	    if ((*cb) (&retained_list[i], user_data) == true)
		return;
	}
    }
}
