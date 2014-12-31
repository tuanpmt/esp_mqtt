/*
 * config.h
 *
 *  Created on: Dec 6, 2014
 *      Author: Minh
 */

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_
#include "os_type.h"
#include "user_config.h"
typedef struct{
	uint32_t cfg_holder;
	uint8_t device_id[16];

	uint8_t sta_ssid[32];
	uint8_t sta_pwd[32];
	uint32_t sta_type;

	uint8_t mqtt_host[64];
	uint32_t mqtt_port;
	uint8_t mqtt_user[32];
	uint8_t mqtt_pass[32];
	uint32_t mqtt_keepalive;
} SYSCFG;

typedef struct {
    uint8 flag;
    uint8 pad[3];
} SAVE_FLAG;

void CFG_Save();
void CFG_Load();

extern SYSCFG sysCfg;

#endif /* USER_CONFIG_H_ */
