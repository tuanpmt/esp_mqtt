#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"

#include "user_interface.h"
#include "string.h"
#include "driver/uart.h"

#include "ringbuf.h"
#include "user_config.h"
#include "config_flash.h"
#include "sys_time.h"

#include "mqtt_server.h"
#include "mqtt_topiclist.h"
#include "mqtt_retainedlist.h"

#ifdef GPIO
//#include "easygpio.h"
#include "pwm.h"
#define PWM_CHANNELS 5
const uint32_t period = 5000; // * 200ns ^= 1 kHz
#endif

#ifdef NTP
#include "ntp.h"
uint64_t t_ntp_resync = 0;
#endif

#ifdef MDNS
static struct mdns_info mdnsinfo;
#endif

#ifdef SCRIPTED
#include "lang.h"
#include "pub_list.h"

struct espconn *downloadCon;
struct espconn *scriptcon;
uint8_t *load_script;
uint32_t load_size;
bool timestamps_init;
#endif

/* System Task, for signals refer to user_config.h */
#define user_procTaskPrio        0
#define user_procTaskQueueLen    2
os_event_t user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t * events);

static os_timer_t ptimer;

/* Some stats */
uint64_t t_old;

/* Hold the system wide configuration */
sysconfig_t config;

static ringbuf_t console_rx_buffer, console_tx_buffer;

static ip_addr_t my_ip;
static ip_addr_t dns_ip;
bool connected;
uint8_t my_channel;
bool do_ip_config;

void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void);
void ICACHE_FLASH_ATTR user_set_softap_ip_config(void);

uint8_t remote_console_disconnect;
struct espconn *console_conn;
bool client_sent_pending;


void ICACHE_FLASH_ATTR to_console(char *str) {
    ringbuf_memcpy_into(console_tx_buffer, str, os_strlen(str));
}

bool ICACHE_FLASH_ATTR check_connection_access(struct espconn *pesp_conn, uint8_t access_flags) {
    remot_info *premot = NULL;
    ip_addr_t *remote_addr;
    bool is_local;

    remote_addr = (ip_addr_t *)&(pesp_conn->proto.tcp->remote_ip);
    //os_printf("Remote addr is %d.%d.%d.%d\r\n", IP2STR(remote_addr));
    is_local = (remote_addr->addr & 0x00ffffff) == (config.network_addr.addr & 0x00ffffff);

    if (is_local && (access_flags & LOCAL_ACCESS))
	return true;
    if (!is_local && (access_flags & REMOTE_ACCESS))
	return true;

    return false;
}

#ifdef MQTT_CLIENT

MQTT_Client mqttClient;
bool mqtt_enabled, mqtt_connected;

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t * args) {
    uint8_t ip_str[16];

    MQTT_Client *client = (MQTT_Client *) args;
    mqtt_connected = true;
#ifdef SCRIPTED
    interpreter_mqtt_connect();
#endif
    os_printf("MQTT client connected\r\n");
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t * args) {
    MQTT_Client *client = (MQTT_Client *) args;
    mqtt_connected = false;
    os_printf("MQTT client disconnected\r\n");
}

static void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t * args) {
    MQTT_Client *client = (MQTT_Client *) args;
//  os_printf("MQTT: Published\r\n");
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t * args, const char *topic,
					 uint32_t topic_len, const char *data, uint32_t data_len) {
#ifdef SCRIPTED
    MQTT_Client *client = (MQTT_Client *) args;

    char *topic_copy = (char*)os_malloc(topic_len+1);
    if (topic_copy == NULL)
	return;
    os_memcpy(topic_copy, topic, topic_len);
    topic_copy[topic_len] = '\0';

    interpreter_topic_received(topic_copy, data, data_len, false);

    os_free(topic_copy);

    // Any local topics to process as result?
    pub_process();

#endif
}
#endif				/* MQTT_CLIENT */

#ifdef SCRIPTED
static void ICACHE_FLASH_ATTR script_recv_cb(void *arg, char *data, unsigned short length) {
    struct espconn *pespconn = (struct espconn *)arg;
    int index;
    uint8_t ch;

    for (index = 0; index < length; index++) {
	ch = *(data + index);
	//os_printf("%c", ch);
	if (load_size < MAX_SCRIPT_SIZE - 5)
	    load_script[4 + load_size++] = ch;
    }
}

void ICACHE_FLASH_ATTR http_script_cb(char *response_body, int http_status, char *response_headers, int body_size) {
    char response[64];

    if (http_status != 200) {
	os_sprintf(response, "\rHTTP script upload failed (error code %d)\r\n", http_status);
	to_console(response);
	return;
    }

    if (body_size > MAX_SCRIPT_SIZE-5) {
	os_sprintf(response, "\rHTTP script upload failed (script too long)\r\n");
	to_console(response);
	return;
    }

    char *load_script = (char *)os_malloc(body_size+5);
    if (load_script == NULL) {
	os_sprintf(response, "\rHTTP script upload failed (out of memory)\r\n");
	to_console(response);
	return;
    }
    //os_printf("LOAD: %d %x::%s\r\n", body_size, load_script, response_body);
    os_memcpy(&load_script[4], response_body, body_size);
    load_script[4 + body_size] = '\0';
    *(uint32_t *) load_script = body_size + 5;
    blob_save(0, (uint32_t *) load_script, body_size + 5);;
    os_free(load_script);
    blob_zero(1, MAX_FLASH_SLOTS * FLASH_SLOT_LEN);

    os_sprintf(response, "\rHTTP script download completed (%d Bytes)\r\n", body_size);
    to_console(response);

    system_os_post(user_procTaskPrio, SIG_SCRIPT_HTTP_LOADED, (ETSParam) scriptcon);
}

static void ICACHE_FLASH_ATTR script_discon_cb(void *arg) {
    char response[64];

    load_script[4 + load_size] = '\0';
    *(uint32_t *) load_script = load_size + 5;
    blob_save(0, (uint32_t *) load_script, load_size + 5);
    os_free(load_script);
    blob_zero(1, MAX_FLASH_SLOTS * FLASH_SLOT_LEN);

    os_sprintf(response, "\rScript upload completed (%d Bytes)\r\n", load_size);
    to_console(response);

    system_os_post(user_procTaskPrio, SIG_SCRIPT_LOADED, (ETSParam) scriptcon);
}

/* Called when a client connects to the script server */
static void ICACHE_FLASH_ATTR script_connected_cb(void *arg) {
    char response[64];
    struct espconn *pespconn = (struct espconn *)arg;

    load_script = (uint8_t *) os_malloc(MAX_SCRIPT_SIZE);
    load_size = 0;

    //espconn_regist_sentcb(pespconn,     tcp_client_sent_cb);
    espconn_regist_disconcb(pespconn, script_discon_cb);
    espconn_regist_recvcb(pespconn, script_recv_cb);
    espconn_regist_time(pespconn, 300, 1);
}

uint32_t ICACHE_FLASH_ATTR get_script_size(void) {
    uint32_t size;

    blob_load(0, &size, 4);
    return size;
}

