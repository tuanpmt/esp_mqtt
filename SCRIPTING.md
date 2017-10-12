Scripting Language
==================

The scripting language of the esp_uMQTT_broker is stricly event based. It mainly consists of "on _event_ do _action_" clauses. An event can be:
- the reception of an MQTT item,
- the sucessful connection to an external MQTT broker,
- the sucessful connect to the WiFi network
- an expiring timer, 
- a predefined time-of-day,
- a GPIO interrupt, 
- an HTTP response, and
- the initialization of the system.

An action can be a sequence of:
- subscriptions on MQTT topics,
- publication of MQTT topics,
- timer settings,
- I/O on the GPIOs,
- manipulation of variables, and
- output on the command line.

The scripting language assumes that there *can* be two MQTT brokers: the _local_ one on the ESP8266 and/or a _remote_ one another node. The connection to the local broker is always present, the connection to the remote broker must be established via the configuration of the esp_uMQTT_broker. Neither one must be used, scripts can also work without MQTT, but the real intention of the language is to provide an easy way to react on received topics and, if required, to forward or rewrite topics from one broker to the other.

# Syntax
In general, scripts conform to the following BNF:

```
<statement> ::= on <event> do <action> |
		config <param> ([any ASCII]* | @<num>) |
                <statement> <statement>

<event> ::= init |
            wificonnect |
	    mqttconnect |
            timer <num> |
            clock <timestamp> |
            gpio_interrupt <num> (pullup|nopullup) |
            topic (local|remote) <topic-id> |
            http_response

<action> ::= publish (local|remote) <topic-id> <expr> [retained] |
             subscribe (local|remote) <topic-id> |
             unsubscribe (local|remote) <topic-id> |
             settimer <num> <expr> |
             setvar ($[any ASCII]* | @<num>) = <expr> |
             http_get <expr> |
             gpio_pinmode <num> (input|output) [pullup] |
             gpio_out <num> <expr> |
             gpio_pwm <num> <num> |
             if <expr> then <action> [else <action>] endif |
	     print <expr> | println <expr> |
	     system <expr> |
             <action> <action>

<expr> ::= <val> | <val> <op> <expr> | (<expr>) | not (<expr>) | json_parse (<expr>,<expr>)

<op> := '=' | '>' | gte | str_ge | str_gte | '+' | '-' | '*' | '|' | div

<val> := <string> | <const> | #<hex-string> | $[any ASCII]* | @<num> |
         gpio_in(<num>) | $adc | $this_item | $this_data | $this_gpio | 
         $this_http_code | $this_http_body | $timestamp | $weekday

<string> := "[any ASCII]*" | [any ASCII]*

<num> := [0-9]*

<timestamp> := hh:mm:ss
```

# Statements
```
on <event> do <action>
```
Whenever the _event_ occurs, the _action_ is executed. This is the basic form for all activities defined in a script. 

```
config <param> ([any ASCII]* | @<num>)
```
These aditional statements are executed once right after startup. Typically, they should be located at the beginning of a script, but this is not required. The _param_ is any configuration parameter that can be set via the CLI of the esp_uMQTT_broker, e.g. the _ssid_ or the _password_ of the uplink WiFi AP. Basically, "config _x_ _y_" means the same as "set _x_ _y_" on the CLI. But all parameters defined  in _config_ statements override the manually configured values in the CLI. Thus a script can contain its own network, MQTT, and security configuration. Instead of constant values also flash variables are allowed (see below). This can be used to configure node specific values, like an id or an address.

# Events
```
init
```
This event happens once after restart of the script. All "config" parameters are applied, but typically WiFi is not yet up and no external nodes are connected. This is typically the clause where the initalization of variables and timers as well as subscriptions to topics on the local broker take place.

```
wificonnect
```
This event happens each time, the esp_uMQTT_broker (re-)connects as client to the WiFi and has received an IP address.

```
mqttconnect
```
This event happens each time, the MQTT *client* module (re-)connects to an external broker. This is typically the clause where subscriptions to topics on this broker are done (must be re-done after a connection loss anyway).

