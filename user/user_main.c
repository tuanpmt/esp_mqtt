/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "os_type.h"

MQTT_Client mqttClient;
static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status)
{
  if (status == STATION_GOT_IP) {
    MQTT_Connect(&mqttClient);
  } else {
    MQTT_Disconnect(&mqttClient);
  }
}
static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Connected\r\n");
  // MQTT_Subscribe(client, "/mqtt/topic/0", 0);
  // MQTT_Subscribe(client, "/mqtt/topic/1", 1);
  // MQTT_Subscribe(client, "/mqtt/topic/2", 2);
  //
  // MQTT_Publish(client, "/mqtt/topic/0", "hello0", 6, 0, 0);
  // MQTT_Publish(client, "/mqtt/topic/1", "hello1", 6, 1, 0);
  // MQTT_Publish(client, "/mqtt/topic/2", "hello2", 6, 2, 0);

}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Disconnected\r\n");
}

static void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Published\r\n");
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
  char *topicBuf = (char*)os_zalloc(topic_len + 1),
        *dataBuf = (char*)os_zalloc(data_len + 1);

  MQTT_Client* client = (MQTT_Client*)args;
  os_memcpy(topicBuf, topic, topic_len);
  topicBuf[topic_len] = 0;
  os_memcpy(dataBuf, data, data_len);
  dataBuf[data_len] = 0;
  INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
  os_free(topicBuf);
  os_free(dataBuf);
}

void ICACHE_FLASH_ATTR print_info()
{
  INFO("\r\n\r\n[INFO] BOOTUP...\r\n");
  INFO("[INFO] SDK: %s\r\n", system_get_sdk_version());
  INFO("[INFO] Chip ID: %08X\r\n", system_get_chip_id());
  INFO("[INFO] Memory info:\r\n");
  system_print_meminfo();

  INFO("[INFO] -------------------------------------------\n");
  INFO("[INFO] Build time: %s\n", BUID_TIME);
  INFO("[INFO] -------------------------------------------\n");

}

// Handler to handle interupts of GPIO pins
LOCAL void gpio_intr_handler(uint32_t *args)
{

  uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

  // if the interrupt was by GPIO2
  if (gpio_status & BIT2)
  {
    // disable interrupt for GPIO2
    gpio_pin_intr_state_set(GPIO_ID_PIN(2), GPIO_PIN_INTR_DISABLE);

    // Do something, for example, increment whatyouwant indirectly
    INFO("GPIO2 pin detected change == %d\n", gpio_status);

    MQTT_Client* client = (MQTT_Client*)args;
    MQTT_Publish(client, MOTION_TOPIC, MOTION_PAYLOAD, strlen(MOTION_PAYLOAD), 0, 0);

    //clear interrupt status for GPIO2
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT2);

    // Reactivate interrupts for GPIO2
    gpio_pin_intr_state_set(GPIO_ID_PIN(2), GPIO_PIN_INTR_POSEDGE);
  }
}

static void ICACHE_FLASH_ATTR app_init(void)
{
  uart_init(BIT_RATE_115200, BIT_RATE_115200);
  print_info();

  // initialize gpio subsystem
  gpio_init();
  // set pin 2 to output mode
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
  // set pin2 to pullup.
  PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);
  //Set GPIO2 as input
  gpio_output_set(0, 0, 0, BIT2);

  // Disable interrupts by GPIO
  ETS_GPIO_INTR_DISABLE();
  // attach a handler
  // static int someVal;
  ETS_GPIO_INTR_ATTACH(gpio_intr_handler, &mqttClient);
  gpio_register_set(GPIO_PIN_ADDR(2),
    GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE)  |
    GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
    GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));

  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(2));
  gpio_pin_intr_state_set(GPIO_ID_PIN(2), GPIO_PIN_INTR_POSEDGE);
  // renable interrupts
  ETS_GPIO_INTR_ENABLE();

  MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
  //MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

  if ( !MQTT_InitClient(&mqttClient, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
  {
    INFO("Failed to initialize properly. Check MQTT version.\r\n");
    return;
  }
  //MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);
  MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
  MQTT_OnConnected(&mqttClient, mqttConnectedCb);
  MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
  MQTT_OnPublished(&mqttClient, mqttPublishedCb);
  MQTT_OnData(&mqttClient, mqttDataCb);

  WIFI_Connect(STA_SSID, STA_PASS, wifiConnectCb);
}

void user_init(void)
{
  system_init_done_cb(app_init);
}