uint8_t *my_script = NULL;
uint32_t ICACHE_FLASH_ATTR read_script(void) {
    uint32_t size = get_script_size();
    if (size <= 5)
	return 0;

    my_script = (uint8_t *) os_malloc(size);

    if (my_script == 0) {
	os_printf("Out of memory");
	return 0;
    }

    blob_load(0, (uint32_t *) my_script, size);

    uint32_t num_token = text_into_tokens(my_script + 4);

    if (num_token == 0) {
	os_free(my_script);
	my_script = NULL;
    }
    return num_token;
}

void ICACHE_FLASH_ATTR free_script(void) {
    if (my_script != NULL) {
	free_tokens();
	os_free(my_script);
    }
    my_script = NULL;
}
#endif				/* SCRIPTED */

int ICACHE_FLASH_ATTR parse_str_into_tokens(char *str, char **tokens, int max_tokens)
{
char    *p, *q, *end;
int     token_count = 0;
bool    in_token = false;

   // preprocessing
   for (p = q = str; *p != 0; p++) {
	if (*(p) == '%' && *(p+1) != 0 && *(p+2) != 0) {
	   // quoted hex
		uint8_t a;
		p++;
		if (*p <= '9')
		    a = *p - '0';
		else
		    a = toupper(*p) - 'A' + 10;
		a <<= 4;
		p++;
		if (*p <= '9')
		    a += *p - '0';
		else
		    a += toupper(*p) - 'A' + 10;
		*q++ = a;
	} else if (*p == '\\' && *(p+1) != 0) {
	   // next char is quoted - just copy it, skip this one
	   *q++ = *++p;
	} else if (*p == 8) {
	   // backspace - delete previous char
	   if (q != str) q--;
	} else if (*p <= ' ') {
	   // mark this as whitespace
	   *q++ = 0;
	} else {
	   *q++ = *p;
	}
   }

   end = q;
   *q = 0;

   // cut into tokens
   for (p = str; p != end; p++) {
	if (*p == 0) {
	   if (in_token) {
		in_token = false;
	   }
	} else {
	   if (!in_token) {
		tokens[token_count++] = p;
		if (token_count == max_tokens)
		   return token_count;
		in_token = true;
	   }  
	}
   }
   return token_count;
}

void ICACHE_FLASH_ATTR console_send_response(struct espconn *pespconn) {
    char payload[MAX_CON_SEND_SIZE];
    uint16_t len = ringbuf_bytes_used(console_tx_buffer);

    if (len == 0)
	return;

    ringbuf_memcpy_from(payload, console_tx_buffer, len);
    if (pespconn != NULL) {
	if (!client_sent_pending) {
	    espconn_sent(pespconn, payload, len);
	    client_sent_pending = true;
	}
    } else {
	UART_Send(0, &payload, len);
    }
}

#ifdef ALLOW_SCANNING
void ICACHE_FLASH_ATTR scan_done(void *arg, STATUS status) {
    char response[128];

    if (status == OK) {
	struct bss_info *bss_link = (struct bss_info *)arg;

	ringbuf_memcpy_into(console_tx_buffer, "\r", 1);
	while (bss_link != NULL) {
	    os_sprintf(response, "%d,\"%s\",%d,\"" MACSTR "\",%d\r\n",
		       bss_link->authmode, bss_link->ssid, bss_link->rssi, MAC2STR(bss_link->bssid), bss_link->channel);
	    to_console(response);
	    bss_link = bss_link->next.stqe_next;
	}
    } else {
	os_sprintf(response, "scan fail !!!\r\n");
	to_console(response);
    }
    system_os_post(user_procTaskPrio, SIG_CONSOLE_TX, (ETSParam) console_conn);
}
#endif

void ICACHE_FLASH_ATTR con_print(uint8_t *str) {
    ringbuf_memcpy_into(console_tx_buffer, str, os_strlen(str));
    system_os_post(user_procTaskPrio, SIG_CONSOLE_TX_RAW, (ETSParam) console_conn);
}

bool ICACHE_FLASH_ATTR printf_topic(topic_entry * topic, void *user_data) {
    uint8_t *response = (uint8_t *) user_data;

    os_sprintf(response, "%s: \"%s\" (QoS %d)\r\n",
	       topic->clientcon !=
	       LOCAL_MQTT_CLIENT ? topic->clientcon->connect_info.client_id : "local", topic->topic, topic->qos);
    to_console(response);
    return false;
}

bool ICACHE_FLASH_ATTR printf_retainedtopic(retained_entry * entry, void *user_data) {
    uint8_t *response = (uint8_t *) user_data;

    os_sprintf(response, "\"%s\" len: %d (QoS %d)\r\n", entry->topic, entry->data_len, entry->qos);
    to_console(response);
    return false;
}

void MQTT_local_DataCallback(uint32_t * args, const char *topic, uint32_t topic_len, const char *data, uint32_t length) {
    //os_printf("Received: \"%s\" len: %d\r\n", topic, length);
#ifdef SCRIPTED
    //interpreter_topic_received(topic, data, length, true);
    pub_insert(topic, topic_len, data, length, true);
    system_os_post(user_procTaskPrio, SIG_TOPIC_RECEIVED, 0);
#endif
}


static char INVALID_LOCKED[] = "Invalid command. Config locked\r\n";
static char INVALID_NUMARGS[] = "Invalid number of arguments\r\n";
static char INVALID_ARG[] = "Invalid argument\r\n";

