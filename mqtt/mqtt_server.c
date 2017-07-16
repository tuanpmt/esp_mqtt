#include "user_interface.h"
#include "mem.h"

/* Mem Debug
#undef os_free
#define os_free(x) {os_printf("F:%d-> %x\r\n", __LINE__,(x));vPortFree(x, "", 0);}

int my_os_zalloc(int len, int line) {
int _v = pvPortZalloc(len, "", 0);
os_printf("A:%d-> %x (%d)\r\n", line, _v, len);
return _v;
}
#undef os_zalloc
#define os_zalloc(x) my_os_zalloc(x, __LINE__)
*/

#include "mqtt_server.h"
#include "mqtt_topics.h"
#include "mqtt_topiclist.h"
#include "mqtt_retainedlist.h"
#include "debug.h"

#ifndef QUEUE_BUFFER_SIZE
#define QUEUE_BUFFER_SIZE     2048
#endif

#define MAX_SUBS_PER_REQ      16

#define MQTT_SERVER_TASK_PRIO        1
#define MQTT_TASK_QUEUE_SIZE  1
#define MQTT_SEND_TIMOUT      5

os_event_t mqtt_procServerTaskQueue[MQTT_TASK_QUEUE_SIZE];

LOCAL uint8_t zero_len_id[2] = { 0, 0 };

MQTT_ClientCon *clientcon_list;
LOCAL MqttDataCallback local_data_cb = NULL;

//#define MQTT_INFO os_printf
#define MQTT_WARNING os_printf
#define MQTT_ERROR os_printf


bool ICACHE_FLASH_ATTR print_topic(topic_entry *topic, void* user_data)
{
  if (topic->clientcon == LOCAL_MQTT_CLIENT) {
    MQTT_INFO("MQTT: Client: LOCAL Topic: \"%s\" QoS: %d\r\n", topic->topic, topic->qos);
  } else {
    MQTT_INFO("MQTT: Client: %s Topic: \"%s\" QoS: %d\r\n", topic->clientcon->connect_info.client_id, topic->topic, topic->qos);
  }
  return false;
}


bool ICACHE_FLASH_ATTR publish_topic(topic_entry *topic_e, uint8_t *topic, uint8_t *data, uint16_t data_len)
{
MQTT_ClientCon *clientcon = topic_e->clientcon;
uint16_t message_id = 0;

  if (topic_e->clientcon == LOCAL_MQTT_CLIENT) {
    MQTT_INFO("MQTT: Client: LOCAL Topic: \"%s\" QoS: %d\r\n", topic_e->topic, topic_e->qos);
    if (local_data_cb != NULL)
      local_data_cb(NULL, topic, os_strlen(topic), data, data_len);
    return true;
  }

  MQTT_INFO("MQTT: Client: %s Topic: \"%s\" QoS: %d\r\n", clientcon->connect_info.client_id, topic_e->topic, topic_e->qos);

  clientcon->mqtt_state.outbound_message = 
       mqtt_msg_publish(&clientcon->mqtt_state.mqtt_connection, topic, data, data_len, topic_e->qos, 0, &message_id);
  if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
    MQTT_ERROR("MQTT: Queue full\r\n");
    return false;
  }
  return true;
}


bool ICACHE_FLASH_ATTR publish_retainedtopic(retained_entry *entry, MQTT_ClientCon *clientcon)
{
uint16_t message_id = 0;

  MQTT_INFO("MQTT: Client: %s Topic: \"%s\" QoS: %d\r\n", clientcon->connect_info.client_id, entry->topic, entry->qos);

  clientcon->mqtt_state.outbound_message = 
       mqtt_msg_publish(&clientcon->mqtt_state.mqtt_connection, entry->topic, entry->data, entry->data_len, entry->qos, 1, &message_id);
  if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
    MQTT_ERROR("MQTT: Queue full\r\n");
    return false;
  }
  return true;
}


bool ICACHE_FLASH_ATTR activate_next_client()
{
MQTT_ClientCon *clientcon = clientcon_list;

  for (clientcon = clientcon_list; clientcon != NULL; clientcon = clientcon->next) {
    if (!QUEUE_IsEmpty(&clientcon->msgQueue)) {
      MQTT_INFO("MQTT: Next message to client: %s\r\n", clientcon->connect_info.client_id);
      system_os_post(MQTT_SERVER_TASK_PRIO, 0, (os_param_t)clientcon);  
      return true;
     }
  }
  return true;
}


