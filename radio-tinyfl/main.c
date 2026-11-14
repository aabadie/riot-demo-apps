/*
 * SPDX-FileCopyrightText: 2026 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief       TinyFL: federated learning over the radio
 *
 * Each node learns a Gaussian naive Bayes classifier from its own samples,
 * which only cover 2 of the 4 classes of the dataset, and broadcasts the
 * statistics of its model (78 bytes) in UDP to the link-local multicast
 * address. It relays the statistics of the other nodes for a few hops.
 * Merging all of them gives each node the model it would have learned
 * from the samples of all the nodes. No sample leaves a node. See fl.c.
 *
 * Every 5 seconds the node prints the accuracy of its local model and of
 * its federated model on a test set of all the classes:
 *
 *     A <node> <s> local <%> fed <%> nodes <n> samples <own> <all> sent <n> received <n>
 */

#include <stdio.h>
#include <string.h>

#include "net/gnrc/netif.h"
#include "net/ipv6/addr.h"
#include "net/netopt.h"
#include "net/sock/udp.h"
#include "random.h"
#include "ztimer.h"

#include "fl.h"

#ifndef FL_PORT
#define FL_PORT         (4747U)
#endif
#ifndef FL_SAMPLE_MS
#define FL_SAMPLE_MS    (250U)      /* one new local sample */
#endif
#ifndef FL_LOCAL_MAX
#define FL_LOCAL_MAX    (200U)      /* local samples, then the node stops sampling */
#endif
#ifndef FL_SEND_MS
#define FL_SEND_MS      (2000U)     /* own statistics */
#endif
#ifndef FL_RELAY_MS
#define FL_RELAY_MS     (1000U)     /* statistics of another node */
#endif
#ifndef FL_EVAL_MS
#define FL_EVAL_MS      (5000U)
#endif

static uint16_t _id;
static unsigned _classes[2];
static fl_stats_t _own;
static uint16_t _own_seq;
static bool _own_changed;
static fl_table_t _table;
static fl_stats_t _merged;
static unsigned _relay_next;
static sock_udp_t _sock;
static sock_udp_ep_t _group = {
    .family = AF_INET6,
    .port = FL_PORT,
};
static uint8_t _buf[FL_MSG_LEN];
static unsigned _sent, _received;

static uint16_t _node_id(gnrc_netif_t *netif)
{
    uint8_t addr[GNRC_NETIF_L2ADDR_MAXLEN];
    int len = gnrc_netapi_get(netif->pid, NETOPT_ADDRESS, 0, addr, sizeof(addr));

    if (len >= 2) {
        return (uint16_t)((addr[len - 2] << 8) | addr[len - 1]);
    }
    return (uint16_t)random_uint32();
}

static void _send(uint16_t origin, uint16_t seq, uint8_t hops, const fl_stats_t *stats)
{
    size_t len = fl_encode(_buf, origin, seq, hops, stats);

    if (sock_udp_send(&_sock, _buf, len, &_group) < 0) {
        puts("send failed");
        return;
    }
    _sent++;
}

/* the next entry worth relaying, round robin */
static void _relay(void)
{
    for (unsigned tries = 0; tries < FL_MAX_NODES; tries++) {
        const fl_entry_t *e = &_table.e[_relay_next];
        _relay_next = (_relay_next + 1) % FL_MAX_NODES;
        if (e->used && e->hops < FL_MAX_HOPS) {
            _send(e->origin, e->seq, e->hops, &e->stats);
            return;
        }
    }
}

static void _receive(uint32_t timeout_ms)
{
    static uint8_t buf[FL_MSG_LEN + 1];
    static fl_stats_t stats;
    uint16_t origin, seq;
    uint8_t hops;

    ssize_t len = sock_udp_recv(&_sock, buf, sizeof(buf), timeout_ms * US_PER_MS, NULL);
    if (len <= 0) {
        return;
    }
    if (fl_decode(buf, (size_t)len, &origin, &seq, &hops, &stats) != 0) {
        return;
    }
    if (origin == _id) {
        /* our own statistics relayed back: after a reset, the others still
           have the statistics from before, with a higher sequence number.
           Jump past it, or they would ignore the new ones. */
        if ((int16_t)(seq - _own_seq) > 0) {
            printf("own sequence number %u seen, jumping past it\n", seq);
            _own_seq = seq;
            _own_changed = true;
        }
        return;
    }
    _received++;
    if (fl_table_update(&_table, origin, seq, (uint8_t)(hops + 1), &stats)) {
        printf("R %04x seq %u hops %u, %u nodes known\n", origin, seq, hops + 1,
               fl_table_count(&_table));
    }
}