void ICACHE_FLASH_ATTR console_handle_command(struct espconn *pespconn) {
#define MAX_CMD_TOKENS 4

    char cmd_line[MAX_CON_CMD_SIZE + 1];
    char response[256];
    char *tokens[MAX_CMD_TOKENS];

    int bytes_count, nTokens;

    bytes_count = ringbuf_bytes_used(console_rx_buffer);
    ringbuf_memcpy_from(cmd_line, console_rx_buffer, bytes_count);

    cmd_line[bytes_count] = 0;
    response[0] = 0;

    nTokens = parse_str_into_tokens(cmd_line, tokens, MAX_CMD_TOKENS);

    if (nTokens == 0) {
	char c = '\n';
	ringbuf_memcpy_into(console_tx_buffer, &c, 1);
	goto command_handled_2;
    }

    if (strcmp(tokens[0], "help") == 0) {
	os_sprintf(response, "show [config|stats|mqtt]\r\nsave\r\nreset [factory]\r\nlock [<password>]\r\nunlock <password>\r\nquit\r\n");
	to_console(response);
#ifdef ALLOW_SCANNING
	os_sprintf(response, "scan\r\n");
	to_console(response);
#endif
	os_sprintf(response, "set [ssid|password|auto_connect|ap_ssid|ap_password|ap_on|ap_open] <val>\r\n");
	to_console(response);
	os_sprintf(response, "set [network|dns|ip|netmask|gw|config_port|config_access] <val>\r\n");
	to_console(response);
	os_sprintf(response, "set [broker_user|broker_password|broker_access] <val>\r\n");
	to_console(response);
	os_sprintf(response, "set [broker_subscriptions|broker_retained_messages] <val>\r\n");
	to_console(response);
	os_sprintf(response, "publish [local|remote] <topic> <data>\r\n");
	to_console(response);
#ifdef SCRIPTED
	os_sprintf(response, "script <port>|<url>|delete\r\nshow [script|vars]\r\n");
	to_console(response);
#ifdef GPIO
#ifdef GPIO_PWM
	os_sprintf(response, "set pwm_period <val>\r\n");
	to_console(response);
#endif
#endif
#endif
#ifdef NTP
	os_sprintf(response, "time\r\nset [ntp_server|ntp_interval|<ntp_timezone> <val>\r\n");
	to_console(response);
#endif
#ifdef MQTT_CLIENT
	os_sprintf(response, "set [mqtt_host|mqtt_port|mqtt_user|mqtt_password|mqtt_id] <val>\r\n");
	to_console(response);
#endif

	goto command_handled_2;
    }

    if (strcmp(tokens[0], "show") == 0) {
	int16_t i;
	ip_addr_t i_ip;

	if (nTokens == 1 || (nTokens == 2 && strcmp(tokens[1], "config") == 0)) {
	    os_sprintf(response, "STA: SSID:%s PW:%s%s\r\n",
		       config.ssid,
		       config.locked ? "***" : (char *)config.password, config.auto_connect ? "" : " [AutoConnect:0]");
	    to_console(response);

	    os_sprintf(response, "AP:  SSID:%s PW:%s%s%s IP:%d.%d.%d.%d/24\r\n",
		       config.ap_ssid,
		       config.locked ? "***" : (char *)config.ap_password,
		       config.ap_open ? " [open]" : "",
		       config.ap_on ? "" : " [disabled]", IP2STR(&config.network_addr));
	    to_console(response);

	    // if static IP, add it
	    os_sprintf(response,
		       config.my_addr.addr ?
		       "Static IP: %d.%d.%d.%d Netmask: %d.%d.%d.%d Gateway: %d.%d.%d.%d\r\n"
		       : "", IP2STR(&config.my_addr), IP2STR(&config.my_netmask), IP2STR(&config.my_gw));
	    to_console(response);
	    // if static DNS, add it
	    os_sprintf(response, config.dns_addr.addr ? "DNS: %d.%d.%d.%d\r\n" : "", IP2STR(&config.dns_addr));
	    to_console(response);
#ifdef MDNS
	    if (config.mdns_mode) {
		os_sprintf(response, "mDNS: %s interface\r\n", config.mdns_mode==1 ? "STA": "SoftAP");
		to_console(response);
	    }
#endif
#ifdef REMOTE_CONFIG
	    if (config.config_port == 0 || config.config_access == 0) {
		os_sprintf(response, "No network console access\r\n");
	    } else {
		os_sprintf(response, "Network console access on port %d (mode %d)\r\n", config.config_port, config.config_access);
	    }
	    to_console(response);
#endif

	    os_sprintf(response, "MQTT broker max. subscription: %d\r\nMQTT broker max. retained messages: %d\r\n",
		       config.max_subscriptions, config.max_retained_messages);
		to_console(response);
	    if (os_strcmp(config.mqtt_broker_user, "none") != 0) {
		os_sprintf(response,
			   "MQTT broker username: %s\r\nMQTT broker password: %s\r\n",
			   config.mqtt_broker_user,
			   config.locked ? "***" : (char *)config.mqtt_broker_password);
		to_console(response);
	    }
	    response[0] = '\0';
	    if (config.mqtt_broker_access == LOCAL_ACCESS)
		os_sprintf(response, "MQTT broker: local access only\r\n");
	    if (config.mqtt_broker_access == REMOTE_ACCESS)
		os_sprintf(response, "MQTT broker: remote access only\r\n");
	    if (config.mqtt_broker_access == 0)
		os_sprintf(response, "MQTT broker: disabled\r\n");
	    to_console(response);
#ifdef MQTT_CLIENT
	    os_sprintf(response, "MQTT client %s\r\n", mqtt_enabled ? "enabled" : "disabled");
	    to_console(response);

	    if (os_strcmp(config.mqtt_host, "none") != 0) {
		os_sprintf(response,
			   "MQTT client host: %s\r\nMQTT client port: %d\r\nMQTT client user: %s\r\nMQTT client password: %s\r\nMQTT client id: %s\r\n",
			   config.mqtt_host, config.mqtt_port, config.mqtt_user,
			   config.locked ? "***" : (char *)config.mqtt_password, config.mqtt_id);
		to_console(response);
	    }
#endif
#ifdef NTP
	    if (os_strcmp(config.ntp_server, "none") != 0) {
		os_sprintf(response,
			   "NTP server: %s (interval: %d s, tz: %d)\r\n",
			   config.ntp_server, config.ntp_interval / 1000000, config.ntp_timezone);
		to_console(response);
	    }
#endif
	    os_sprintf(response, "Clock speed: %d\r\n", config.clock_speed);
	    to_console(response);
	    goto command_handled_2;
	}

	if (nTokens == 2 && strcmp(tokens[1], "stats") == 0) {
	    uint32_t time = (uint32_t) (get_long_systime() / 1000000);
	    int16_t i;

	    os_sprintf(response, "System uptime: %d:%02d:%02d\r\n", time / 3600, (time % 3600) / 60, time % 60);
	    to_console(response);

	    os_sprintf(response, "Free mem: %d\r\n", system_get_free_heap_size());
	    to_console(response);
#ifdef SCRIPTED
	    os_sprintf(response, "Interpreter loop: %d us\r\n", loop_time);
	    to_console(response);
#endif
	    if (connected) {
		os_sprintf(response, "External IP-address: " IPSTR "\r\n", IP2STR(&my_ip));
	    } else {
		os_sprintf(response, "Not connected to AP\r\n");
	    }
	    to_console(response);
	    if (config.ap_on)
		os_sprintf(response, "%d Station%s connected to AP\r\n",
			   wifi_softap_get_station_num(), wifi_softap_get_station_num() == 1 ? "" : "s");
	    else
		os_sprintf(response, "AP disabled\r\n");
	    to_console(response);
#ifdef NTP
	    if (ntp_sync_done()) {
		os_sprintf(response, "NTP synced: %s \r\n", get_timestr());
	    } else {
		os_sprintf(response, "NTP no sync\r\n");
	    }
	    to_console(response);
#endif
	    goto command_handled_2;
	}

	if (nTokens == 2 && strcmp(tokens[1], "mqtt") == 0) {
	    if (config.locked) {
		os_sprintf(response, INVALID_LOCKED);
		goto command_handled;
	    }

	    MQTT_ClientCon *clientcon;
	    int ccnt = 0;

	    os_sprintf(response, "Current clients:\r\n");
	    to_console(response);
	    for (clientcon = clientcon_list; clientcon != NULL; clientcon = clientcon->next, ccnt++) {
		os_sprintf(response, "%s%s", clientcon->connect_info.client_id, clientcon->next != NULL ? ", " : "");
		to_console(response);
	    }
	    os_sprintf(response, "%sCurrent subsriptions:\r\n", ccnt ? "\r\n" : "");
	    to_console(response);
	    iterate_topics(printf_topic, response);
	    os_sprintf(response, "Retained topics:\r\n");
	    to_console(response);
	    iterate_retainedtopics(printf_retainedtopic, response);
#ifdef MQTT_CLIENT
	    os_sprintf(response, "MQTT client %s\r\n", mqtt_connected ? "connected" : "disconnected");
	    to_console(response);
#endif
#ifdef SCRIPTED
	    os_sprintf(response, "Script %s\r\n", script_enabled ? "enabled" : "disabled");
	    to_console(response);
#endif
	    goto command_handled_2;
	}
#ifdef SCRIPTED
	if (nTokens >= 2 && strcmp(tokens[1], "script") == 0) {
	    if (config.locked) {
		os_sprintf(response, INVALID_LOCKED);
		goto command_handled;
	    }

	    uint32_t line_count, char_count, start_line = 1;
	    if (nTokens == 3)
		start_line = atoi(tokens[2]);

	    uint32_t size = get_script_size();
	    if (size == 0)
		goto command_handled;

	    uint8_t *script = (uint8_t *) os_malloc(size);
	    uint8_t *p;
	    bool nl;

	    if (script == 0) {
		os_sprintf(response, "Out of memory");
		goto command_handled;
	    }

	    blob_load(0, (uint32_t *) script, size);

	    p = script + 4;
	    for (line_count = 1; line_count < start_line && *p != 0; p++) {
		if (*p == '\n')
		    line_count++;
	    }
	    nl = true;
	    for (char_count = 0; *p != 0 && char_count < MAX_CON_SEND_SIZE - 20; p++, char_count++) {
		if (nl) {
		    os_sprintf(response, "\r%4d: ", line_count);
		    char_count += 7;
		    to_console(response);
		    line_count++;
		    nl = false;
		}
		ringbuf_memcpy_into(console_tx_buffer, p, 1);
		if (*p == '\n')
		    nl = true;
	    }
	    if (*p == 0) {
		ringbuf_memcpy_into(console_tx_buffer, "\r\n--end--", 9);
	    } else {
		ringbuf_memcpy_into(console_tx_buffer, "...", 3);
	    }
	    ringbuf_memcpy_into(console_tx_buffer, "\r\n", 2);

	    os_free(script);
	    goto command_handled_2;
	}

	if (nTokens >= 2 && strcmp(tokens[1], "vars") == 0) {
	    if (config.locked) {
		os_sprintf(response, INVALID_LOCKED);
		goto command_handled;
	    }
	    int i;

	    if (script_enabled) {
		for (i = 0; i < MAX_VARS; i++) {
		    if (!vars[i].free) {
			os_sprintf(response, "%s: %s\r\n", vars[i].name, vars[i].data);
			to_console(response);
		    }
		}
	    }

	    uint8_t slots[MAX_FLASH_SLOTS*FLASH_SLOT_LEN];
	    blob_load(1, (uint32_t *)slots, sizeof(slots));

	    for (i = 0; i < MAX_FLASH_SLOTS; i++) {
		os_sprintf(response, "@%d: %s\r\n", i+1, &slots[i*FLASH_SLOT_LEN]);
		to_console(response);
	    }
	    goto command_handled_2;
	}
#endif
    }

    if (strcmp(tokens[0], "save") == 0) {
	if (config.locked) {
	    os_sprintf(response, INVALID_LOCKED);
	    goto command_handled;
	}

	if (nTokens == 1 || (nTokens == 2 && strcmp(tokens[1], "config") == 0)) {
	    config_save(&config);
	    os_sprintf(response, "Config saved\r\n");
	    goto command_handled;
	}
    }
#ifdef ALLOW_SCANNING
    if (strcmp(tokens[0], "scan") == 0) {
	wifi_station_scan(NULL, scan_done);
	os_sprintf(response, "Scanning...\r\n");
	goto command_handled;
    }
#endif
#ifdef NTP
    if (strcmp(tokens[0], "time") == 0) {
	os_sprintf(response, "%s %s\r\n", get_weekday(), get_timestr());
	goto command_handled;
    }
#endif
    if (strcmp(tokens[0], "reset") == 0) {
	if (config.locked && pespconn != NULL) {
	    os_sprintf(response, INVALID_LOCKED);
	    goto command_handled;
	}
	if (nTokens == 2 && strcmp(tokens[1], "factory") == 0) {
	    config_load_default(&config);
	    config_save(&config);
#ifdef SCRIPTED
	    // Clear script and vars
	    blob_zero(0, MAX_SCRIPT_SIZE);
	    blob_zero(1, MAX_FLASH_SLOTS * FLASH_SLOT_LEN);
#endif
	}
	os_printf("Restarting ... \r\n");
	system_restart();	// if it works this will not return

	os_sprintf(response, "Reset failed\r\n");
	goto command_handled;
    }

    if (strcmp(tokens[0], "quit") == 0) {
	remote_console_disconnect = 1;
	os_sprintf(response, "Quitting console\r\n");
	goto command_handled;
    }
#ifdef SCRIPTED
    if (strcmp(tokens[0], "script") == 0) {
	uint16_t port;

	if (config.locked) {
	    os_sprintf(response, INVALID_LOCKED);
	    goto command_handled;
	}

	if (nTokens != 2) {
	    os_sprintf(response, INVALID_NUMARGS);
	    goto command_handled;
	}

	if (strcmp(tokens[1], "delete") == 0) {
#ifdef SCRIPTED
#ifdef GPIO
	    stop_gpios();
#endif
#endif
	    script_enabled = false;
	    if (my_script != NULL)
		free_script();
	    blob_zero(0, MAX_SCRIPT_SIZE);
	    blob_zero(1, MAX_FLASH_SLOTS * FLASH_SLOT_LEN);
	    os_sprintf(response, "Script deleted\r\n");
	    goto command_handled;
	}

	if (!isdigit(tokens[1][0])) {
	    scriptcon = pespconn;
	    http_get(tokens[1], "", http_script_cb);
	    os_sprintf(response, "HTTP request to %s started\r\n", tokens[1]);
	    goto command_handled;  
	}

	port = atoi(tokens[1]);
	if (port == 0) {
	    os_sprintf(response, "Invalid port\r\n");
	    goto command_handled;
	}
	// delete and disable existing script
#ifdef SCRIPTED
#ifdef GPIO
	stop_gpios();
#endif
#endif
	script_enabled = false;
	if (my_script != NULL)
	    free_script();

	scriptcon = pespconn;
	downloadCon = (struct espconn *)os_zalloc(sizeof(struct espconn));

	/* Equivalent to bind */
	downloadCon->type = ESPCONN_TCP;
	downloadCon->state = ESPCONN_NONE;
	downloadCon->proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));
	downloadCon->proto.tcp->local_port = port;

	/* Register callback when clients connect to the server */
	espconn_regist_connectcb(downloadCon, script_connected_cb);

	/* Put the connection in accept mode */
	espconn_accept(downloadCon);

	os_sprintf(response, "Waiting for script upload on port %d\r\n", port);
	goto command_handled;
    }
