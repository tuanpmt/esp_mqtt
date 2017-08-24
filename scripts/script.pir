%
% Demo script for an ESP-01 module with a PIR (movement) sensor connected to GPIO 2.
% An indicator LED is connected to GPIO 0.
% The sensor connects to the Sonoff switch modul running the esp_uMQTT_broker.
%

% Config params, overwrite any previous settings from the commandline
% No AP, no broker
config ap_on 		0
config broker_access	0

config ssid MQTTbroker
config password stupidPassword

% Give us a time
config ntp_server	1.de.pool.ntp.org

% Connect to the broker on the Sonoff
config mqtt_host	192.168.178.32
config mqtt_user	Martin
config mqtt_password	secret
config speed		160

% Now the initialization, this is done once after booting
on init
do
	println "Starting the PIR script"

	% Device number ("* 1" to make even "" a number)
	setvar $device_number = @1 * 1

	% Read delay constanst in secs from flash @2
	setvar $delay = @2 * 1;
	if $delay = 0 then
		% Write default
		setvar $delay = 10;
		setvar @2 = 10;
	endif

	% Status of the PIR
	setvar $pir_status=0
	gpio_out 0 $pir_status

	% Command topic of the switch
	setvar $command_topic="/martinshome/switch/1/command"

	% Status topic
	setvar $status_topic="/martinshome/pir/" | $device_number | "/status"


% Now the events, checked whenever something happens

% The PIR
on gpio_interrupt 2 pullup
do
	println "New state GPIO 2: " | $this_gpio
	setvar $pir_status = $this_gpio
	gpio_out 0 $pir_status
	publish remote $status_topic $pir_status retained
	if $pir_status = 1 then
		publish remote $command_topic "on"
	endif
	settimer 1 $delay*1000


% Turn off again if nothing happens
on timer 1
do
	if $pir_status = 0 then
		publish remote $command_topic "off"
	endif

