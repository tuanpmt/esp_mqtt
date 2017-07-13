# esp_uMQTT_broker
A basic MQTT Broker on the ESP8266

Thanks to Tuan PM for sharing his MQTT client library https://github.com/tuanpmt/esp_mqtt as a basis with us. The modified code still contains the complete client functionality from the original esp_mqtt lib, but it has been extended by the basic broker service.

The broker does support:
- MQTT protocoll versions v3.1 and v3.1.1 simultaniously
- a smaller number of clients (at least 8 have been tested, memory is the issue)
- retained messages
- LWT
- QoS level 0
- a subset of MQTT (CONNECT, DISCONNECT, SUBSCRIBE, UNSUBSCRIBE, PUBLISH, PING)

The broker does not yet support:
- username, password authentication
- QoS levels other than 0
- many TCP(=MQTT) clients
- non-clear sessions
- TLS

The complete functionality is included in the mqtt directory. The broker is started by simply including:

```c
#include "mqtt_server.h"

bool MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics);
```

in the user_init() function. 

# Building and Flashing
The code can be used in any project that is compiled using the NONOS_SDK or the esp-open-sdk. Also the sample code in the user directory can be build using the standard SDKs after adapting the variables in the Makefile.

If you don't need the full demo program, you can find a minimal demo in the directory "user_basic". Rename it to "user", adapt "user_config.h", and do the "make" to build a small demo that just starts an MQTT broker.

Build the esp_uMQTT_broker firmware with "make". "make flash" flashes it onto an esp8266.

If you want to use the precompiled binaries of the bigger demo (see below) from the firmware directory you can flash them on an ESP12 with 

```bash
esptool.py --port /dev/ttyUSB0 write_flash -fs 32m 0x00000 firmware/0x00000.bin 0x10000 firmware/0x10000.bin
```

# Usage
In the user directory there is a demo program that serves as a stand-alone MQTT broker and bridge. The program starts with the following default configuration:
- ssid: ssid, password: password
- ap_ssid: MyAP, ap_password: none, ap_on: 1, ap_open: 1
- network: 192.168.4.0/24

This means it connects to the internet via AP ssid,password and offers an open AP with ap_ssid MyAP. This default can be changed in the file user_config.h. The default can be overwritten and persistenly saved to flash by using a console interface. This console is available either via the serial port at 115200 baud or via tcp port 7777 (e.g. "telnet 192.168.4.1 7777" from a connected STA).

Use the following commands for an initial setup:
- set ssid your_home_router's_SSID
- set password your_home_router's_password
- set ap_ssid ESP's_ssid
- set ap_password ESP's_password
- show (to check the parameters)
- save
- reset

After reboot it will connect to your home router and itself is ready for stations to connect.

The console understands the following commands:

Basic commands (enough to get it working in nearly all environments):
- help: prints a short help message
- set [ssid|password] _value_: changes the settings for the uplink AP (WiFi config of your home-router)
- set [ap_ssid|ap_password] _value_: changes the settings for the soft-AP of the ESP (for your stations)
- show [config|stats]: prints the current config or some status information and statistics
- show mqtt_broker: shows the current status of the uMQTT broker 
- save [dhcp]: saves the current config parameters [+ the current DHCP leases] to flash
- reset [factory]: resets the esp, optionally resets WiFi params to default values
- lock: locks the current config, changes are not allowed
- unlock _password_: unlocks the config, requires password of the network AP
- quit: terminates a remote session

Advanced commands:
(Most of the set-commands are effective only after save and reset)
- set network _ip-addr_: sets the IP address of the internal network, network is always /24, router is always x.x.x.1
- set dns _dns-addr_: sets a static DNS address
- set dns dhcp: configures use of the dynamic DNS address from DHCP, default
- set ip _ip-addr_: sets a static IP address for the ESP in the uplink network
- set ip dhcp: configures dynamic IP address for the ESP in the uplink network, default
- set netmask _netmask_: sets a static netmask for the uplink network
- set gw _gw-addr_: sets a static gateway address in the uplink network
- scan: does a scan for APs
- set ap_on [0|1]: selects, whether the soft-AP is disabled (ap_on=0) or enabled (ap_on=1, default)
- set ap_open [0|1]: selects, whether the soft-AP uses WPA2 security (ap_open=0,  automatic, if an ap_password is set) or open (ap_open=1)
- set speed [80|160]: sets the CPU clock frequency (default 80 Mhz)
- set ntp_server _IP_or_hostname_: sets the name or IP of an NTP server ("none" disables NTP, default)
- set ntp_interval _interval_: sets the NTP sync interval in seconds (default 60)
- set ntp_timezone _tz_: sets the timezone in hours offset

While the user interface looks similar to my esp_wifi_repeater at https://github.com/martin-ger/esp_wifi_repeater this does NO NAT routing. AP and STA network are stricly separated and there is no routing in between. The only possible connection via both networks is the uMQTT broker that listens on both interfaces.

# MQTT client/bridging functionality
The broker comes with a "local" and a "remote" client, which means, the broker itself can publish and subscribe topics. The "local" client is a client to the own broker (without the need of an additional TCP connection). This feature is meant to provide the basis for a local rule engine that can react on MQTT events, e.g. to switch GPIOs or send other messages (MQTT, HTTP,...). You can use this at source level with the functions:

```c
bool MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain);
bool MQTT_local_subscribe(uint8_t* topic, uint8_t qos);
bool MQTT_local_unsubscribe(uint8_t* topic);
void MQTT_local_onData(MqttDataCallback dataCb);
```

By default the "remote" MQTT client is disabled. It can be enabled by setting the config parameter "mqtt_host" to a hostname different from "none". To configure the "remote" MQTT client you can set the following parameters:
- set mqtt_host _IP_or_hostname_: IP or hostname of the MQTT broker ("none" disables the MQTT client)
- set mqtt_user _username_: Username for authentication ("none" if no authentication is required at the broker)
- set mqtt_user _password_: Password for authentication
- set mqtt_id _clientId_: Id of the client at the broker (default: "ESPRouter_xxxxxx" derived from the MAC address)

You can test this with the commands:

- publish [local|remote] [topic] [data]: this publishes a topic
- subscribe [local|remote] [topic]: subscribes to a topic, received topic will be printed to serial output
- unsubscribe [local|remote] [topic]: unsubscribes from a topic

Currently the clients republish everything they receive (and they have subscribed) to the other client, thus it can act as something like an MQTT bridge. Up to now, the subscriptions are not persistently saved to config, so they have to be entered manually after each reboot - will work on this...

# NTP Support
Remote NTP time servers are supported. By default the NTP client is disabled - and the is no immediate need for synced time. Nowever, it can be enabled by setting the config parameter "ntp_server" to a hostname or IP different from "none" ("1.pool.ntp.org" is a good choice). Also you can set the "ntp_timezone" to an offset from GMT. The system time will be synced with the NTP server every "ntp_interval" seconds. Here it uses NOT the full NTP calculation and clock drift compensation. Instead it will just set the local time to the latest received time. 

After NTP sync has been completed successful once, the local time will be published every second under the topic "$SYS/broker/time" in the format "hh:mm:ss". You can also query the NTP time with the "time" command from the commandline.