#endif
    if (strcmp(tokens[0], "lock") == 0) {
	if (config.locked) {
	    os_sprintf(response, "Config already locked\r\n");
	    goto command_handled;
	}
	if (nTokens == 1) {
	    if (os_strlen(config.lock_password) == 0) {
		os_sprintf(response, "No password defined\r\n");
		goto command_handled;
	    }
	}
	else if (nTokens == 2) {
	    os_sprintf(config.lock_password, "%s", tokens[1]);
	}
	else {
	    os_sprintf(response, INVALID_NUMARGS);
	    goto command_handled;
	}
	config.locked = 1;
	config_save(&config);
	os_sprintf(response, "Config locked (pw: %s)\r\n", config.lock_password);
	goto command_handled;
    }

    if (strcmp(tokens[0], "unlock") == 0) {
	if (nTokens != 2) {
	    os_sprintf(response, INVALID_NUMARGS);
	} else if (os_strcmp(tokens[1], config.lock_password) == 0) {
	    config.locked = 0;
	    config_save(&config);
	    os_sprintf(response, "Config unlocked\r\n");
	} else {
	    os_sprintf(response, "Unlock failed. Invalid password\r\n");
	}
	goto command_handled;
    }

    if (strcmp(tokens[0], "publish") == 0)
    {
	if (nTokens != 4) {
            os_sprintf(response, INVALID_NUMARGS);
            goto command_handled;
	}
	if (strcmp(tokens[1], "local") == 0) {
	    MQTT_local_publish(tokens[2], tokens[3], os_strlen(tokens[3]), 0, 0);
	}
#ifdef MQTT_CLIENT
	else if (strcmp(tokens[1], "remote") == 0 && mqtt_connected) {
	    MQTT_Publish(&mqttClient, tokens[2], tokens[3], os_strlen(tokens[3]), 0, 0);
	}
#endif
	os_sprintf(response, "Published topic\r\n");
	goto command_handled;
    }

    if (strcmp(tokens[0], "set") == 0) {
	if (config.locked) {
	    os_sprintf(response, INVALID_LOCKED);
	    goto command_handled;
	}

	/*
	 * For set commands atleast 2 tokens "set" "parameter" "value" is needed
	 * hence the check
	 */
	if (nTokens < 3) {
	    os_sprintf(response, INVALID_NUMARGS);
	    goto command_handled;
	} else {
	    // atleast 3 tokens, proceed
	    if (strcmp(tokens[1], "ssid") == 0) {
		os_sprintf(config.ssid, "%s", tokens[2]);
		config.auto_connect = 1;
		os_sprintf(response, "SSID set (auto_connect = 1)\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "password") == 0) {
		os_sprintf(config.password, "%s", tokens[2]);
		os_sprintf(response, "Password set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "auto_connect") == 0) {
		config.auto_connect = atoi(tokens[2]);
		os_sprintf(response, "Auto Connect set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ap_ssid") == 0) {
		os_sprintf(config.ap_ssid, "%s", tokens[2]);
		os_sprintf(response, "AP SSID set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ap_password") == 0) {
		if (os_strlen(tokens[2]) < 8) {
		    os_sprintf(response, "Password to short (min. 8)\r\n");
		} else {
		    os_sprintf(config.ap_password, "%s", tokens[2]);
		    config.ap_open = 0;
		    os_sprintf(response, "AP Password set\r\n");
		}
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ap_open") == 0) {
		config.ap_open = atoi(tokens[2]);
		os_sprintf(response, "Open Auth set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ap_on") == 0) {
		if (atoi(tokens[2])) {
		    if (!config.ap_on) {
			wifi_set_opmode(STATIONAP_MODE);
			user_set_softap_wifi_config();
			do_ip_config = true;
			config.ap_on = true;
			os_sprintf(response, "AP on\r\n");
		    } else {
			os_sprintf(response, "AP already on\r\n");
		    }

		} else {
		    if (config.ap_on) {
			wifi_set_opmode(STATION_MODE);
#ifdef MDNS
			if (config.mdns_mode == 2) {
			    espconn_mdns_close();
			}
#endif
			config.ap_on = false;
			os_sprintf(response, "AP off\r\n");
		    } else {
			os_sprintf(response, "AP already off\r\n");
		    }
		}
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "speed") == 0) {
		uint16_t speed = atoi(tokens[2]);
		bool succ = system_update_cpu_freq(speed);
		if (succ)
		    config.clock_speed = speed;
		os_sprintf(response, "Clock speed update %s\r\n", succ ? "successful" : "failed");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "network") == 0) {
		config.network_addr.addr = ipaddr_addr(tokens[2]);
		ip4_addr4(&config.network_addr) = 0;
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "dns") == 0) {
		if (os_strcmp(tokens[2], "dhcp") == 0) {
		    config.dns_addr.addr = 0;
		    os_sprintf(response, "DNS from DHCP\r\n");
		} else {
		    config.dns_addr.addr = ipaddr_addr(tokens[2]);
		    os_sprintf(response, "DNS set to %d.%d.%d.%d\r\n", IP2STR(&config.dns_addr));
		    if (config.dns_addr.addr) {
			dns_ip.addr = config.dns_addr.addr;
		    }
		}
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ip") == 0) {
		if (os_strcmp(tokens[2], "dhcp") == 0) {
		    config.my_addr.addr = 0;
		    os_sprintf(response, "IP from DHCP\r\n");
		} else {
		    config.my_addr.addr = ipaddr_addr(tokens[2]);
		    os_sprintf(response, "IP address set to %d.%d.%d.%d\r\n", IP2STR(&config.my_addr));
		}
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "netmask") == 0) {
		config.my_netmask.addr = ipaddr_addr(tokens[2]);
		os_sprintf(response, "IP netmask set to %d.%d.%d.%d\r\n", IP2STR(&config.my_netmask));
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "gw") == 0) {
		config.my_gw.addr = ipaddr_addr(tokens[2]);
		os_sprintf(response, "Gateway set to %d.%d.%d.%d\r\n", IP2STR(&config.my_gw));
		goto command_handled;
	    }