```
topic (local|remote) <topic-id>
```
This event happens when a matching topic has been received from one broker, either from the local or the remote one. The _topic-id_ may contain the usual MQTT wildcards '+' or '#'. The actual topic-id of the message can be accessed in the actions via the special variable _$this_topic_, the content of the message via the special variable _$this_data_. These variables are only defined inside the "on topic" clause. If the script needs these values elsewhere, they have to be saved in other variables.

```
timer <num>
```
This event happens when the timer with the given number expires. Timers are set in millisecond units, but their expiration might be delayed by other interrupts and events, e.g. network traffic. Thus their accuracy is limited. Timers are not reloading automatically. I.e. if you need a permanently running timer, reload the expired timer in the "on timer" clause.

```
clock <timestamp>
```
This event happens when the time-of-day value given in the event has been reached. It happens once per day. Timestamps are given as "hh:mm:ss" and are only available if NTP is enabled. Make sure to set the correct "ntp_timezone" to adjust UTC to your location.

```
gpio_interrupt <num> (pullup|nopullup)
```
This event happens when the GPIO pin with the given number generates an interrupt. An interrupt happens on each state change, i.e. a 0-1-0 sequence will cause two events. Use the special variable _$this_gpio_ to access the actual state of the pin. This variable is only defined inside the "on topic" clause. The interrupt mechanism uses a 50ms delay for debouncing the input. This means this event is suitable for switches, not for high-frequency signals. The "pullup" or "nopullup" defines whether the input pin is free floating or internally pulled to high level.

```
http_response
```
This event happens when an HTTP-request has been sent with "http_get" and a response arrives. The actual body of the response can be accessed in the actions via the special variable _$this_http_body_, the HTTP return code via the special variable _$this_http_code_. These variables are only defined inside the "on http_response" clause.

# Action
```
publish (local|remote) <topic-id> <expr> [retained]
```
Publishs an MQTT topic to either the local or the remote broker. The "topic-id" must be a valid MQTT topic without wildcards, the "expr" can be any value. A string will be published without null-termination. If the optional "retained" is given, the topic will be published with the retained-flag, i.e. the broker will permanently store this topic/value until it is overwritten by a new value.

```
subscribe (local|remote) <topic-id> |
unsubscribe (local|remote) <topic-id>
```
Subscribes or unsubscribes a topic either at the local or at the remote broker. The _topic-id_ may contain the usual MQTT wildcards '+' or '#'. Without prior subscription no "on topic" events can happen.

```
settimer <num> <expr>
```
(Re-)initializes the timer "num" with a value given in milliseconds. Timers are not reloading automatically. I.e. if you need a permanently running timer, reload the expired timer in the "on timer" clause.

```
setvar ($[any ASCII]* | @<num>) = <expr>
```
Sets a variable to a given value. All variable names start with a '$'. Variables are not typed and a handled like strings. Whenever a numerical value is need, the contents of a variable is interpreted as an integer number. If a boolean value is required, it tested, whether the string evaluates to zero (= false) or any other value (= true).

Currently the interpreter is configured for a maximum of 10 variables, with a significant id length of 15. In addition, there are currently 10 flash variables (up to 63 chars long) that do preserve their state even after reset or power down. These variables are named @1 to @10. Writing these variables is very slow as this includes a flash sector clear and rewrite cycle.  Thus, these variables should be written only when relevant state should be saved. Reading these vars is faster.

Flash variables can also be used for storing config parameters or handing them over from the CLI to a script. They can be set with the "set @[num] _value_" on the CLI and the written values can then be picked up by a script to read e.g. config parameters like DNS names, IPs, node IDs or username/password.

```
http_get <expr>
```
Sends an HTTP request to the URL given in the expression.

```
gpio_pinmode <num> (input|output) [pullup]
```
Defines the status of a GPIO pin. This is only required for input pins, that are not used in "gpio_interrupt" events. The status of these pins can be accessed via the "gpio_in()" expression. It is optional for output and PWM pins as these are configured automatically as soon as an output command is given. The optional "pullup" defines whether the input pin is free floating or internally pulled to high level.

