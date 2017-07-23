# esp_uMQTT_broker
A basic MQTT Broker/Client with scripting support on the ESP8266

This program enables the ESP8266 to become the central node in a small distributed IoT system. It implements an MQTT Broker and a simple scripted rule engine with event/action statements that links together the MQTT sensors and actors. It can act as STA, as AP, or as both and it can connect to another MQTT broker (i.e. in the cloud). Here it can act as bridge and forward and rewrite topics in both directions.

# Usage
In the user directory there is the main program that serves as a stand-alone MQTT broker, client and bridge. The program starts with the following default configuration:

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
- set ap_on [0|1]: selects, whether the soft-AP is disabled (ap_on=0) or enabled (ap_on=1, default)
- set [ap_ssid|ap_password] _value_: changes the settings for the soft-AP of the ESP (for your stations)
- show [config|stats|script|mqtt]: prints the current config or some status information and statistics
- save: saves the current config parameters to flash
- reset [factory]: resets the esp, optionally resets WiFi params to default values
- lock [_password_]: saves and locks the current config, changes are not allowed. Password can be left open if already set before
- unlock _password_: unlocks the config, requires password from the lock command
- quit: terminates a remote session

Advanced commands (most of the set-commands are effective only after save and reset):
- set network _ip-addr_: sets the IP address of the internal network, network is always /24, router is always x.x.x.1
- set dns _dns-addr_: sets a static DNS address
- set dns dhcp: configures use of the dynamic DNS address from DHCP, default
- set ip _ip-addr_: sets a static IP address for the ESP in the uplink network
- set ip dhcp: configures dynamic IP address for the ESP in the uplink network, default
- set netmask _netmask_: sets a static netmask for the uplink network
- set gw _gw-addr_: sets a static gateway address in the uplink network
- scan: does a scan for APs
- set ap_open [0|1]: selects, whether the soft-AP uses WPA2 security (ap_open=0,  automatic, if an ap_password is set) or open (ap_open=1)
- set speed [80|160]: sets the CPU clock frequency (default 80 Mhz)
- set config_port _portno_: sets the port number of the console login (default is 7777, 0 disables remote console config)
- script [_portno_|delete]: opens port for upload of scripts or deletes the current script

While the user interface looks similar to my esp_wifi_repeater at https://github.com/martin-ger/esp_wifi_repeater this does NO NAT routing. AP and STA network are stricly separated and there is no routing in between. The only possible connection via both networks is the uMQTT broker that listens on both interfaces.

# MQTT client/bridging functionality
The broker comes with a "local" and a "remote" client, which means, the broker itself can publish and subscribe topics. The "local" client is a client to the own broker (without the need of an additional TCP connection).

By default the "remote" MQTT client is disabled. It can be enabled by setting the config parameter "mqtt_host" to a hostname different from "none". To configure the "remote" MQTT client you can set the following parameters:

- set mqtt_host _IP_or_hostname_: IP or hostname of the MQTT broker ("none" disables the MQTT client)
- set mqtt_user _username_: Username for authentication ("none" if no authentication is required at the broker)
- set mqtt_user _password_: Password for authentication
- set mqtt_id _clientId_: Id of the client at the broker (default: "ESPRouter_xxxxxx" derived from the MAC address)

# Scripting
The esp_uMQTT_broker comes with a build-in scripting engine. A script enables the ESP not just to act as a passive broker but to react on events (publications and timing events) and to send out its own items.

Here is a demo of a small script to give you an idea of the power of the scripting feature:
```
% Config params, overwrite any previous settings from the commandline
config ap_ssid 		MyAP
config ap_password	stupidPassword
config ntp_server	1.pool.ntp.org
config mqtt_host	192.168.1.20

% Now the initialization, this is done once after booting
on init
do
	println "MQTT Script 1.0 starting"
	subscribe local /test/#
	settimer 1 1000			% once per second
	setvar $1=0
	setvar $2=0
	setvar $3=10

% Now the events, checked whenever something happens

% Here a remote republish, of any local topic starting with "/test/"
on topic local /test/#
do 
	publish remote $this_topic $this_data

% When timer 1 expires, do some stuff
on timer 1
do
	% publish a timestamp locally
	publish local /t/time $timestamp

	% Let the LED on GPIO 2 blink
	gpio_out 2 $1
	setvar $1 = not $1

	% Count occurences in var $2
	setvar $2=$2+1

	% And if we have reached 10, print that to the console
	if $2 = $3 then
		println "We have reached "|$2| " at " |$timestamp
		setvar $3=$2+10
	endif

	% Reload the timer
	settimer 1 1000

% Here a local publication once each day at noon
on clock 12:00:00
do
	publish local /t/2 "High Noon"
```