#ifdef MDNS
	    if (strcmp(tokens[1], "mdns_mode") == 0) {
		config.mdns_mode = atoi(tokens[2]);
		os_sprintf(response, "mDNS mode set to %d\r\n", config.mdns_mode);
		goto command_handled;
	    }
#endif
#ifdef REMOTE_CONFIG
	    if (strcmp(tokens[1], "config_port") == 0) {
		config.config_port = atoi(tokens[2]);
		if (config.config_port == 0)
		    os_sprintf(response, "WARNING: if you save this, remote console access will be disabled!\r\n");
		else
		    os_sprintf(response, "Config port set to %d\r\n", config.config_port);
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "config_access") == 0) {
		config.config_access = atoi(tokens[2]) & (LOCAL_ACCESS | REMOTE_ACCESS);
		if (config.config_access == 0)
		    os_sprintf(response, "WARNING: if you save this, remote console access will be disabled!\r\n");
		else
		    os_sprintf(response, "Config access set\r\n");
		goto command_handled;
	    }
#endif
	    if (strcmp(tokens[1], "broker_subscriptions") == 0) {
		config.max_subscriptions = atoi(tokens[2]);
		os_sprintf(response, "Broker subscriptions set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "broker_retained_messages") == 0) {
		config.max_retained_messages = atoi(tokens[2]);
		os_sprintf(response, "Broker retained messages set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "broker_user") == 0) {
		os_strncpy(config.mqtt_broker_user, tokens[2], 32);
		config.mqtt_broker_user[31] = '\0';
		os_sprintf(response, "Broker username set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "broker_password") == 0) {
		if (os_strcmp(tokens[2], "none") == 0) {
		    config.mqtt_broker_password[0] = '\0';
		} else {
		    os_strncpy(config.mqtt_broker_password, tokens[2], 32);
		    config.mqtt_broker_password[31] = '\0';
		}
		os_sprintf(response, "Broker password set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "broker_access") == 0) {
		config.mqtt_broker_access = atoi(tokens[2]) & (LOCAL_ACCESS | REMOTE_ACCESS);
		os_sprintf(response, "Broker access set\r\n", config.config_port);
		goto command_handled;
	    }
#ifdef SCRIPTED
	    if (strcmp(tokens[1], "script_logging") == 0) {
		lang_logging = atoi(tokens[2]);
		os_sprintf(response, "Script logging set\r\n");
		goto command_handled;
	    }

	    if (tokens[1][0] == '@') {
		uint32_t slot_no = atoi(&tokens[1][1]);
		if (slot_no == 0 || slot_no > MAX_FLASH_SLOTS) {
		    os_sprintf(response, "Invalid flash slot number");
		} else {
		    slot_no--;
		    uint8_t slots[MAX_FLASH_SLOTS*FLASH_SLOT_LEN];
		    blob_load(1, (uint32_t *)slots, sizeof(slots));
		    os_strcpy(&slots[slot_no*FLASH_SLOT_LEN], tokens[2]);
		    blob_save(1, (uint32_t *)slots, sizeof(slots));
		    os_sprintf(response, "%s written to flash\r\n", tokens[1]);
		}
		goto command_handled;
	    }
#ifdef GPIO
#ifdef GPIO_PWM
	    if (strcmp(tokens[1], "pwm_period") == 0) {
		config.pwm_period = atoi(tokens[2]);
		os_sprintf(response, "PWM period set\r\n");
		goto command_handled;
	    }
#endif
#endif
#endif
#ifdef NTP
	    if (strcmp(tokens[1], "ntp_server") == 0) {
		os_strncpy(config.ntp_server, tokens[2], 32);
		config.ntp_server[31] = 0;
		ntp_set_server(config.ntp_server);
		os_sprintf(response, "NTP server set to %s\r\n", config.ntp_server);
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ntp_interval") == 0) {
		config.ntp_interval = atoi(tokens[2]) * 1000000;
		os_sprintf(response, "NTP interval set to %d s\r\n", atoi(tokens[2]));
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "ntp_timezone") == 0) {
		config.ntp_timezone = atoi(tokens[2]);
		set_timezone(config.ntp_timezone);
		os_sprintf(response, "NTP timezone set to %d h\r\n", config.ntp_timezone);
		goto command_handled;
	    }
#endif
#ifdef MQTT_CLIENT
	    if (strcmp(tokens[1], "mqtt_host") == 0) {
		os_strncpy(config.mqtt_host, tokens[2], 32);
		config.mqtt_host[31] = 0;
		os_sprintf(response, "MQTT host set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "mqtt_port") == 0) {
		config.mqtt_port = atoi(tokens[2]);
		os_sprintf(response, "MQTT port set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "mqtt_user") == 0) {
		os_strncpy(config.mqtt_user, tokens[2], 32);
		config.mqtt_user[31] = 0;
		os_sprintf(response, "MQTT user set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "mqtt_password") == 0) {
		os_strncpy(config.mqtt_password, tokens[2], 32);
		config.mqtt_password[31] = 0;
		os_sprintf(response, "MQTT password set\r\n");
		goto command_handled;
	    }

	    if (strcmp(tokens[1], "mqtt_id") == 0) {
		os_strncpy(config.mqtt_id, tokens[2], 32);
		config.mqtt_id[31] = 0;
		os_sprintf(response, "MQTT id set\r\n");
		goto command_handled;
	    }
#endif				/* MQTT_CLIENT */
	}

    }

    /* Control comes here only if the tokens[0] command is not handled */
    os_sprintf(response, "\r\nInvalid Command\r\n");

 command_handled:
    to_console(response);
 command_handled_2:
    system_os_post(user_procTaskPrio, SIG_CONSOLE_TX, (ETSParam) pespconn);
    return;
}

#ifdef SCRIPTED
void ICACHE_FLASH_ATTR do_command(char *t1, char *t2, char *t3) {
    ringbuf_memcpy_into(console_rx_buffer, t1, os_strlen(t1));
    ringbuf_memcpy_into(console_rx_buffer, " ", 1);
    ringbuf_memcpy_into(console_rx_buffer, t2, os_strlen(t2));
    ringbuf_memcpy_into(console_rx_buffer, " ", 1);
    ringbuf_memcpy_into(console_rx_buffer, t3, os_strlen(t3));

    uint8_t save_locked = config.locked;
    config.locked = false;
    console_handle_command(console_conn);
    config.locked = save_locked;

    system_os_post(user_procTaskPrio, SIG_CONSOLE_TX_RAW, (ETSParam) console_conn);
}
#endif

#ifdef REMOTE_CONFIG
static void ICACHE_FLASH_ATTR tcp_client_recv_cb(void *arg, char *data, unsigned short length) {
    struct espconn *pespconn = (struct espconn *)arg;
    int index;
    uint8_t ch;

    for (index = 0; index < length; index++) {
	ch = *(data + index);
	ringbuf_memcpy_into(console_rx_buffer, &ch, 1);

	// If a complete commandline is received, then signal the main
	// task that command is available for processing
	if (ch == '\n')
	    system_os_post(user_procTaskPrio, SIG_CONSOLE_RX, (ETSParam) arg);
    }

    *(data + length) = 0;
}

static void ICACHE_FLASH_ATTR tcp_client_sent_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;

    client_sent_pending = false;
    console_send_response(pespconn);
    
}

static void ICACHE_FLASH_ATTR tcp_client_discon_cb(void *arg) {
    os_printf("tcp_client_discon_cb(): client disconnected\n");
    struct espconn *pespconn = (struct espconn *)arg;
    console_conn = NULL;
}

/* Called when a client connects to the console server */
static void ICACHE_FLASH_ATTR tcp_client_connected_cb(void *arg) {
    char payload[128];
    struct espconn *pespconn = (struct espconn *)arg;

    os_printf("tcp_client_connected_cb(): Client connected\r\n");

    if (!check_connection_access(pespconn, config.config_access)) {
	os_printf("Client disconnected - no config access on this network\r\n");
	espconn_disconnect(pespconn);
	return;
    }

    espconn_regist_sentcb(pespconn, tcp_client_sent_cb);
    espconn_regist_disconcb(pespconn, tcp_client_discon_cb);
    espconn_regist_recvcb(pespconn, tcp_client_recv_cb);
    espconn_regist_time(pespconn, 300, 1);	// Specific to console only

    ringbuf_reset(console_rx_buffer);
    ringbuf_reset(console_tx_buffer);

    os_sprintf(payload, "CMD>");
    espconn_sent(pespconn, payload, os_strlen(payload));
    client_sent_pending = true;
    console_conn = pespconn;
}
#endif				/* REMOTE_CONFIG */

// Timer cb function
void ICACHE_FLASH_ATTR timer_func(void *arg) {
    uint64_t t_new;

    // Do we still have to configure the AP netif? 
    if (do_ip_config) {
	user_set_softap_ip_config();
#ifdef MDNS
	if (config.mdns_mode == 2) {
	    struct mdns_info *info = &mdnsinfo;
	    struct ip_info ipconfig;

	    wifi_get_ip_info(SOFTAP_IF, &ipconfig);

	    info->host_name = "mqtt";
	    info->ipAddr = ipconfig.ip.addr; //ESP8266 SoftAP IP
	    info->server_name = "mqtt";
	    info->server_port = 1883;
	    //info->txt_data[0] = "version = now";

	    espconn_mdns_init(info);
	}
#endif
	do_ip_config = false;
    }

    t_new = get_long_systime();
#ifdef NTP
    if (t_new - t_ntp_resync > config.ntp_interval) {
	ntp_get_time();
	t_ntp_resync = t_new;
    }

    if (ntp_sync_done()) {
	uint8_t *timestr = get_timestr();
	MQTT_local_publish("$SYS/broker/time", get_timestr(config.ntp_timezone), 8, 0, 0);
#ifdef SCRIPTED
	if (!timestamps_init) {
	    init_timestamps(timestr);
	    timestamps_init = true;
	}
	check_timestamps(timestr);
#endif
    }
#endif
    os_timer_arm(&ptimer, 1000, 0);
}

//Priority 0 Task
static void ICACHE_FLASH_ATTR user_procTask(os_event_t * events) {
    //os_printf("Sig: %d\r\n", events->sig);
    //os_printf("Pub_list: %d\r\n", pub_empty());

    switch (events->sig) {
    case SIG_START_SERVER:
	// Anything else to do here, when the broker has received its IP?
	break;
#ifdef SCRIPTED
    case SIG_TOPIC_RECEIVED:
	{
	    // We check this on any signal
	    // pub_process();
	}
	break;

    case SIG_SCRIPT_LOADED:
	{
	    espconn_disconnect(downloadCon);
	    espconn_delete(downloadCon);
	    os_free(downloadCon->proto.tcp);
	    os_free(downloadCon);
	    // continue to next case and check syntax...
	}
    case SIG_SCRIPT_HTTP_LOADED:
	{
	    if (read_script()) {
		interpreter_syntax_check();
		ringbuf_memcpy_into(console_tx_buffer, tmp_buffer, os_strlen(tmp_buffer));
		ringbuf_memcpy_into(console_tx_buffer, "\r\n", 2);
	    }
	    // continue to next case and print...
	}
#endif
    case SIG_CONSOLE_TX:
	{
	    ringbuf_memcpy_into(console_tx_buffer, "CMD>", 4);
	}
    case SIG_CONSOLE_TX_RAW:
	{
	    struct espconn *pespconn = (struct espconn *)events->par;
	    console_send_response(pespconn);

	    if (pespconn != 0 && remote_console_disconnect)
		espconn_disconnect(pespconn);
	    remote_console_disconnect = 0;
	}
	break;

    case SIG_CONSOLE_RX:
	{
	    struct espconn *pespconn = (struct espconn *)events->par;
	    console_handle_command(pespconn);
	}
	break;

    case SIG_DO_NOTHING:
    default:
	// Intentionally ignoring other signals
	os_printf("Spurious Signal received\r\n");
	break;
    }

    // Check queued messages on any signal
#ifdef SCRIPTED
    pub_process();
#endif
}

/* Callback called when the connection state of the module with an Access Point changes */
void wifi_handle_event_cb(System_Event_t * evt) {
    uint16_t i;
    uint8_t mac_str[20];

    //os_printf("wifi_handle_event_cb: ");
    switch (evt->event) {
    case EVENT_STAMODE_CONNECTED:
	os_printf("connect to ssid %s, channel %d\n",
		  evt->event_info.connected.ssid, evt->event_info.connected.channel);
	my_channel = evt->event_info.connected.channel;
	break;

    case EVENT_STAMODE_DISCONNECTED:
	os_printf("disconnect from ssid %s, reason %d\n",
		  evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
	connected = false;

#ifdef MQTT_CLIENT
	if (mqtt_enabled)
	    MQTT_Disconnect(&mqttClient);
#endif				/* MQTT_CLIENT */

#ifdef MDNS
	if (config.mdns_mode == 1) {
	    espconn_mdns_close();
	}
#endif
	break;

    case EVENT_STAMODE_AUTHMODE_CHANGE:
	os_printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
	break;

    case EVENT_STAMODE_GOT_IP:
	if (config.dns_addr.addr == 0) {
	    dns_ip.addr = dns_getserver(0);
	}

	os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR ",dns:" IPSTR "\n",
		  IP2STR(&evt->event_info.got_ip.ip),
		  IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw), IP2STR(&dns_ip));

	my_ip = evt->event_info.got_ip.ip;
	connected = true;

#ifdef SCRIPTED
	interpreter_wifi_connect();
#endif

#ifdef MQTT_CLIENT
	if (mqtt_enabled)
	    MQTT_Connect(&mqttClient);
#endif

#ifdef NTP
	if (os_strcmp(config.ntp_server, "none") != 0)
	    ntp_set_server(config.ntp_server);
	set_timezone(config.ntp_timezone);
#endif

#ifdef MDNS
	if (config.mdns_mode == 1) {
	    struct mdns_info *info = &mdnsinfo;

	    info->host_name = "mqtt";
	    info->ipAddr = evt->event_info.got_ip.ip.addr; //ESP8266 station IP
	    info->server_name = "mqtt";
	    info->server_port = 1883;
	    //info->txt_data[0] = "version = now";

	    espconn_mdns_init(info);
	}
#endif

	// Post a Server Start message as the IP has been acquired to Task with priority 0
	system_os_post(user_procTaskPrio, SIG_START_SERVER, 0);
	break;

    case EVENT_SOFTAPMODE_STACONNECTED:
	os_sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_connected.mac));
	os_printf("station: %s join, AID = %d\n", mac_str, evt->event_info.sta_connected.aid);
	break;

    case EVENT_SOFTAPMODE_STADISCONNECTED:
	os_sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_disconnected.mac));
	os_printf("station: %s leave, AID = %d\n", mac_str, evt->event_info.sta_disconnected.aid);
	break;

    default:
	break;
    }
}