bool ICACHE_FLASH_ATTR
MQTT_InitClientCon(MQTT_ClientCon *mqttClientCon)
{
  uint32_t temp;
  MQTT_INFO("MQTT:InitClientCon\r\n");

  mqttClientCon->connState = TCP_CONNECTED;

  os_memset(&mqttClientCon->connect_info, 0, sizeof(mqtt_connect_info_t));

  mqttClientCon->connect_info.client_id = zero_len_id;
  mqttClientCon->protocolVersion = 0;

  mqttClientCon->mqtt_state.in_buffer = (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
  mqttClientCon->mqtt_state.in_buffer_length = MQTT_BUF_SIZE;
  mqttClientCon->mqtt_state.out_buffer =  (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
  mqttClientCon->mqtt_state.out_buffer_length = MQTT_BUF_SIZE;
  mqttClientCon->mqtt_state.connect_info = &mqttClientCon->connect_info;

  mqtt_msg_init(&mqttClientCon->mqtt_state.mqtt_connection, mqttClientCon->mqtt_state.out_buffer, mqttClientCon->mqtt_state.out_buffer_length);

  QUEUE_Init(&mqttClientCon->msgQueue, QUEUE_BUFFER_SIZE);

  mqttClientCon->next = clientcon_list;
  clientcon_list = mqttClientCon;

  return true;
}


bool ICACHE_FLASH_ATTR
MQTT_DeleteClientCon(MQTT_ClientCon *mqttClientCon)
{
  MQTT_INFO("MQTT:DeleteClientCon\r\n");

  if (mqttClientCon == NULL)
    return;

  os_timer_disarm(&mqttClientCon->mqttTimer);

  if (mqttClientCon->pCon != NULL) {
    espconn_delete(mqttClientCon->pCon);
  }

  MQTT_ClientCon **p = &clientcon_list;
  while (*p != mqttClientCon && *p != NULL) {
    p=&((*p)->next);
  }
  if (*p == mqttClientCon)
    *p = (*p)->next;

  if (mqttClientCon->user_data != NULL) {
    os_free(mqttClientCon->user_data);
    mqttClientCon->user_data = NULL;
  }

  if (mqttClientCon->mqtt_state.in_buffer != NULL) {
    os_free(mqttClientCon->mqtt_state.in_buffer);
    mqttClientCon->mqtt_state.in_buffer = NULL;
  }

  if (mqttClientCon->mqtt_state.out_buffer != NULL) {
    os_free(mqttClientCon->mqtt_state.out_buffer);
    mqttClientCon->mqtt_state.out_buffer = NULL;
  }

  if (mqttClientCon->mqtt_state.outbound_message != NULL) {
    if (mqttClientCon->mqtt_state.outbound_message->data != NULL)
    {
      os_free(mqttClientCon->mqtt_state.outbound_message->data);
      mqttClientCon->mqtt_state.outbound_message->data = NULL;
    }
  }

  if (mqttClientCon->mqtt_state.mqtt_connection.buffer != NULL) {
    // Already freed but not NULL
    mqttClientCon->mqtt_state.mqtt_connection.buffer = NULL;
  }

  if (mqttClientCon->connect_info.client_id != NULL) {
    /* Don't attempt to free if it's the zero_len array */
    if ( ((uint8_t*)mqttClientCon->connect_info.client_id) != zero_len_id )
      os_free(mqttClientCon->connect_info.client_id);
    mqttClientCon->connect_info.client_id = NULL;
  }

  if (mqttClientCon->connect_info.username != NULL) {
    os_free(mqttClientCon->connect_info.username);
    mqttClientCon->connect_info.username = NULL;
  }

  if (mqttClientCon->connect_info.password != NULL) {
    os_free(mqttClientCon->connect_info.password);
    mqttClientCon->connect_info.password = NULL;
  }

  if (mqttClientCon->connect_info.will_topic != NULL) {
    // Publish the LWT
    find_topic(mqttClientCon->connect_info.will_topic, publish_topic, 
       mqttClientCon->connect_info.will_data, mqttClientCon->connect_info.will_data_len);
    activate_next_client();

    if (mqttClientCon->connect_info.will_retain) {
      update_retainedtopic(mqttClientCon->connect_info.will_topic, mqttClientCon->connect_info.will_data, 
         mqttClientCon->connect_info.will_data_len, mqttClientCon->connect_info.will_qos);
    }

    os_free(mqttClientCon->connect_info.will_topic);
    mqttClientCon->connect_info.will_topic = NULL;
  }

  if (mqttClientCon->connect_info.will_data != NULL) {
    os_free(mqttClientCon->connect_info.will_data);
    mqttClientCon->connect_info.will_data = NULL;
  }

  if (mqttClientCon->msgQueue.buf != NULL) {
    os_free(mqttClientCon->msgQueue.buf);
    mqttClientCon->msgQueue.buf = NULL;
  }

  delete_topic(mqttClientCon, 0);

  os_free(mqttClientCon);

  return true;
}


void ICACHE_FLASH_ATTR
MQTT_ServerDisconnect(MQTT_ClientCon *mqttClientCon)
{
  MQTT_INFO("MQTT:ServerDisconnect\r\n");

  mqttClientCon->mqtt_state.message_length_read = 0;
  mqttClientCon->connState = TCP_DISCONNECTED;
  system_os_post(MQTT_SERVER_TASK_PRIO, 0, (os_param_t)mqttClientCon);
  os_timer_disarm(&mqttClientCon->mqttTimer);
}


void ICACHE_FLASH_ATTR mqtt_server_timer(void *arg)
{
  MQTT_ClientCon *clientcon = (MQTT_ClientCon*)arg;

  if (clientcon->sendTimeout > 0)
    clientcon->sendTimeout--;
}


static void ICACHE_FLASH_ATTR MQTT_ClientCon_recv_cb(void *arg, char *pdata, unsigned short len)
{
  uint8_t msg_type;
  uint8_t msg_qos;
  uint16_t msg_id;
  enum mqtt_connect_flag msg_conn_ret;
  uint16_t topic_index;
  uint16_t topic_len;
  uint8_t *topic_str;
  uint8_t topic_buffer[MQTT_BUF_SIZE];
  uint16_t data_len;
  uint8_t *data;

  struct espconn *pCon = (struct espconn *)arg;

  MQTT_INFO("MQTT_ClientCon_recv_cb(): %d bytes of data received\n", len);

  MQTT_ClientCon *clientcon = (MQTT_ClientCon *)pCon->reverse;
  if (clientcon == NULL) {
    MQTT_ERROR("ERROR: No client status\r\n");
    return;
  }

  MQTT_INFO("MQTT: TCP: data received %d bytes (State: %d)\r\n", len, clientcon->connState);
  
  // Expect minimum the full fixed size header
  if (len+clientcon->mqtt_state.message_length_read > MQTT_BUF_SIZE || len < 2) {
    MQTT_ERROR("MQTT: Message too short/long\r\n");
    clientcon->mqtt_state.message_length_read = 0;
    return;
  }
READPACKET:
  os_memcpy(&clientcon->mqtt_state.in_buffer[clientcon->mqtt_state.message_length_read], pdata, len);
  clientcon->mqtt_state.message_length_read += len;

  clientcon->mqtt_state.message_length =
           mqtt_get_total_length(clientcon->mqtt_state.in_buffer, clientcon->mqtt_state.message_length_read);
  MQTT_INFO("MQTT: total_len: %d\r\n", clientcon->mqtt_state.message_length);
  if (clientcon->mqtt_state.message_length > clientcon->mqtt_state.message_length_read) {
    MQTT_WARNING("MQTT: Partial message received\r\n");
    return;
  }

  msg_type = mqtt_get_type(clientcon->mqtt_state.in_buffer);
  MQTT_INFO("MQTT: message_type: %d\r\n", msg_type);
  //msg_qos = mqtt_get_qos(clientcon->mqtt_state.in_buffer);
  switch (clientcon->connState) {
    case TCP_CONNECTED:
      switch (msg_type)
      {
        case MQTT_MSG_TYPE_CONNECT:

          MQTT_INFO("MQTT: Connect received, message_len: %d\r\n", clientcon->mqtt_state.message_length);

          if (clientcon->mqtt_state.message_length < sizeof(struct mqtt_connect_variable_header)+3) {
            MQTT_ERROR("MQTT: Too short Connect message\r\n");
            MQTT_ServerDisconnect(clientcon);
            return;
          }

          struct mqtt_connect_variable_header4* variable_header = 
                 (struct mqtt_connect_variable_header4*)&clientcon->mqtt_state.in_buffer[2];
          uint16_t var_header_len = sizeof(struct mqtt_connect_variable_header4);

          // We check MQTT v3.11 (version 4)
          if ((variable_header->lengthMsb<<8) + variable_header->lengthLsb == 4 && 
              variable_header->version == 4 &&
              os_strncmp(variable_header->magic, "MQTT", 4) == 0) {
            clientcon->protocolVersion = 4;
          } else {
            struct mqtt_connect_variable_header3* variable_header3 = 
                 (struct mqtt_connect_variable_header3*)&clientcon->mqtt_state.in_buffer[2];
            var_header_len = sizeof(struct mqtt_connect_variable_header3);

            // We check MQTT v3.1 (version 3)
            if ((variable_header3->lengthMsb<<8) + variable_header3->lengthLsb == 6 && 
                variable_header3->version == 3 &&
                os_strncmp(variable_header3->magic, "MQIsdp", 6) == 0) {
              clientcon->protocolVersion = 3;
              // adapt the remaining header fields (dirty as we overlay the two structs of different length)
              variable_header->version = variable_header3->version;
              variable_header->flags = variable_header3->flags;
              variable_header->keepaliveMsb = variable_header3->keepaliveMsb;
              variable_header->keepaliveLsb = variable_header3->keepaliveLsb;
            } else {
            // Neither found
              MQTT_WARNING("MQTT: Wrong protocoll version\r\n");
              msg_conn_ret = CONNECTION_REFUSE_PROTOCOL;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }
          }

          MQTT_INFO("MQTT: Connect flags %x\r\n", variable_header->flags);
          clientcon->connect_info.clean_session = (variable_header->flags & MQTT_CONNECT_FLAG_CLEAN_SESSION) != 0;
          if ((variable_header->flags & MQTT_CONNECT_FLAG_USERNAME) != 0 ||
              (variable_header->flags & MQTT_CONNECT_FLAG_PASSWORD) != 0) {
            MQTT_WARNING("MQTT: Connect option currently not supported\r\n");
            msg_conn_ret = CONNECTION_REFUSE_NOT_AUTHORIZED;
            clientcon->connState = TCP_DISCONNECTING;
            break;
          }
          clientcon->connect_info.keepalive = (variable_header->keepaliveMsb<<8) + variable_header->keepaliveLsb;
          espconn_regist_time(clientcon->pCon,  2*clientcon->connect_info.keepalive, 1);
          MQTT_INFO("MQTT: Keepalive %d\r\n", clientcon->connect_info.keepalive);

          // Get the client id
          uint16_t id_len = clientcon->mqtt_state.message_length - (2+var_header_len);
          const char *client_id = mqtt_get_str(&clientcon->mqtt_state.in_buffer[2+var_header_len], &id_len);
          if (client_id == NULL || id_len > 80) {
            MQTT_WARNING("MQTT: Client Id invalid\r\n");
            msg_conn_ret = CONNECTION_REFUSE_ID_REJECTED;
            clientcon->connState = TCP_DISCONNECTING;
            break;
          }
          if (id_len == 0) {
            if (clientcon->protocolVersion == 3) {
              MQTT_WARNING("MQTT: Empty client Id in MQTT 3.1\r\n");
              msg_conn_ret = CONNECTION_REFUSE_ID_REJECTED;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }
            if (!clientcon->connect_info.clean_session) {
              MQTT_WARNING("MQTT: Null client Id and NOT cleansession\r\n");
              msg_conn_ret = CONNECTION_REFUSE_ID_REJECTED;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }
            clientcon->connect_info.client_id = zero_len_id;
          } else {
            clientcon->connect_info.client_id = (char *) os_zalloc(id_len+1);
            if (clientcon->connect_info.client_id != NULL) {
              os_memcpy(clientcon->connect_info.client_id, client_id, id_len);
              clientcon->connect_info.client_id[id_len]=0;
              MQTT_INFO("MQTT: Client id %s\r\n", clientcon->connect_info.client_id);
            } else {
              MQTT_ERROR("MQTT: Out of mem\r\n");
              msg_conn_ret = CONNECTION_REFUSE_SERVER_UNAVAILABLE;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }
          }

          // Get the LWT
          clientcon->connect_info.will_retain = (variable_header->flags & MQTT_CONNECT_FLAG_WILL_RETAIN) != 0;
          clientcon->connect_info.will_qos = (variable_header->flags & 0x18)>>3;
          if (!(variable_header->flags & MQTT_CONNECT_FLAG_WILL)) {
            // Must be all 0 if no lwt is given
            if (clientcon->connect_info.will_retain || clientcon->connect_info.will_qos) {
              MQTT_WARNING("MQTT: Last Will flags invalid\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }
          } else {
            uint16_t lw_topic_len = clientcon->mqtt_state.message_length - (4+var_header_len+id_len);
            const char *lw_topic = mqtt_get_str(&clientcon->mqtt_state.in_buffer[4+var_header_len+id_len], &lw_topic_len);

            if (lw_topic == NULL) {
              MQTT_WARNING("MQTT: Last Will topic invalid\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }

            clientcon->connect_info.will_topic = (char *) os_zalloc(lw_topic_len+1);
            if (clientcon->connect_info.will_topic != NULL) {
              os_memcpy(clientcon->connect_info.will_topic, lw_topic, lw_topic_len);
              clientcon->connect_info.will_topic[lw_topic_len]=0;
              if (Topics_hasWildcards(clientcon->connect_info.will_topic)) {
                MQTT_WARNING("MQTT: Last Will topic has wildcards\r\n");
                MQTT_ServerDisconnect(clientcon);
                return;
              }
              MQTT_INFO("MQTT: LWT topic %s\r\n", clientcon->connect_info.will_topic);
            } else {
              MQTT_ERROR("MQTT: Out of mem\r\n");
              msg_conn_ret = CONNECTION_REFUSE_SERVER_UNAVAILABLE;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }

            uint16_t lw_data_len = clientcon->mqtt_state.message_length - (6+var_header_len+id_len+lw_topic_len);
            const char *lw_data = mqtt_get_str(&clientcon->mqtt_state.in_buffer[6+var_header_len+id_len+lw_topic_len], &lw_data_len);

            if (lw_data == NULL) {
              MQTT_WARNING("MQTT: Last Will data invalid\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }

            clientcon->connect_info.will_data = (char *) os_zalloc(lw_data_len);
            clientcon->connect_info.will_data_len = lw_data_len;
            if (clientcon->connect_info.will_data != NULL) {
              os_memcpy(clientcon->connect_info.will_data, lw_data, lw_data_len);
              MQTT_INFO("MQTT: %d bytes of LWT data\r\n", clientcon->connect_info.will_data_len);
            } else {
              MQTT_ERROR("MQTT: Out of mem\r\n");
              msg_conn_ret = CONNECTION_REFUSE_SERVER_UNAVAILABLE;
              clientcon->connState = TCP_DISCONNECTING;
              break;
            }
          }


          msg_conn_ret = CONNECTION_ACCEPTED;
          clientcon->connState = MQTT_DATA;
          break;

        default:
          MQTT_WARNING("MQTT: Invalid message\r\n");
          MQTT_ServerDisconnect(clientcon);
          return;
      }
      clientcon->mqtt_state.outbound_message = mqtt_msg_connack(&clientcon->mqtt_state.mqtt_connection, msg_conn_ret);
      if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
        MQTT_ERROR("MQTT: Queue full\r\n");
      }

      break;

    case MQTT_DATA:
      switch (msg_type)
      {
      uint8_t ret_codes[MAX_SUBS_PER_REQ];
      uint8_t num_subs;
  
        case MQTT_MSG_TYPE_SUBSCRIBE:
          MQTT_INFO("MQTT: Subscribe received, message_len: %d\r\n", clientcon->mqtt_state.message_length);
          // 2B fixed header + 2B variable header + 2 len + 1 char + 1 QoS 
          if (clientcon->mqtt_state.message_length < 8) {
            MQTT_ERROR("MQTT: Too short Subscribe message\r\n");
            MQTT_ServerDisconnect(clientcon);
            return;
          }
          msg_id = mqtt_get_id(clientcon->mqtt_state.in_buffer, clientcon->mqtt_state.in_buffer_length);
          MQTT_INFO("MQTT: Message id %d\r\n", msg_id);
	  topic_index = 4;
          num_subs = 0;
          while (topic_index < clientcon->mqtt_state.message_length && num_subs < MAX_SUBS_PER_REQ) {
            topic_len = clientcon->mqtt_state.message_length - topic_index;
            topic_str = mqtt_get_str(&clientcon->mqtt_state.in_buffer[topic_index], &topic_len);
            if (topic_str == NULL) {
              MQTT_WARNING("MQTT: Subscribe topic invalid\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }
            topic_index += 2 + topic_len;

            if (topic_index >= clientcon->mqtt_state.message_length) {
              MQTT_WARNING("MQTT: Subscribe QoS missing\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }
            uint8_t topic_QoS = clientcon->mqtt_state.in_buffer[topic_index++];

            os_memcpy(topic_buffer, topic_str, topic_len);
            topic_buffer[topic_len] = 0;
            MQTT_INFO("MQTT: Subscribed topic %s QoS %d\r\n", topic_buffer, topic_QoS);

            // the return codes, one per topic
            // For now we always give back error or QoS = 0 !!
            ret_codes[num_subs++] = add_topic(clientcon, topic_buffer, 0)?0:0x80;
            //iterate_topics(print_topic, 0);
          }
          MQTT_INFO("MQTT: Subscribe successful\r\n");

          clientcon->mqtt_state.outbound_message = mqtt_msg_suback(&clientcon->mqtt_state.mqtt_connection, ret_codes, num_subs, msg_id);
          if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
            MQTT_ERROR("MQTT: Queue full\r\n");
          }

          find_retainedtopic(topic_buffer, publish_retainedtopic, clientcon);

          break;

        case MQTT_MSG_TYPE_UNSUBSCRIBE:
          MQTT_INFO("MQTT: Unsubscribe received, message_len: %d\r\n", clientcon->mqtt_state.message_length);
          // 2B fixed header + 2B variable header + 2 len + 1 char 
          if (clientcon->mqtt_state.message_length < 7) {
            MQTT_ERROR("MQTT: Too short Unsubscribe message\r\n");
            MQTT_ServerDisconnect(clientcon);
            return;
          }
          msg_id = mqtt_get_id(clientcon->mqtt_state.in_buffer, clientcon->mqtt_state.in_buffer_length);
          MQTT_INFO("MQTT: Message id %d\r\n", msg_id);
	  topic_index = 4;
          while (topic_index < clientcon->mqtt_state.message_length) {
            uint16_t topic_len = clientcon->mqtt_state.message_length - topic_index;
            char *topic_str = mqtt_get_str(&clientcon->mqtt_state.in_buffer[topic_index], &topic_len);
            if (topic_str == NULL) {
              MQTT_WARNING("MQTT: Subscribe topic invalid\r\n");
              MQTT_ServerDisconnect(clientcon);
              return;
            }
            topic_index += 2 + topic_len;

            os_memcpy(topic_buffer, topic_str, topic_len);
            topic_buffer[topic_len] = 0;
            MQTT_INFO("MQTT: Unsubscribed topic %s\r\n", topic_buffer);

            delete_topic(clientcon, topic_buffer);
            //iterate_topics(print_topic, 0);
          }
          MQTT_INFO("MQTT: Unubscribe successful\r\n");

          clientcon->mqtt_state.outbound_message = mqtt_msg_unsuback(&clientcon->mqtt_state.mqtt_connection, msg_id);
          if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
            MQTT_ERROR("MQTT: Queue full\r\n");
          }
          break;

        case MQTT_MSG_TYPE_PUBLISH:
          MQTT_INFO("MQTT: Publish received, message_len: %d\r\n", clientcon->mqtt_state.message_length);

/*          if (msg_qos == 1)
            clientcon->mqtt_state.outbound_message = mqtt_msg_puback(&clientcon->mqtt_state.mqtt_connection, msg_id);
          else if (msg_qos == 2)
            clientcon->mqtt_state.outbound_message = mqtt_msg_pubrec(&clientcon->mqtt_state.mqtt_connection, msg_id);
          if (msg_qos == 1 || msg_qos == 2) {
            MQTT_INFO("MQTT: Queue response QoS: %d\r\n", msg_qos);
            if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
              MQTT_ERROR("MQTT: Queue full\r\n");
            }
          }
*/
          topic_len = clientcon->mqtt_state.in_buffer_length;
          topic_str = (uint8_t*)mqtt_get_publish_topic(clientcon->mqtt_state.in_buffer, &topic_len);
          os_memcpy(topic_buffer, topic_str, topic_len);
          topic_buffer[topic_len] = 0;
          data_len = clientcon->mqtt_state.in_buffer_length;
          data = (uint8_t*)mqtt_get_publish_data(clientcon->mqtt_state.in_buffer, &data_len);

          MQTT_INFO("MQTT: Published topic \"%s\"\r\n", topic_buffer);
          MQTT_INFO("MQTT: Matches to:\r\n");

          // Now find, if anything matches and enque publish message
          find_topic(topic_buffer, publish_topic, data, data_len);

          if (mqtt_get_retain(clientcon->mqtt_state.in_buffer)) {
            update_retainedtopic(topic_buffer, data, data_len, mqtt_get_qos(clientcon->mqtt_state.in_buffer));
          }
          
          break;

        case MQTT_MSG_TYPE_PINGREQ:
          MQTT_INFO("MQTT: receive MQTT_MSG_TYPE_PINGREQ\r\n");
          clientcon->mqtt_state.outbound_message = mqtt_msg_pingresp(&clientcon->mqtt_state.mqtt_connection);
          if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
            MQTT_ERROR("MQTT: Queue full\r\n");
          }
          break;

        case MQTT_MSG_TYPE_DISCONNECT:
          MQTT_INFO("MQTT: receive MQTT_MSG_TYPE_DISCONNECT\r\n");

          // Clean session close: no LWT
          if (clientcon->connect_info.will_topic != NULL) {
            os_free(clientcon->connect_info.will_topic);
            clientcon->connect_info.will_topic = NULL;
          }
          MQTT_ServerDisconnect(clientcon);
          return;

/*
        case MQTT_MSG_TYPE_PUBACK:
          if (clientcon->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && clientcon->mqtt_state.pending_msg_id == msg_id) {
            MQTT_INFO("MQTT: received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish\r\n");
          }

          break;
        case MQTT_MSG_TYPE_PUBREC:
          clientcon->mqtt_state.outbound_message = mqtt_msg_pubrel(&clientcon->mqtt_state.mqtt_connection, msg_id);
          if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
            MQTT_ERROR("MQTT: Queue full\r\n");
          }
          break;
        case MQTT_MSG_TYPE_PUBREL:
          clientcon->mqtt_state.outbound_message = mqtt_msg_pubcomp(&clientcon->mqtt_state.mqtt_connection, msg_id);
          if (QUEUE_Puts(&clientcon->msgQueue, clientcon->mqtt_state.outbound_message->data, clientcon->mqtt_state.outbound_message->length) == -1) {
            MQTT_ERROR("MQTT: Queue full\r\n");
          }
          break;
        case MQTT_MSG_TYPE_PUBCOMP:
          if (clientcon->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && clientcon->mqtt_state.pending_msg_id == msg_id) {
            MQTT_INFO("MQTT: receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish\r\n");
          }
          break;
        case MQTT_MSG_TYPE_PINGRESP:
          // Ignore
          break;
*/

        default:
          // Ignore
          break;
      }
     break;
  }

  // More than one MQTT command in the packet?
  len = clientcon->mqtt_state.message_length_read;
  if (clientcon->mqtt_state.message_length < len) {
    len -= clientcon->mqtt_state.message_length;
    pdata += clientcon->mqtt_state.message_length;
    clientcon->mqtt_state.message_length_read = 0;

    MQTT_INFO("MQTT: Get another received message\r\n");
    goto READPACKET;
  }  
  clientcon->mqtt_state.message_length_read = 0;

  if (msg_type != MQTT_MSG_TYPE_PUBLISH) {
    system_os_post(MQTT_SERVER_TASK_PRIO, 0, (os_param_t)clientcon);
  } else {
    activate_next_client();
  }
}


/* Called when a client has disconnected from the MQTT server */
static void ICACHE_FLASH_ATTR MQTT_ClientCon_discon_cb(void *arg)
{
    struct espconn *pCon = (struct espconn *)arg;
    MQTT_ClientCon *clientcon = (MQTT_ClientCon *)pCon->reverse;

    MQTT_INFO("MQTT_ClientCon_discon_cb(): client disconnected\n");
    MQTT_DeleteClientCon(clientcon);
}


static void ICACHE_FLASH_ATTR MQTT_ClientCon_sent_cb(void *arg)
{
    struct espconn *pCon = (struct espconn *)arg;
    MQTT_ClientCon *clientcon = (MQTT_ClientCon *)pCon->reverse;

    MQTT_INFO("MQTT_ClientCon_sent_cb(): Data sent\n");

    clientcon->sendTimeout = 0;

    if (clientcon->connState == TCP_DISCONNECTING) {
      clientcon->connState = TCP_DISCONNECTED;
      espconn_disconnect(clientcon->pCon);
    }

    activate_next_client();
}


/* Called when a client connects to the MQTT server */
static void ICACHE_FLASH_ATTR MQTT_ClientCon_connected_cb(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    MQTT_ClientCon *mqttClientCon;

    MQTT_INFO("MQTT_ClientCon_connected_cb(): Client connected\r\n");

    espconn_regist_sentcb(pespconn,     MQTT_ClientCon_sent_cb);
    espconn_regist_disconcb(pespconn,   MQTT_ClientCon_discon_cb);
    espconn_regist_recvcb(pespconn,     MQTT_ClientCon_recv_cb);
    espconn_regist_time(pespconn,  30, 1);

    mqttClientCon = (MQTT_ClientCon *)os_zalloc(sizeof(MQTT_ClientCon));
    pespconn->reverse = mqttClientCon;
    if (mqttClientCon == NULL) {
	MQTT_ERROR("ERROR: Cannot allocate client status\r\n");
	return;
    }

    MQTT_InitClientCon(mqttClientCon);
    mqttClientCon->pCon = pespconn;

    os_timer_setfn(&mqttClientCon->mqttTimer, (os_timer_func_t *)mqtt_server_timer, mqttClientCon);
    os_timer_arm(&mqttClientCon->mqttTimer, 1000, 1);
}


void ICACHE_FLASH_ATTR
MQTT_ServerTask(os_event_t *e)
{
  MQTT_ClientCon *clientcon = (MQTT_ClientCon *)e->par;
  uint8_t dataBuffer[MQTT_BUF_SIZE];
  uint16_t dataLen;
  if (e->par == 0)
    return;

  MQTT_INFO("MQTT: Server task activated - state %d\r\n", clientcon->connState);

  switch (clientcon->connState) {

    case TCP_DISCONNECTED:
      MQTT_INFO("MQTT: Disconnect\r\n");
      espconn_disconnect(clientcon->pCon);
      break;
    case TCP_DISCONNECTING:
    case MQTT_DATA:
      if (!QUEUE_IsEmpty(&clientcon->msgQueue) && clientcon->sendTimeout == 0 &&
          QUEUE_Gets(&clientcon->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == 0) {

        clientcon->mqtt_state.pending_msg_type = mqtt_get_type(dataBuffer);
        clientcon->mqtt_state.pending_msg_id = mqtt_get_id(dataBuffer, dataLen);

        clientcon->sendTimeout = MQTT_SEND_TIMOUT;
        MQTT_INFO("MQTT: Sending, type: %d, id: %04X\r\n", clientcon->mqtt_state.pending_msg_type, clientcon->mqtt_state.pending_msg_id);
        espconn_send(clientcon->pCon, dataBuffer, dataLen);

        clientcon->mqtt_state.outbound_message = NULL;
        break;
      }
      if (clientcon->connState == TCP_DISCONNECTING) {
        MQTT_ServerDisconnect(clientcon);
      }
      break;
  }
}


bool ICACHE_FLASH_ATTR MQTT_server_start(uint16_t portno, uint16_t max_subscriptions, uint16_t max_retained_topics)
{
    MQTT_INFO("Starting MQTT server on port %d\r\n", portno);

    if (!create_topiclist(max_subscriptions)) return false;
    if (!create_retainedlist(max_retained_topics)) return false;
    clientcon_list = NULL;

    struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
    if (pCon == NULL)
      return false;

    /* Equivalent to bind */
    pCon->type  = ESPCONN_TCP;
    pCon->state = ESPCONN_NONE;
    pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    if (pCon->proto.tcp == NULL) {
      os_free(pCon);
      return false;
    }
    pCon->proto.tcp->local_port = portno;

    /* Register callback when clients connect to the server */
    espconn_regist_connectcb(pCon, MQTT_ClientCon_connected_cb);

    /* Put the connection in accept mode */
    espconn_accept(pCon);

    system_os_task(MQTT_ServerTask, MQTT_SERVER_TASK_PRIO, mqtt_procServerTaskQueue, MQTT_TASK_QUEUE_SIZE);
    return true;
}


bool ICACHE_FLASH_ATTR MQTT_local_publish(uint8_t* topic, uint8_t* data, uint16_t data_length, uint8_t qos, uint8_t retain)
{
  find_topic(topic, publish_topic, data, data_length);
  if (retain)
    update_retainedtopic(topic, data, data_length, qos);
  activate_next_client();
  return true;
}


bool ICACHE_FLASH_ATTR MQTT_local_subscribe(uint8_t* topic, uint8_t qos)
{
  return add_topic(LOCAL_MQTT_CLIENT, topic, 0);
}

bool ICACHE_FLASH_ATTR MQTT_local_unsubscribe(uint8_t* topic)
{
  return delete_topic(LOCAL_MQTT_CLIENT, topic);
}

void ICACHE_FLASH_ATTR MQTT_local_onData(MqttDataCallback dataCb)
{
  local_data_cb = dataCb;
}