```
gpio_out <num> <expr>
```
Sets GPIO pin num to the given boolean value.

```
gpio_pwm <num> <num>
```
Defines the GPIO pin num as PWM output and sets the PWM duty cycle to the given value. The value should be in the range from 0-1000 (0 = off, 1000 = full duty). By default the PWM frequency is 1000Hz. It can be changed with the _pwm_period_ config parameter.

```
system <expr>
```
Executes the given expression as if it has been issued on the CLI. Useful e.g. for "save", "lock" or "reset" commands.

```
print <expr> | 
println <expr>
```
Prints the given expression to serial or a connected remote console (either with or without trailing line break).

```
if <expr> then <action> [else <action>] endif
```
Classic "if then else" expression. Sequences of actions must be terminated with the (optional) "else" and the "endif". Can be nested.

# Expressions
Expressions evaluate to a (string) value. A single constant, a string, or a variable are the basic expressions. Expressions can be combined by operators. If more than one operator is used in an expression, all expressions are stricly evaluated from left to right. CAUTION: arithmetical preceedence does not (yet) apply automatically like in other programming languages. However, the preceedence can be fully controlled by brackets. 

```
not(<expr>)
```
Interpretes the argument expression as boolean and inverts the result.

```
json_parse (<expr>,<expr>)
```
Parses a JSON structure. The first argument expression is interpreted as JSON path, i.g. a string with names or numbers separated by "." characters. The second argument expression is interpreted as a JSON structure and the result of the expression is the data field of the JSON structure that is identified by the path (or an empty string if not found).

Example - give in the variable $json the following JSON structure:
```
{
"name":
	{ "first":"John",
          "last":"Snow" }
"age":30,
"cars":[ "Ford", "BMW", "Fiat" ]
}
```
"json_parse("name.first", $json)" results in "John", "json_parse("cars.1", $json)" results in "BMW".

# Values
A constant, a string, or a variable are values. Optionally, strings and constans can be put in quotes, like e.g. "A String" or "-10". This is especially useful for strings containing a whitespace or an operator. Any single character can be quotet using the '\\' escape character, like e.g. A\ String (equals "A String").

Other value terms are:

```
#<hex-string>
```
A value given as hex value in multiples of two hex-digit value, e.g. "#fffeff1a". With this notation binary and even NULL characters can be defined e.g. for MQTT publication data.

Some (additional) vars contain special status: $this_topic and $this_data are only defined in 'on topic' clauses and contain the current topic and its data. $this_gpio contains the state of the GPIO in an 'on gpio_interrupt' clause and $timestamp contains the current time of day in 'hh:mm:ss' format. If no NTP sync happened the time will be reported as "99:99:99". The variable "$weekday" returns the day of week as three letters ("Mon","Tue",...).

```
gpio_in(<num>)
```
Reads the current boolean input value of the given GPIO pin. This pin has to be defined as input before using the "gpio_pinmode" action.

```
$adc | $this_item | $this_data | $this_gpio | $timestamp | $weekday | $this_http_body | $this_http_code
```
Special variables:
- $adc gives you the current value of the ADC (analog to digital input pin)
- $this_topic and $this_data are only defined in "on topic" clauses and contain the current topic and its data.
- $this_gpio contains the state of the GPIO in an "on gpio_interrupt" clause.
- $timestamp contains the current time of day in "hh:mm:ss" format. If no NTP sync happened the time will be reported as "99:99:99". $weekday returns the day of week as three letters ("Mon","Tue",...). 
- $this_http_body and $this_http_code are only defined inside the "on http_response" clause and contain the body of an HTTP response and the HTTP return code.

# Operators
Operators are used to combine values and expressions.

```
'=' | '>' | gte | str_ge | str_gte
```
These operators result in boolean values and are typically used in comparisons in "if" clauses. "gte" means "greater or equal". The smaller operators are not required, as they can be replaced by the greater operators and swapping the values. "str_ge" and "str_gte" also do a greater comparison, but as the former operators compare numerical values, these do a lexicographical comparison, i.e. "11" is lexicographically greater than "012".

