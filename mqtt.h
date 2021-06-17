#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <lwip/ip.h>

#ifndef BOOL
#define BOOL boolean
#endif

extern "C"
{
#include "mqtt/mqtt.h"
#include "tweetnacl.h"
}

#define N_ADDR_MAX 10
struct mqtt_if_data
{
  struct netif netif;
  ip_addr_t ipaddr;
  MQTT_Client *mqttcl;
  char *topic_pre;
  char *receive_topic;
  char *broadcast_topic;
  uint8_t key_set;
  u_char key[crypto_secretbox_KEYBYTES];
  u_char buf[2048];
  u_char cypherbuf_buf[2048];
  uint8_t n_addr;
  char* addr_topic[N_ADDR_MAX];
};

extern MQTT_Client mqttClient;
extern struct mqtt_if_data *mqtt_if;

#define MQTT_IF_TASK_PRIO 1
#define MQTT_IF_TASK_QUEUE_SIZE 2

extern os_event_t mqtt_if_procTaskQueue[MQTT_IF_TASK_QUEUE_SIZE];

static void ICACHE_FLASH_ATTR mqtt_if_Task(os_event_t *e)
{
  struct pbuf *pb = (struct pbuf *)e->par;
  if (pb == NULL)
    return;
  if (mqtt_if->netif.input(pb, &mqtt_if->netif) != ERR_OK)
  {
    pbuf_free(pb);
  }
}

void mqtt_if_input(struct mqtt_if_data *data, const char *topic, uint32_t topic_len, const char *mqtt_data, uint32_t mqtt_data_len);
struct mqtt_if_data *mqtt_if_add(MQTT_Client *cl, char *topic_prefix);

static void mqttOnData(uint32_t *args, const char *topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
  mqtt_if_input(mqtt_if, topic, topic_len, data, data_len);
}

#endif
