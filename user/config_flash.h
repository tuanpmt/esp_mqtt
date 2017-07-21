#ifndef _CONFIG_FLASH_H_
#define _CONFIG_FLASH_H_

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "spi_flash.h"

#include "user_config.h"

#define FLASH_BLOCK_NO 0xc

#define MAGIC_NUMBER    0x015005fc

typedef struct
{
    // To check if the structure is initialized or not in flash
    uint32_t    magic_number;

    // Length of the structure, since this is a evolving library, the variant may change
    // hence used for verification
    uint16_t     length;

    /* Below variables are specific to my code */
    uint8_t     ssid[32];       // SSID of the AP to connect to
    uint8_t     password[64];   // Password of the network
    uint8_t     auto_connect;   // Should we auto connect

    uint8_t     ap_ssid[32];       // SSID of the own AP
    uint8_t     ap_password[64];   // Password of the own network
    uint8_t     ap_open;           // Should we use no WPA?
    uint8_t	ap_on;		   // AP enabled?

    uint8_t     locked;		// Should we allow for config changes
    uint8_t     lock_password[32];   // Password of config lock
    ip_addr_t	network_addr;	// Address of the internal network
    ip_addr_t	dns_addr;	// Optional: address of the dns server

    ip_addr_t	my_addr;	// Optional (if not DHCP): IP address of the uplink side
    ip_addr_t	my_netmask;	// Optional (if not DHCP): IP netmask of the uplink side
    ip_addr_t	my_gw;		// Optional (if not DHCP): Gateway of the uplink side

    uint16_t	clock_speed;	// Freq of the CPU
    uint16_t	config_port;	// Port on which the concole listenes (0 if no access)

#ifdef MQTT_CLIENT
    uint8_t     mqtt_host[32];	// IP or hostname of the MQTT broker, "none" if empty
    uint16_t	mqtt_port;	// Port of the MQTT broker

    uint8_t     mqtt_user[32];	// Username for broker login, "none" if empty
    uint8_t     mqtt_password[32]; // Password for broker login
    uint8_t	mqtt_id[32];    // MQTT clientId
#endif
#ifdef NTP
    uint8_t	ntp_server[32];	// IP or hostname of the MQTT broker, "none" if empty
    uint32_t	ntp_interval;	// Sync interval in usec
    int16_t	ntp_timezone;	// Timezone (hour offset to GMT)
#endif
} sysconfig_t, *sysconfig_p;

int config_load(sysconfig_p config);
void config_save(sysconfig_p config);

void blob_save(uint8_t blob_no, uint32_t *data, uint16_t len);
void blob_load(uint8_t blob_no, uint32_t *data, uint16_t len);
void blob_zero(uint8_t blob_no, uint16_t len);

#endif
