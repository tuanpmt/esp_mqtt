#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
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

/* System Task, for signals refer to user_config.h */
#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);

static os_timer_t ptimer;	

/* Some stats */
uint64_t Bytes_in, Bytes_out, Bytes_in_last, Bytes_out_last;
uint32_t Packets_in, Packets_out, Packets_in_last, Packets_out_last;
uint64_t t_old;

/* Hold the system wide configuration */
sysconfig_t config;

static ringbuf_t console_rx_buffer, console_tx_buffer;

static ip_addr_t my_ip;
static ip_addr_t dns_ip;
bool connected;
uint8_t my_channel;
bool do_ip_config;

uint8_t remote_console_disconnect;

void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void);
void ICACHE_FLASH_ATTR user_set_softap_ip_config(void);

int parse_str_into_tokens(char *str, char **tokens, int max_tokens)
{
char    *p, *q;
int     token_count = 0;
bool    in_token = false;

   // preprocessing
   for (p = q = str; *p != 0; p++) {
	if (*p == '\\') {
	   // next char is quoted, copy it skip, this one
	   if (*(p+1) != 0) *q++ = *++p;
	} else if (*p == 8) {
	   // backspace - delete previous char
	   if (q != str) q--;
	} else if (*p <= ' ') {
	   // mark this as whitespace
	   *q++ = 1;
	} else {
	   *q++ = *p;
	}
   }

   *q = 0;

   // cut into tokens
   for (p = str; *p != 0; p++) {
	if (*p == 1) {
	   if (in_token) {
		*p = 0;
		in_token = false;
	   }
	} else {
	   if (*p & 0x80) *p &= 0x7f;
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


void console_send_response(struct espconn *pespconn)
{
    char payload[MAX_CON_SEND_SIZE+4];
    uint16_t len = ringbuf_bytes_used(console_tx_buffer);

    ringbuf_memcpy_from(payload, console_tx_buffer, len);
    os_memcpy(&payload[len], "CMD>", 4);

    if (pespconn != NULL)
	espconn_sent(pespconn, payload, len+4);
    else
	UART_Send(0, &payload, len+4);
}


#ifdef ALLOW_SCANNING
struct espconn *scanconn;
void ICACHE_FLASH_ATTR scan_done(void *arg, STATUS status)
{
  char response[128];

  if (status == OK)
  {
    struct bss_info *bss_link = (struct bss_info *)arg;

    ringbuf_memcpy_into(console_tx_buffer, "\r", 1);
    while (bss_link != NULL)
    {
      os_sprintf(response, "%d,\"%s\",%d,\""MACSTR"\",%d\r\n",
                 bss_link->authmode, bss_link->ssid, bss_link->rssi,
                 MAC2STR(bss_link->bssid),bss_link->channel);
      ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
      bss_link = bss_link->next.stqe_next;
    }
  }
  else
  {
     os_sprintf(response, "scan fail !!!\r\n");
     ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
  }
  system_os_post(0, SIG_CONSOLE_TX, (ETSParam) scanconn);
}
#endif

bool ICACHE_FLASH_ATTR printf_topic(topic_entry *topic, void *user_data)
{
  uint8_t *response = (uint8_t *)user_data;

  os_sprintf(response, "%s: \"%s\" (QoS %d)\r\n", 
    topic->clientcon!=LOCAL_MQTT_CLIENT?topic->clientcon->connect_info.client_id:"LOCAL", topic->topic, topic->qos);
  ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
  return false;
}

bool ICACHE_FLASH_ATTR printf_retainedtopic(retained_entry *entry, void *user_data)
{
  uint8_t *response = (uint8_t *)user_data;

  os_sprintf(response, "\"%s\" len: %d (QoS %d)\r\n", entry->topic, entry->data_len, entry->qos);
  ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
  return false;
}

void MQTT_local_DataCallback(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t length)
{
  os_printf("Received: \"%s\" len: %d\r\n", topic, length); 
}

static char INVALID_LOCKED[] = "Invalid command. Config locked\r\n";
static char INVALID_NUMARGS[] = "Invalid number of arguments\r\n";
static char INVALID_ARG[] = "Invalid argument\r\n";

void ICACHE_FLASH_ATTR console_handle_command(struct espconn *pespconn)
{
#define MAX_CMD_TOKENS 9

    char cmd_line[MAX_CON_CMD_SIZE+1];
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

    if (strcmp(tokens[0], "help") == 0)
    {
        os_sprintf(response, "show [config|stats|mqtt|mqtt_broker]\r\n|set [ssid|password|auto_connect|ap_ssid|ap_password|network|dns|ip|netmask|gw|ap_on|ap_open|speed|config_port] <val>\r\n|quit|save [config]|reset [factory]|lock|unlock <password>|publish <topic> <data>|subscribe <topic>|unsubscribe <topic>");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#ifdef ALLOW_SCANNING
        os_sprintf(response, "|scan");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#endif
	ringbuf_memcpy_into(console_tx_buffer, "\r\n", 2);
        goto command_handled_2;
    }

    if (strcmp(tokens[0], "show") == 0)
    {
      int16_t i;
      ip_addr_t i_ip;

      if (nTokens == 1 || (nTokens == 2 && strcmp(tokens[1], "config") == 0)) {
        os_sprintf(response, "STA: SSID:%s PW:%s%s\r\n",
                   config.ssid,
                   config.locked?"***":(char*)config.password,
                   config.auto_connect?"":" [AutoConnect:0]");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

        os_sprintf(response, "AP:  SSID:%s PW:%s%s%s IP:%d.%d.%d.%d/24",
                   config.ap_ssid,
                   config.locked?"***":(char*)config.ap_password,
                   config.ap_open?" [open]":"",
                   config.ap_on?"":" [disabled]",
		   IP2STR(&config.network_addr));
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	// if static DNS, add it
	os_sprintf(response, config.dns_addr.addr?" DNS: %d.%d.%d.%d\r\n":"\r\n", IP2STR(&config.dns_addr));
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	// if static IP, add it
	os_sprintf(response, config.my_addr.addr?"Static IP: %d.%d.%d.%d Netmask: %d.%d.%d.%d Gateway: %d.%d.%d.%d\r\n":"", 
		IP2STR(&config.my_addr), IP2STR(&config.my_netmask), IP2STR(&config.my_gw));
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

        os_sprintf(response, "Clock speed: %d\r\n", config.clock_speed);
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	goto command_handled_2;
      }

      if (nTokens == 2 && strcmp(tokens[1], "stats") == 0) {
           uint32_t time = (uint32_t)(get_long_systime()/1000000);
	   int16_t i;

           os_sprintf(response, "System uptime: %d:%02d:%02d\r\n", 
	      time/3600, (time%3600)/60, time%60);
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

	   os_sprintf(response, "%d KiB in (%d packets)\r\n%d KiB out (%d packets)\r\n", 
			(uint32_t)(Bytes_in/1024), Packets_in, 
			(uint32_t)(Bytes_out/1024), Packets_out);
           ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   if (connected) {
		os_sprintf(response, "External IP-address: " IPSTR "\r\n", IP2STR(&my_ip));
	   } else {
		os_sprintf(response, "Not connected to AP\r\n");
	   }
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   if (config.ap_on)
		   os_sprintf(response, "%d Stations connected\r\n", wifi_softap_get_station_num());
	   else
		   os_sprintf(response, "AP disabled\r\n");
           ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   goto command_handled_2;
      }

      if (nTokens == 2 && (strcmp(tokens[1], "mqtt_broker")==0 || strcmp(tokens[1], "mqtt")==0)) {
	   MQTT_ClientCon *clientcon;

           os_sprintf(response, "Current clients:\r\n");
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   for (clientcon = clientcon_list; clientcon != NULL; clientcon = clientcon->next) {
	       os_sprintf(response, "%s%s", clientcon->connect_info.client_id, clientcon->next != NULL?", ":"");
	       ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   }
           os_sprintf(response, "\r\nCurrent subsriptions:\r\n");
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
           iterate_topics(printf_topic, response);
           os_sprintf(response, "Retained topics:\r\n");
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
           iterate_retainedtopics(printf_retainedtopic, response);

	   goto command_handled_2;
      }
    }

    if (strcmp(tokens[0], "save") == 0)
    {
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
    if (strcmp(tokens[0], "scan") == 0)
    {
        scanconn = pespconn;
        wifi_station_scan(NULL,scan_done);
        os_sprintf(response, "Scanning...\r\n");
        goto command_handled;
    }
#endif
    if (strcmp(tokens[0], "reset") == 0)
    {
	if (nTokens == 2 && strcmp(tokens[1], "factory") == 0) {
           config_load_default(&config);
           config_save(&config);
	}
        os_printf("Restarting ... \r\n");
	system_restart(); // if it works this will not return

	os_sprintf(response, "Reset failed\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "quit") == 0)
    {
	remote_console_disconnect = 1;
	os_sprintf(response, "Quitting console\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "publish") == 0)
    {
	if (nTokens != 3) {
            os_sprintf(response, INVALID_NUMARGS);
            goto command_handled;
	}

	MQTT_local_publish(tokens[1], tokens[2], os_strlen(tokens[2]), 0, 0);

	os_sprintf(response, "Published topic\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "subscribe") == 0)
    {
	if (nTokens != 2) {
            os_sprintf(response, INVALID_NUMARGS);
            goto command_handled;
	}

	MQTT_local_subscribe(tokens[1], 0);

	os_sprintf(response, "subscribed topic\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "unsubscribe") == 0)
    {
	if (nTokens != 2) {
            os_sprintf(response, INVALID_NUMARGS);
            goto command_handled;
	}

	uint8_t retval = MQTT_local_unsubscribe(tokens[1]);

	if (retval)
	  os_sprintf(response, "unsubscribed topic\r\n");
	else
	  os_sprintf(response, "unsubscribe failed\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "lock") == 0)
    {
	config.locked = 1;
	os_sprintf(response, "Config locked\r\n");
        goto command_handled;
    }

    if (strcmp(tokens[0], "unlock") == 0)
    {
        if (nTokens != 2) {
            os_sprintf(response, INVALID_NUMARGS);
        }
        else if (strcmp(tokens[1],config.password) == 0) {
	    config.locked = 0;
	    os_sprintf(response, "Config unlocked\r\n");
        } else {
	    os_sprintf(response, "Unlock failed. Invalid password\r\n");
        }
        goto command_handled;
    }

    if (strcmp(tokens[0], "set") == 0)
    {
        if (config.locked)
        {
            os_sprintf(response, INVALID_LOCKED);
            goto command_handled;
        }

        /*
         * For set commands atleast 2 tokens "set" "parameter" "value" is needed
         * hence the check
         */
        if (nTokens < 3)
        {
            os_sprintf(response, INVALID_NUMARGS);
            goto command_handled;
        }
        else
        {
            // atleast 3 tokens, proceed
            if (strcmp(tokens[1],"ssid") == 0)
            {
                os_sprintf(config.ssid, "%s", tokens[2]);
                os_sprintf(response, "SSID set\r\n");
                goto command_handled;
            }

            if (strcmp(tokens[1],"password") == 0)
            {
                os_sprintf(config.password, "%s", tokens[2]);
                os_sprintf(response, "Password set\r\n");
                goto command_handled;
            }

            if (strcmp(tokens[1],"auto_connect") == 0)
            {
                config.auto_connect = atoi(tokens[2]);
                os_sprintf(response, "Auto Connect set\r\n");
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_ssid") == 0)
            {
                os_sprintf(config.ap_ssid, "%s", tokens[2]);
                os_sprintf(response, "AP SSID set\r\n");
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_password") == 0)
            {
		if (os_strlen(tokens[2])<8) {
		    os_sprintf(response, "Password to short (min. 8)\r\n");
		} else {
                    os_sprintf(config.ap_password, "%s", tokens[2]);
		    config.ap_open = 0;
                    os_sprintf(response, "AP Password set\r\n");
		}
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_open") == 0)
            {
                config.ap_open = atoi(tokens[2]);
                os_sprintf(response, "Open Auth set\r\n");
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_on") == 0)
            {
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
				config.ap_on = false;
                		os_sprintf(response, "AP off\r\n");
			} else {
				os_sprintf(response, "AP already off\r\n");
			}
		}
                goto command_handled;
            }

	    if (strcmp(tokens[1], "speed") == 0)
	    {
		uint16_t speed = atoi(tokens[2]);
		bool succ = system_update_cpu_freq(speed);
		if (succ) 
		    config.clock_speed = speed;
		os_sprintf(response, "Clock speed update %s\r\n",
		  succ?"successful":"failed");
        	goto command_handled;
	    }

            if (strcmp(tokens[1],"network") == 0)
            {
                config.network_addr.addr = ipaddr_addr(tokens[2]);
		ip4_addr4(&config.network_addr) = 0;
                os_sprintf(response, "Network set to %d.%d.%d.%d/24\r\n", 
			IP2STR(&config.network_addr));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ip") == 0)
            {
		if (os_strcmp(tokens[2], "dhcp") == 0) {
		    config.my_addr.addr = 0;
		    os_sprintf(response, "IP from DHCP\r\n");
		} else {
		    config.my_addr.addr = ipaddr_addr(tokens[2]);
		    os_sprintf(response, "IP address set to %d.%d.%d.%d\r\n", 
			IP2STR(&config.my_addr));
		}
                goto command_handled;
            }

            if (strcmp(tokens[1],"netmask") == 0)
            {
                config.my_netmask.addr = ipaddr_addr(tokens[2]);
                os_sprintf(response, "IP netmask set to %d.%d.%d.%d\r\n", 
			IP2STR(&config.my_netmask));
                goto command_handled;
            }

            if (strcmp(tokens[1],"gw") == 0)
            {
                config.my_gw.addr = ipaddr_addr(tokens[2]);
                os_sprintf(response, "Gateway set to %d.%d.%d.%d\r\n", 
			IP2STR(&config.my_gw));
                goto command_handled;
            }

        }
    }

    /* Control comes here only if the tokens[0] command is not handled */
    os_sprintf(response, "\r\nInvalid Command\r\n");

command_handled:
    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
command_handled_2:
    system_os_post(0, SIG_CONSOLE_TX, (ETSParam) pespconn);
    return;
}

#ifdef REMOTE_CONFIG
static void ICACHE_FLASH_ATTR tcp_client_recv_cb(void *arg,
                                                 char *data,
                                                 unsigned short length)
{
    struct espconn *pespconn = (struct espconn *)arg;
    int            index;
    uint8_t         ch;

    for (index=0; index <length; index++)
    {
        ch = *(data+index);
	ringbuf_memcpy_into(console_rx_buffer, &ch, 1);

        // If a complete commandline is received, then signal the main
        // task that command is available for processing
        if (ch == '\n')
            system_os_post(0, SIG_CONSOLE_RX, (ETSParam) arg);
    }

    *(data+length) = 0;
}


static void ICACHE_FLASH_ATTR tcp_client_discon_cb(void *arg)
{
    os_printf("tcp_client_discon_cb(): client disconnected\n");
    struct espconn *pespconn = (struct espconn *)arg;
}


/* Called when a client connects to the console server */
static void ICACHE_FLASH_ATTR tcp_client_connected_cb(void *arg)
{
    char payload[128];
    struct espconn *pespconn = (struct espconn *)arg;

    os_printf("tcp_client_connected_cb(): Client connected\r\n");

    //espconn_regist_sentcb(pespconn,     tcp_client_sent_cb);
    espconn_regist_disconcb(pespconn,   tcp_client_discon_cb);
    espconn_regist_recvcb(pespconn,     tcp_client_recv_cb);
    espconn_regist_time(pespconn,  300, 1);  // Specific to console only

    ringbuf_reset(console_rx_buffer);
    ringbuf_reset(console_tx_buffer);
    
    os_sprintf(payload, "CMD>");
    espconn_sent(pespconn, payload, os_strlen(payload));
}
#endif /* REMOTE_CONFIG */


bool toggle;
// Timer cb function
void ICACHE_FLASH_ATTR timer_func(void *arg){
uint32_t Vcurr;
uint64_t t_new;
uint32_t t_diff;

    toggle = !toggle;

    // Do we still have to configure the AP netif? 
    if (do_ip_config) {
	user_set_softap_ip_config();
	do_ip_config = false;
    }

    t_new = get_long_systime();

    os_timer_arm(&ptimer, toggle?1000:100, 0); 
}

//Priority 0 Task
static void ICACHE_FLASH_ATTR user_procTask(os_event_t *events)
{
    //os_printf("Sig: %d\r\n", events->sig);

    switch(events->sig)
    {
    case SIG_START_SERVER:
	// Anything else to do here, when the repeater has received its IP?
	break;

    case SIG_CONSOLE_TX:
        {
            struct espconn *pespconn = (struct espconn *) events->par;
            console_send_response(pespconn);

	    if (pespconn != 0 && remote_console_disconnect) espconn_disconnect(pespconn);
	    remote_console_disconnect = 0;
        }
        break;

    case SIG_CONSOLE_RX:
        {
            struct espconn *pespconn = (struct espconn *) events->par;
            console_handle_command(pespconn);
        }
        break;

    case SIG_DO_NOTHING:
    default:
        // Intentionally ignoring other signals
        os_printf("Spurious Signal received\r\n");
        break;
    }
}

/* Callback called when the connection state of the module with an Access Point changes */
void wifi_handle_event_cb(System_Event_t *evt)
{
    uint16_t i;
    uint8_t mac_str[20];

    //os_printf("wifi_handle_event_cb: ");
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        os_printf("connect to ssid %s, channel %d\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
	my_channel = evt->event_info.connected.channel;
        break;

    case EVENT_STAMODE_DISCONNECTED:
        os_printf("disconnect from ssid %s, reason %d\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
	connected = false;
        break;

    case EVENT_STAMODE_AUTHMODE_CHANGE:
        os_printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
        break;

    case EVENT_STAMODE_GOT_IP:
        os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR ",dns:" IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw), IP2STR(&dns_ip));

	my_ip = evt->event_info.got_ip.ip;
	connected = true;

        // Post a Server Start message as the IP has been acquired to Task with priority 0
	system_os_post(user_procTaskPrio, SIG_START_SERVER, 0 );
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


void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void)
{
struct softap_config apConfig;

   wifi_softap_get_config(&apConfig); // Get config first.
    
   os_memset(apConfig.ssid, 0, 32);
   os_sprintf(apConfig.ssid, "%s", config.ap_ssid);
   os_memset(apConfig.password, 0, 64);
   os_sprintf(apConfig.password, "%s", config.ap_password);
   if (!config.ap_open)
      apConfig.authmode = AUTH_WPA_WPA2_PSK;
   else
      apConfig.authmode = AUTH_OPEN;
   apConfig.ssid_len = 0;// or its actual length

   apConfig.max_connection = MAX_CLIENTS; // how many stations can connect to ESP8266 softAP at most.

   // Set ESP8266 softap config
   wifi_softap_set_config(&apConfig);
}


void ICACHE_FLASH_ATTR user_set_softap_ip_config(void)
{
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


void ICACHE_FLASH_ATTR user_set_station_config(void)
{
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


void ICACHE_FLASH_ATTR user_init()
{
struct ip_info info;

    connected = false;
    do_ip_config = false;
    my_ip.addr = 0;
    Bytes_in = Bytes_out = Bytes_in_last = Bytes_out_last = 0,
    Packets_in = Packets_out = Packets_in_last = Packets_out_last = 0;
    t_old = 0;

    console_rx_buffer = ringbuf_new(MAX_CON_CMD_SIZE);
    console_tx_buffer = ringbuf_new(MAX_CON_SEND_SIZE);

    gpio_init();
    init_long_systime();

    UART_init_console(BIT_RATE_115200, 0, console_rx_buffer, console_tx_buffer);

    os_printf("\r\n\r\nWiFi Router/MQTT Broker V1.5 starting\r\n");

    // Load config
    config_load(&config);

    // Configure the AP and start it, if required
    if (config.dns_addr.addr == 0)
	// Google's DNS as default, as long as we havn't got one from DHCP
	IP4_ADDR(&dns_ip, 8, 8, 8, 8);
    else
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

#ifdef REMOTE_CONFIG
    if (config.config_port != 0) {
      os_printf("Starting Console TCP Server on %d port\r\n", CONSOLE_SERVER_PORT);
      struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));

      /* Equivalent to bind */
      pCon->type  = ESPCONN_TCP;
      pCon->state = ESPCONN_NONE;
      pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
      pCon->proto.tcp->local_port = config.config_port;

      /* Register callback when clients connect to the server */
      espconn_regist_connectcb(pCon, tcp_client_connected_cb);

      /* Put the connection in accept mode */
      espconn_accept(pCon);
    }
#endif

    remote_console_disconnect = 0;

    // Now start the STA-Mode
    user_set_station_config();

    system_update_cpu_freq(config.clock_speed);

    espconn_tcp_set_max_con(10);
    os_printf("Max number of TCP clients: %d\r\n", espconn_tcp_get_max_con());

    MQTT_server_start(1883 /*port*/, 30 /*max_subscriptions*/, 30 /*max_retained_items*/);

    //MQTT_local_subscribe("/test/#", 0);
    MQTT_local_onData(MQTT_local_DataCallback);

    // Start the timer
    os_timer_setfn(&ptimer, timer_func, 0);
    os_timer_arm(&ptimer, 500, 0); 

    //Start task
    system_os_task(user_procTask, user_procTaskPrio, user_procTaskQueue, user_procTaskQueueLen);
}

