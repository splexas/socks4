#include "socks4.h"

#include <stdio.h>
#include <string.h>

#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include <netinet/in.h>

static void
read_cb(struct bufferevent *bev, void *ctx)
{

}

static void
event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
        fprintf(stderr, "Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        bufferevent_free(bev);
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
            base, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(bev, read_cb, NULL, event_cb, NULL);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

int socks4_init(struct event_base *base, const char *ip,
    const unsigned short port)
{
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (evutil_inet_pton(AF_INET, ip, &sin.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed");
        return 1;
    }

    struct evconnlistener *listener = evconnlistener_new_bind(base,
        accept_conn_cb, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
        (struct sockaddr *)&sin, sizeof(sin));

    if (!listener) {
        fprintf(stderr, "Couldn't create listener");
        return 1;
    }

    evconnlistener_set_error_cb(listener, accept_error_cb);
    return 0;
}