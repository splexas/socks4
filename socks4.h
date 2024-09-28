#ifndef SOCKS4_H
#define SOCKS4_H

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <stdbool.h>

#define SOCKS4_VERSION    0x04
#define SOCKS4_CD_CONNECT 0x01
#define SOCKS4_CD_BIND    0x02

typedef struct socks4_client {
    struct bufferevent *dst;
} socks4_client_t;

typedef struct socks4_packet {
    unsigned char vn;
    unsigned char cd;
    unsigned short dstport;
    unsigned int dstip;
} __attribute__((packed)) socks4_packet_t;

int socks4_init(struct event_base *base, const char *ip,
    const unsigned short port);

#endif