void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void) {
    struct softap_config apConfig;

    wifi_softap_get_config(&apConfig);	// Get config first.

    os_memset(apConfig.ssid, 0, 32);
    os_sprintf(apConfig.ssid, "%s", config.ap_ssid);
    os_memset(apConfig.password, 0, 64);
    os_sprintf(apConfig.password, "%s", config.ap_password);
    if (!config.ap_open)
	apConfig.authmode = AUTH_WPA_WPA2_PSK;
    else
	apConfig.authmode = AUTH_OPEN;
    apConfig.ssid_len = 0;	// or its actual length

    apConfig.max_connection = MAX_CLIENTS;	// how many stations can connect to ESP8266 softAP at most.

    // Set ESP8266 softap config
    wifi_softap_set_config(&apConfig);
}

void ICACHE_FLASH_ATTR user_set_softap_ip_config(void) {
    struct ip_info info;
    struct dhcps_lease dhcp_lease;
    struct netif *nif;
    int i;

    // Configure the internal network

    wifi_softap_dhcps_stop();

    info.ip = config.network_addr;
    ip4_addr4(&info.ip) = 1;
    info.gw = info.ip;
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);

    wifi_set_ip_info(1, &info);

    dhcp_lease.start_ip = config.network_addr;
    ip4_addr4(&dhcp_lease.start_ip) = 2;
    dhcp_lease.end_ip = config.network_addr;
    ip4_addr4(&dhcp_lease.end_ip) = 128;
    wifi_softap_set_dhcps_lease(&dhcp_lease);

    wifi_softap_dhcps_start();
}