```
'+' | '-' | '*' | div
```
These operators are the arithmetical operations. CAUTION: arithmetical preceedence does not (yet) apply automatically, all expressions are evaluated from left to right. I.e. "2+3\*4" evaluates to 20 instead of 14. However, the preceedence can be fully controlled by brackets. Write "2+(3\*4)" instead.

```
'|'
```
This operator concatenates the left and the right operator as strings. Useful e.g. in "print" actions or when putting together MQTT topics.

# Comments
Comments start with a ’%' anywhere in a line and reach until the end of this line.

# Sample
Here is a demo of a script to give you an idea of the power of the scripting feature. This script controls a Sonoff switch module. It connects to a remote MQTT broker and in parallel offers locally its own. The device has a number stored in the variable $device_number. On both brokers it subscribes to a topic named '/martinshome/switch/($device_number)/command', where it receives commands, and it publishes the topic '/martinshome/switch/($device_number)/status' with the current state of the switch relay. It understands the commands 'on','off', 'toggle', and 'blink'. Blinking is realized via a timer event. Local status is stored in the two variables $relay_status and $blink (blinking on/off). The 'on gpio_interrupt' clause reacts on pressing the pushbutton of the Sonoff and simply toggles the switch (and stops blinking). The last two 'on clock' clauses implement a daily on and off period:

```
% Config params, overwrite any previous settings from the commandline
config ap_ssid 		MyAP
config ap_password	stupidPassword
config ntp_server	1.de.pool.ntp.org
config broker_user	Martin
config broker_password	secret
config mqtt_host	martinshome.fritz.box
config speed		160

% Now the initialization, this is done once after booting
on init
do
	% Device number
	setvar $device_number = 1

	% @<num> vars are stored in flash and are persistent even after reboot 
	setvar $run = @1 + 1
	setvar @1 = $run
	println "This is boot no "|$run

	% Status of the relay
	setvar $relay_status=0
	gpio_out 12 $relay_status
	gpio_out 13 not ($relay_status)

	% Blink flag
	setvar $blink=0

	% Command topic
	setvar $command_topic="/martinshome/switch/" | $device_number | "/command"

	% Status topic
	setvar $status_topic="/martinshome/switch/" | $device_number | "/status"

	publish local $status_topic $relay_status retained

	% local subscriptions once in 'init'
	subscribe local $command_topic

% Now the MQTT client init, this is done each time the client connects
on mqttconnect
do
	% remote subscriptions for each connection in 'mqttconnect'
	subscribe remote $command_topic

	publish remote $status_topic $relay_status retained

% Now the events, checked whenever something happens

% Is there a remote command?
on topic remote $command_topic
do
	println "Received remote command: " | $this_data

	% republish this locally - this does the action
	publish local $command_topic $this_data


% Is there a local command?
on topic local $command_topic
do
	println "Received local command: " | $this_data

	if $this_data = "on" then
		setvar $relay_status = 1
		setvar $blink = 0
		gpio_out 12 $relay_status
		gpio_out 13 not ($relay_status)
	else
	    if $this_data = "off" then
		setvar $relay_status = 0
		setvar $blink = 0
		gpio_out 12 $relay_status
		gpio_out 13 not ($relay_status)
	    endif
	endif
	if $this_data = "toggle" then
		setvar $relay_status = not ($relay_status)
		gpio_out 12 $relay_status
		gpio_out 13 not ($relay_status)
	endif
	if $this_data = "blink" then
		setvar $blink = 1
		settimer 1 500
	endif

	publish local $status_topic $relay_status retained
	publish remote $status_topic $relay_status retained


% The local pushbutton
on gpio_interrupt 0 pullup
do
	println "New state GPIO 0: " | $this_gpio
	if $this_gpio = 0 then
		setvar $blink = 0
		publish local $command_topic "toggle"
	endif


% Blinking
on timer 1
do
	if $blink = 1 then
		publish local $command_topic "toggle"
		settimer 1 500
	endif


% Switch on in the evening
on clock 19:30:00
do
	publish local $command_topic "on"

% Switch off at night
on clock 01:00:00
do
	publish local $command_topic "off"
```


