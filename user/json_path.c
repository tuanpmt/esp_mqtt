#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"

#include "user_interface.h"
#include "string.h"

#include "json/jsonparse.h"

bool ICACHE_FLASH_ATTR find_next_pair(struct jsonparse_state *state, char *name, int level) {
    int json_type;

    //os_printf ("name: %s level: %d\r\n", name, level);
    while(json_type = jsonparse_next(state)) {
	//os_printf ("json_type: %d json_level: %d\r\n", json_type, state->depth);
	if (state->depth < level-1)
	    return false;

	if (state->depth == level && json_type == JSON_TYPE_PAIR_NAME &&
	    jsonparse_strcmp_value(state, name) == 0)
	    return true;
    }
    return false;
}

bool ICACHE_FLASH_ATTR find_index(struct jsonparse_state *state, int index, int level) {
    int json_type;

    //os_printf ("index: %d level: %d\r\n", index, level);
    index++;
    while(index > 0 && (json_type = jsonparse_next(state))) {
	//os_printf ("json_type: %d json_level: %d index: %d\r\n", json_type, state->depth, index);
	if (state->depth < level-1)
	    return false;

	if (state->depth == level && (json_type == JSON_TYPE_ARRAY || json_type == ',')) {
	    index--;
	}
    }
    return index == 0;
}

void ICACHE_FLASH_ATTR json_path(char *json, char *path, char *buf, int *buf_size)
{
    char *p;
    char *ids[JSONPARSE_MAX_DEPTH];
    char tmppath[strlen(path)+1];
    int i_max = 0;

    os_strcpy(tmppath, path);
    ids[i_max++] = tmppath;
    for (p=tmppath; *p != '\0'; p++) {
	if (*p == '.') {
	    *p = '\0';
	    ids[i_max++] = p+1;
	}
	if (*p == '[') {
	    *p = '\0';
	    ids[i_max++] = p+1;
	}
    }

    int i;
    int level = 1;
    struct jsonparse_state state;
    int json_type;
    bool hit;
    int array_count = -1;
    jsonparse_setup(&state, json, os_strlen(json));

    if (*buf_size > 0)
	buf[0] = '\0';

    for (i = 0, hit = true; hit && i<i_max; i++) {
	if (isdigit(ids[i][0]))
	    hit = find_index(&state, atoi(ids[i]), level);
	else
	    hit = find_next_pair(&state, ids[i], level);
	level += 2;
    }

    if (!hit) {
	*buf_size = 0;
	return;
    }
    level -= 2;

    while (json_type = jsonparse_next(&state)) {
	//os_printf ("level: %d json_type: %d json_level: %d\r\n", level, json_type, state.depth);
	if (state.depth < level) {
	    *buf_size = 0;
	    return;
	}

	if (json_type == JSON_TYPE_STRING || json_type == JSON_TYPE_INT ||
	    json_type == JSON_TYPE_NUMBER) {
	    jsonparse_copy_value(&state, buf, *buf_size);
	    *buf_size = jsonparse_get_len(&state);
	    return;
	}
    }

    *buf_size = 0;
    return;
}
