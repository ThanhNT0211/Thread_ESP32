#pragma once

#include <stdbool.h>
#include "openthread/ip6.h"
#include "openthread/instance.h"

#define THREAD_COMM_PORT   61631
#define MAX_NODES          8
#define NODE_ID_LEN        16
#define PAYLOAD_LEN        128

typedef struct {
    bool active;
    char id[NODE_ID_LEN];
    char role[16];
    otIp6Address eid;
} thread_node_t;

typedef void (*thread_data_cb_t)(
    const char *from,
    const char *payload,
    const otIp6Address *src);

/* API */
void thread_comm_init(otInstance *instance,
                      const char *self_id,
                      const char *self_role);

void thread_comm_announce(void);          /* gửi HELLO multicast */
void thread_comm_send(const char *node_id,
                      const char *payload);

void thread_comm_register_rx_cb(thread_data_cb_t cb);
void thread_comm_print_nodes(void);