void ICACHE_FLASH_ATTR user_set_station_config(void) {
    struct station_config stationConf;
    char hostname[40];

    /* Setup AP credentials */
    stationConf.bssid_set = 0;
    os_sprintf(stationConf.ssid, "%s", config.ssid);
    os_sprintf(stationConf.password, "%s", config.password);
    wifi_station_set_config(&stationConf);

    os_sprintf(hostname, "NET_%s", config.ap_ssid);
    hostname[32] = '\0';
    wifi_station_set_hostname(hostname);

    wifi_set_event_handler_cb(wifi_handle_event_cb);

    wifi_station_set_auto_connect(config.auto_connect != 0);
}


bool ICACHE_FLASH_ATTR mqtt_broker_auth(const char* username, const char *password, struct espconn *pesp_conn) {
    //os_printf("connect from " IPSTR "\r\n", IP2STR((ip_addr_t *)&(pesp_conn->proto.tcp->remote_ip)));

    if (os_strcmp(config.mqtt_broker_user, "none") == 0)
	return true;

    if (os_strcmp(username, config.mqtt_broker_user) != 0 ||
	os_strcmp(password, config.mqtt_broker_password) != 0) {
	os_printf("Authentication with %s/%s failed\r\n", username, password);
	return false;
    }
    return true;
}


