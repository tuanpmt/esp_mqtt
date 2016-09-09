**esp_mqtt**
==========
[![](https://travis-ci.org/tuanpmt/esp_mqtt.svg?branch=master)](https://travis-ci.org/tuanpmt/esp_mqtt)

This is MQTT client library for ESP8266, port from: [MQTT client library for Contiki](https://github.com/esar/contiki-mqtt) (thanks)



**Features:**

 * Support subscribing, publishing, authentication, will messages, keep alive pings and all 3 QoS levels (it should be a fully functional client).
 * Support multiple connection (to multiple hosts).
 * Support SSL connection
 * Easy to setup and use

***Prerequire:***

- ESPTOOL.PY: https://github.com/themadinventor/esptool
- SDK 2.0 or higher: http://bbs.espressif.com/viewtopic.php?f=46&t=2451
- ESP8266 compiler: 
    + OSX or Linux: http://tuanpm.net/esp8266-development-kit-on-mac-os-yosemite-and-eclipse-ide/
    + Windows: http://programs74.ru/udkew-en.html 

**Compile:**

- Copy file `include/user_config.sample.h` to `include/user_config.local.h` and change settings, included: SSID, PASS, MQTT configurations ...


Make sure to add PYTHON PATH and compile PATH to Eclipse environment variable if using Eclipse


```bash
git clone --recursive https://github.com/tuanpmt/esp_mqtt
cd esp_mqtt
#clean
make clean
#make
make SDK_BASE=/tools/esp8266/sdk/ESP8266_NONOS_SDK ESPTOOL=tools/esp8266/esptool/esptool.py all
#flash
make ESPPORT=/dev/ttyUSB0 flash
```

**Usage**

See file: `user/user_main.c`


**Publish message and Subscribe**

```c
/* TRUE if success */
BOOL MQTT_Subscribe(MQTT_Client *client, char* topic, uint8_t qos);

BOOL MQTT_Publish(MQTT_Client *client, const char* topic, const char* data, int data_length, int qos, int retain);

```

**Already support LWT: (Last Will and Testament)**

```c

/* Broker will publish a message with qos = 0, retain = 0, data = "offline" to topic "/lwt" if client don't send keepalive packet */
MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);

```

#Default configuration

See: **include/user_config.sample.h**

**Define protocol name in include/user_config.local.h**

```c
#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/
```


**Create SSL Self sign**

```
openssl req -x509 -newkey rsa:1024 -keyout key.pem -out cert.pem -days XXX
```

**SSL Mqtt broker for test**

```javascript
var mosca = require('mosca')
var SECURE_KEY = __dirname + '/key.pem';
var SECURE_CERT = __dirname + '/cert.pem';
var ascoltatore = {
  //using ascoltatore
  type: 'mongo',
  url: 'mongodb://localhost:27017/mqtt',
  pubsubCollection: 'ascoltatori',
  mongo: {}
};

var moscaSettings = {
  port: 1880,
  stats: false,
  backend: ascoltatore,
  persistence: {
    factory: mosca.persistence.Mongo,
    url: 'mongodb://localhost:27017/mqtt'
  },
  secure : {
    keyPath: SECURE_KEY,
    certPath: SECURE_CERT,
    port: 1883
  }
};

var server = new mosca.Server(moscaSettings);
server.on('ready', setup);

server.on('clientConnected', function(client) {
    console.log('client connected', client.id);
});

// fired when a message is received
server.on('published', function(packet, client) {
  console.log('Published', packet.payload);
});

// fired when the mqtt server is ready
function setup() {
  console.log('Mosca server is up and running')
}
```

**Example projects using esp_mqtt:**

- [https://github.com/eadf/esp_mqtt_lcd](https://github.com/eadf/esp_mqtt_lcd)

[MQTT Broker for test](https://github.com/mcollina/mosca)

[MQTT Client for test](https://chrome.google.com/webstore/detail/mqttlens/hemojaaeigabkbcookmlgmdigohjobjm?hl=en)

**Contributing:**

Feel free to contribute to the project in any way you like!


**Authors:**
[Tuan PM](https://twitter.com/TuanPMT)


**LICENSE - "MIT License"**
