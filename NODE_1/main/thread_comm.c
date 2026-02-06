#include "thread_comm.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "openthread/udp.h"
#include "openthread/ip6.h"

#define TAG "THREAD_COMM"

static otUdpSocket socket;
static otInstance *ot = NULL;

static char self_id[NODE_ID_LEN];
static char self_role[16];

static thread_node_t nodes[MAX_NODES];
static thread_data_cb_t rx_cb = NULL;

/* ===================================================== */
/* Utils */
/* ===================================================== */

static thread_node_t *find_node(const char *id)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].active &&
            strcmp(nodes[i].id, id) == 0) {
            return &nodes[i];
        }
    }
    return NULL;
}

static thread_node_t *alloc_node(void)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].active) {
            memset(&nodes[i], 0, sizeof(thread_node_t));
            nodes[i].active = true;
            return &nodes[i];
        }
    }
    return NULL;
}

static void register_node(const char *id,
                          const char *role,
                          const otIp6Address *addr)
{
    thread_node_t *n = find_node(id);
    if (!n) {
        n = alloc_node();
        if (!n) return;
    }

    strncpy(n->id, id, NODE_ID_LEN - 1);
    strncpy(n->role, role, sizeof(n->role) - 1);
    n->eid = *addr;

    ESP_LOGI(TAG, "Node registered: %s (%s)", id, role);
}

/* ===================================================== */
/* UDP RX handler */
/* ===================================================== */

static void udp_handler(void *ctx,
                        otMessage *msg,
                        const otMessageInfo *info)
{
    char buf[PAYLOAD_LEN];
    int len;

    if (!msg || !info) return;

    len = otMessageRead(msg, 0, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';

    ESP_LOGI(TAG, "RX UDP: %s", buf);

    /* HELLO */
    if (strncmp(buf, "HELLO|", 6) == 0) {

        char id[NODE_ID_LEN] = {0};
        char role[8] = {0};

        sscanf(buf,
               "HELLO|id=%15[^|]|role=%7s",
               id, role);

        register_node(id, role, &info->mPeerAddr);
    
        thread_comm_send(id, "HELLO|id=node_1|role=receiver");
        return;
    }

    /* DATA */
    if (strncmp(buf, "DATA|", 5) == 0 && rx_cb) {

        char from[NODE_ID_LEN] = {0};
        char payload[PAYLOAD_LEN] = {0};

        sscanf(buf,
               "DATA|from=%15[^|]|%127[^\n]",
               from, payload);

        rx_cb(from, payload, &info->mPeerAddr);
    }
    ESP_LOGI(TAG,
    "RX from %04x: %s",
    info->mPeerAddr.mFields.m16[7],
    buf);
}
/* ===================================================== */
/* API */
/* ===================================================== */

void thread_comm_init(otInstance *instance,
                      const char *id,
                      const char *role)
{
    ot = instance;

    strncpy(self_id, id, NODE_ID_LEN - 1);
    strncpy(self_role, role, sizeof(self_role) - 1);

    memset(nodes, 0, sizeof(nodes));

    otError err;

    err = otUdpOpen(ot, &socket, udp_handler, NULL);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "otUdpOpen failed: %d", err);
        return;
    }

    otSockAddr addr = {0};
    addr.mPort = THREAD_COMM_PORT;

    err = otUdpBind(ot, &socket, &addr, OT_NETIF_UNSPECIFIED);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "otUdpBind failed: %d", err);
        return;
    }

    ESP_LOGI(TAG,
        "thread_comm init ok (id=%s role=%s)",
        self_id, self_role);
}

/* Gửi HELLO multicast – CHỦ ĐỘNG */
void thread_comm_announce(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf),
             "HELLO|id=%s|role=%s",
             self_id, self_role);

    otMessage *msg = otUdpNewMessage(ot, NULL);
    if (!msg) return;

    otMessageAppend(msg, buf, strlen(buf));

    otMessageInfo info = {0};
    info.mPeerPort = THREAD_COMM_PORT;
    otIp6AddressFromString("ff02::1", &info.mPeerAddr);

    otUdpSend(ot, &socket, msg, &info);

    ESP_LOGI(TAG, "HELLO announced");
}

void thread_comm_send(const char *node_id,
                      const char *payload)
{
    thread_node_t *n = find_node(node_id);
    if (!n) {
        ESP_LOGW(TAG, "Node %s not found", node_id);
        return;
    }

    char buf[PAYLOAD_LEN];
    snprintf(buf, sizeof(buf),
             "DATA|from=%s|%s",
             self_id, payload);

    otMessage *msg = otUdpNewMessage(ot, NULL);
    if (!msg) return;

    otMessageAppend(msg, buf, strlen(buf));

    otMessageInfo info = {0};
    info.mPeerAddr = n->eid;
    info.mPeerPort = THREAD_COMM_PORT;

    otUdpSend(ot, &socket, msg, &info);
}

void thread_comm_register_rx_cb(thread_data_cb_t cb)
{
    rx_cb = cb;
}

void thread_comm_print_nodes(void)
{
    printf("=== Thread nodes ===\n");
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].active) {
            printf("ID=%s role=%s\n",
                   nodes[i].id,
                   nodes[i].role);
        }
    }
}