static void _evaluate(uint32_t now)
{
    fl_model_build(FL_LOCAL, &_own);
    fl_table_merge(&_table, _id, &_own, &_merged);
    fl_model_build(FL_FEDERATED, &_merged);
    unsigned local = (unsigned)(fl_model_accuracy(FL_LOCAL) * 1000 + 0.5f);
    unsigned fed = (unsigned)(fl_model_accuracy(FL_FEDERATED) * 1000 + 0.5f);

    printf("A %04x %lu local %u.%u fed %u.%u nodes %u samples %lu %lu sent %u received %u\n",
           _id, (unsigned long)(now / 1000), local / 10, local % 10, fed / 10, fed % 10,
           fl_table_count(&_table) + 1, (unsigned long)fl_stats_count(&_own),
           (unsigned long)fl_stats_count(&_merged), _sent, _received);
}

int main(void)
{
    gnrc_netif_t *netif = gnrc_netif_iter(NULL);
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    fl_rng_t rng;

    puts("TinyFL");
    if (!netif) {
        puts("no network interface");
        return 1;
    }
    _id = _node_id(netif);
    fl_node_classes(_id, _classes);
    printf("node %04x sees classes %u and %u\n", _id, _classes[0], _classes[1]);

    ipv6_addr_set_all_nodes_multicast((ipv6_addr_t *)&_group.addr.ipv6,
                                      IPV6_ADDR_MCAST_SCP_LINK_LOCAL);
    _group.netif = netif->pid;
    local.port = FL_PORT;
    if (sock_udp_create(&_sock, &local, NULL, 0) < 0) {
        puts("cannot create the UDP socket");
        return 1;
    }

    fl_rng_seed(&rng, _id ^ 0xa5a5a5a5U);
    fl_stats_reset(&_own);
    fl_table_reset(&_table);

    uint32_t start = ztimer_now(ZTIMER_MSEC);
    /* spread the nodes in time */
    uint32_t next_sample = start;
    uint32_t next_send = start + random_uint32_range(0, FL_SEND_MS);
    uint32_t next_relay = start + random_uint32_range(0, FL_RELAY_MS);
    uint32_t next_eval = start + FL_EVAL_MS;

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_MSEC);

        if ((int32_t)(now - next_sample) >= 0) {
            next_sample += FL_SAMPLE_MS;
            uint32_t n = fl_stats_count(&_own);
            if (n < FL_LOCAL_MAX) {
                float x[FL_FEATURES];
                unsigned cls = _classes[n % 2];
                fl_draw(&rng, cls, x);
                fl_stats_add(&_own, cls, x);
                _own_changed = true;
            }
        }
        if ((int32_t)(now - next_send) >= 0) {
            next_send += FL_SEND_MS - FL_SEND_MS / 8 +
                         random_uint32_range(0, FL_SEND_MS / 4);
            if (_own_changed) {
                _own_seq++;
                _own_changed = false;
            }
            _send(_id, _own_seq, 0, &_own);
        }
        if ((int32_t)(now - next_relay) >= 0) {
            next_relay += FL_RELAY_MS - FL_RELAY_MS / 8 +
                          random_uint32_range(0, FL_RELAY_MS / 4);
            _relay();
        }
        if ((int32_t)(now - next_eval) >= 0) {
            next_eval += FL_EVAL_MS;
            _evaluate(now - start);
        }

        /* wait for messages until the next action */
        uint32_t next = next_sample;
        next = (int32_t)(next_send - next) < 0 ? next_send : next;
        next = (int32_t)(next_relay - next) < 0 ? next_relay : next;
        next = (int32_t)(next_eval - next) < 0 ? next_eval : next;
        now = ztimer_now(ZTIMER_MSEC);
        _receive((int32_t)(next - now) > 0 ? next - now : 1);
    }
    return 0;
}
