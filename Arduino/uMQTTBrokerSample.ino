/*
 * esp_uMQTT_broker demo for Arduino
 * 
 * The program starts a broker, subscribes to anything and publishs a topic every second.
 * Try to connect from a remote client and publish something - the console will show this as well.
 */

#include <ESP8266WiFi.h>

#include "mqtt_server.h"

/*
 * Your WiFi config here
 */
char ssid[] = "MySSID";  //  your network SSID (name)
char pass[] = "MyPassword";       // your network password


unsigned int mqttPort = 1883;       // the standard MQTT broker port
unsigned int max_subscriptions = 30;
unsigned int max_retained_topics = 30;

void data_callback(uint32_t *client /* we can ignore this */, const char* topic, uint32_t topic_len, const char *data, uint32_t lengh) {
  char topic_str[topic_len+1];
  os_memcpy(topic_str, topic, topic_len);
  topic_str[topic_len] = '\0';

  char data_str[lengh+1];
  os_memcpy(data_str, data, lengh);
  data_str[lengh] = '\0';

  Serial.print("received topic '");
  Serial.print(topic_str);
  Serial.print("' with data '");
  Serial.print(data_str);
  Serial.println("'");
}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

/*
 * Register the callback
 */
  MQTT_server_onData(data_callback);

/*
 * Start the broker
 */
  Serial.println("Starting MQTT broker");
  MQTT_server_start(mqttPort, max_subscriptions, max_retained_topics);

/*
 * Subscribe to anything
 */
  MQTT_local_subscribe((unsigned char *)"#", 0);
}

int counter = 0;

void loop()
{
  String myData(counter++);

/*
 * Publish the counter value as String
 */
  MQTT_local_publish((unsigned char *)"/MyBroker/count", (unsigned char *)myData.c_str(), myData.length(), 0, 0);
  
  // wait a second
  delay(1000);
}