bool ICACHE_FLASH_ATTR mqtt_broker_connect(struct espconn *pesp_conn) {
    //os_printf("connect from " IPSTR "\r\n", IP2STR((ip_addr_t *)&(pesp_conn->proto.tcp->remote_ip)));

    if (!check_connection_access(pesp_conn, config.mqtt_broker_access)) {
	os_printf("Client disconnected - no mqtt access from the address " IPSTR "\r\n",
		  IP2STR((ip_addr_t *)&(pesp_conn->proto.tcp->remote_ip)));
	return false;
    }

    return true;
}


void  user_init() {
    struct ip_info info;

    connected = false;
    do_ip_config = false;
    my_ip.addr = 0;
    t_old = 0;

    console_rx_buffer = ringbuf_new(MAX_CON_CMD_SIZE);
    console_tx_buffer = ringbuf_new(MAX_CON_SEND_SIZE);
#ifdef GPIO
    gpio_init();
#endif
    init_long_systime();

    UART_init_console(BIT_RATE_115200, 0, console_rx_buffer, console_tx_buffer);

    os_printf("\r\n\r\nWiFi Router/MQTT Broker V2.0 starting\r\n");

    // Load config
    int config_res = config_load(&config);

#ifdef SCRIPTED
    script_enabled = false;
    if ((config_res == 0) && read_script()) {
	if (interpreter_syntax_check() != -1) {
	    bool lockstat = config.locked;
	    config.locked = false;

	    script_enabled = true;
	    interpreter_config();

	    config.locked = lockstat;
	} else {
	    os_printf("ERROR in script: %s\r\nScript disabled\r\n", tmp_buffer);
	}
    } else {
	// Clear script and vars
	blob_zero(0, MAX_SCRIPT_SIZE);
	blob_zero(1, MAX_FLASH_SLOTS * FLASH_SLOT_LEN);
    }
#endif

    // Configure the AP and start it, if required

    if (config.dns_addr.addr != 0)
	// We have a static DNS server
	dns_ip.addr = config.dns_addr.addr;

    if (config.ap_on) {
	wifi_set_opmode(STATIONAP_MODE);
	user_set_softap_wifi_config();
	do_ip_config = true;
    } else {
	wifi_set_opmode(STATION_MODE);
    }

    if (config.my_addr.addr != 0) {
	wifi_station_dhcpc_stop();
	info.ip.addr = config.my_addr.addr;
	info.gw.addr = config.my_gw.addr;
	info.netmask.addr = config.my_netmask.addr;
	wifi_set_ip_info(STATION_IF, &info);
	espconn_dns_setserver(0, &dns_ip);
    }

#ifdef MDNS
    wifi_set_broadcast_if(STATIONAP_MODE);
#endif

#ifdef REMOTE_CONFIG
    if (config.config_port != 0) {
	os_printf("Starting Console TCP Server on port %d\r\n", config.config_port);
	struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));

	/* Equivalent to bind */
	pCon->type = ESPCONN_TCP;
	pCon->state = ESPCONN_NONE;
	pCon->proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));
	pCon->proto.tcp->local_port = config.config_port;

	/* Register callback when clients connect to the server */
	espconn_regist_connectcb(pCon, tcp_client_connected_cb);

	/* Put the connection in accept mode */
	espconn_accept(pCon);
    }
#endif

#ifdef MQTT_CLIENT
    mqtt_connected = false;
    mqtt_enabled = (os_strcmp(config.mqtt_host, "none") != 0);
    if (mqtt_enabled) {
	MQTT_InitConnection(&mqttClient, config.mqtt_host, config.mqtt_port, 0);

	if (os_strcmp(config.mqtt_user, "none") == 0) {
	    MQTT_InitClient(&mqttClient, config.mqtt_id, 0, 0, 120, 1);
	} else {
	    MQTT_InitClient(&mqttClient, config.mqtt_id, config.mqtt_user, config.mqtt_password, 120, 1);
	}
//      MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);
    }
#endif				/* MQTT_CLIENT */

    remote_console_disconnect = 0;
    console_conn = NULL;

    // Now start the STA-Mode
    user_set_station_config();

    system_update_cpu_freq(config.clock_speed);

    // Start the broker only if it accessible
    if (config.mqtt_broker_access != 0) {
	espconn_tcp_set_max_con(15);
	os_printf("Max number of TCP clients: %d\r\n", espconn_tcp_get_max_con());

	MQTT_server_onData(MQTT_local_DataCallback);
	MQTT_server_onConnect(mqtt_broker_connect);
	MQTT_server_onAuth(mqtt_broker_auth);

	MQTT_server_start(1883 /*port */ , config.max_subscriptions,
			  config.max_retained_messages);
    }

    //Start task
    system_os_task(user_procTask, user_procTaskPrio, user_procTaskQueue, user_procTaskQueueLen);

#ifdef SCRIPTED
    timestamps_init = false;
    interpreter_init();
#endif

    // Start the timer
    os_timer_setfn(&ptimer, timer_func, 0);
    os_timer_arm(&ptimer, 500, 0);
}