In general, scripts have the following BNF:

```
<statement> ::= on <event> do <action> |
		config <param> <value> |
                <statement> <statement>

<event> ::= init | timer <num> | clock <timestamp> | topic (local|remote) <topic-id>

<action> ::= publish (local|remote) <topic-id> <val> [retained] |
             subscribe (local|remote) <topic-id> |
             unsubscribe (local|remote) <topic-id> |
             settimer <num> <num> |
             setvar $<num> = <expr> |
             gpio_out <num> <expr> |
             if <expr> then <action> endif |
	     print <expr> | println <expr>
             <action> <action>

<expr> ::= <val> <op> <expr> | not <expr>

<op> := '=' | '>' | gte | str_ge | str_gte | '+' | '-' | '/' | '*' | div

<val> := <string> | <const> | #<hex-string> | $<num> | $this_item | $this_data | $timestamp

<string> := "[any ASCII]*" | [any ASCII]*

<num> := [0-9]*

<timestamp> := hh:mm:ss
```

Scripts with size up to 4KB are uploaded to the esp_uMQTT_broker using a network interface. Start the upload with "script <portno>" on the concole of the ESP, e.g.:
```
CMD>script 2000
Waiting for script upload on port 2000
CMD>

```
Now the ESP listens on the given port for an incoming connection and stores anything it receives as new script. Upload a file using netcat, e.g.:
```bash
$ netcat 192.168.178.29 2000 < user/demo_script2
```
The ESP will store the file and immediatly checks the syntax of the script:
```
CMD>script 2000
Waiting for script upload on port 2000
Script upload completed (451 Bytes)
Syntax okay
CMD>
```

You can examine the currently loaded script using the "show script" command. It only displays about 1KB of a script. If you need to see more, use "show script <line_no>" with a higher starting line. Newly loaded scripts are stored persistently in flash and will be executed after next reset if they contain no syntax errors. "script delete" stops script execution and deleted a script from flash.

# NTP Support
NTP time is supported and timestamps are only available if the sync with an NTP server is done. By default the NTP client is enabled and set to "1.pool.ntp.org". It can be changed by setting the config parameter "ntp_server" to a hostname or an IP address. An ntp_server of "none" will disable the NTP client. Also you can set the "ntp_timezone" to an offset from GMT in hours. The system time will be synced with the NTP server every "ntp_interval" seconds (default ). Here it uses NOT the full NTP calculation and clock drift compensation. Instead it will just set the local time to the latest received time.

After NTP sync has been completed successfully once, the local time will be published every second under the topic "$SYS/broker/time" in the format "hh:mm:ss". You can also query the NTP time using the "time" command from the commandline and with the variable "$timestamp" from a script. If no NTP sync happened the time will be reported as "99:99:99".

- set ntp_server _IP_or_hostname_: sets the name or IP of an NTP server (default "1.pool.ntp.org", "none" disables NTP)
- set ntp_interval _interval_: sets the NTP sync interval in seconds (default 300)
- set ntp_timezone _tz_: sets the timezone in hours offset (default 0)
- time: prints the current time as hh:mm:ss

# Building and Flashing
The code can be used in any project that is compiled using the NONOS_SDK or the esp-open-sdk. Also the sample code in the user directory can be build using the standard SDKs after adapting the variables in the Makefile.

Build the esp_uMQTT_broker firmware with "make". "make flash" flashes it onto an esp8266.
 
If you want to use the precompiled binaries from the firmware directory you can flash them directly on an ESP8266, e.g. with

```bash
$ esptool.py --port /dev/ttyUSB0 write_flash -fs 32m 0x00000 firmware/0x00000.bin 0x10000 firmware/0x10000.bin

```
# The MQTT broker library
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

# Using the Source Code
The complete functionality is included in the mqtt directory and can be integrated into any NONOS SDK program. The broker is started by simply including:

```c
#include "mqtt_server.h"

bool MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics);

```
in the user_init() function. Now it is ready for MQTT connections on all activated interfaces (STA and/or AP). You can find a minimal demo in the directory "user_basic". Rename it to "user", adapt "user_config.h", and do the "make" to build a small demo that just starts an MQTT broker.

Your code can locally interact with the broker using the functions:

```c
bool MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain);
bool MQTT_local_subscribe(uint8_t* topic, uint8_t qos);
bool MQTT_local_unsubscribe(uint8_t* topic);
void MQTT_local_onData(MqttDataCallback dataCb);
```

With these functions you can publish and subscribe topics as a local client like you would with a remote MQTT broker.

