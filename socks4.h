#ifndef SOCKS4_H
#define SOCKS4_H

#include <event2/event.h>

int socks4_init(struct event_base *base, const char *ip,
    const unsigned short port);

#endif