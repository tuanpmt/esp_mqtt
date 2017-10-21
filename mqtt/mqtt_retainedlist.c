#include "c_types.h"
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
static on_retainedtopic_cb retained_cb = NULL;

bool ICACHE_FLASH_ATTR create_retainedlist(uint16_t num_entires) {
    max_entry = num_entires;
    retained_list = (retained_entry *) os_zalloc(num_entires * sizeof(retained_entry));
    retained_cb = NULL;
    return retained_list != NULL;
}

bool ICACHE_FLASH_ATTR update_retainedtopic(uint8_t * topic, uint8_t * data, uint16_t data_len, uint8_t qos) {
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
	if (retained_cb != NULL)
	    retained_cb(NULL);
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
    if (retained_cb != NULL)
	retained_cb(&retained_list[i]);

    return true;
}

bool ICACHE_FLASH_ATTR find_retainedtopic(uint8_t * topic, find_retainedtopic_cb cb, void *user_data) {
    uint16_t i;
    bool retval = false;

    if (retained_list == NULL)
	return false;

    for (i = 0; i < max_entry; i++) {
	if (retained_list[i].topic != NULL) {
	    if (Topics_matches(topic, 1, retained_list[i].topic)) {
		(*cb) (&retained_list[i], user_data);
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

bool ICACHE_FLASH_ATTR clear_cb(retained_entry *entry, void *user_data) {
    update_retainedtopic(entry->topic, "", 0, entry->qos);
    return false;
}

void ICACHE_FLASH_ATTR clear_retainedtopics() {
    iterate_retainedtopics(clear_cb, NULL);
}

int ICACHE_FLASH_ATTR serialize_retainedtopics(char *buf, int len) {
    uint16_t i;
    uint16_t pos = 0;

    if (retained_list == NULL)
	return 0;

    for (i = 0; i < max_entry; i++) {
	if (retained_list[i].topic != NULL) {
	    uint16_t data_len = retained_list[i].data_len;
	    if (pos + os_strlen(retained_list[i].topic) + 4 + data_len + 1 >= len-1)
		return 0;
	    os_strcpy(&buf[pos], retained_list[i].topic);
	    pos += os_strlen(retained_list[i].topic) + 1;
	    
	    buf[pos++] = data_len & 0xff;
	    buf[pos++] = (data_len >> 8) & 0xff;
	    os_memcpy(&buf[pos], retained_list[i].data, data_len);
	    pos += data_len;
	    buf[pos++] = retained_list[i].qos;
	    buf[pos] = '\0';
	}
    }

    if (pos == 0) {
	buf[pos++] = '\0';
    }

    return pos;
}

bool ICACHE_FLASH_ATTR deserialize_retainedtopics(char *buf, int len) {
    uint16_t pos = 0;

    while (pos < len && buf[pos] != '\0') {
	uint8_t *topic = &buf[pos];
	pos += os_strlen(topic) + 1;
	if (pos >= len) return false;
	uint16_t data_len = buf[pos++] + (buf[pos++] << 8);
	uint8_t *data = &buf[pos];
	pos += data_len;
	if (pos >= len) return false;
	uint8_t qos = buf[pos++];

	if (update_retainedtopic(topic, data, data_len, qos) == false)
	    return false;
    }
    return true;
}

void ICACHE_FLASH_ATTR set_on_retainedtopic_cb(on_retainedtopic_cb cb) {
    retained_cb = cb;
}
