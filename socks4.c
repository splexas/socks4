#include "socks4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include <netinet/in.h>

#define UNUSED __attribute__((unused))

static void
read_cb(struct bufferevent *bev, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    struct evbuffer *input = bufferevent_get_input(bev);

    /* -- debug -- */
    // todo: remove when useless
    char data[1000];
    evbuffer_copyout(input, data, 1000);
    int len = evbuffer_get_length(input);
    for (int i = 0; i < len; i++)
        printf("%d ", data[i]);
    printf("\n");
    /* ----- */

    /* peek first 2 bytes */
    if (!client->dst) {
        struct evbuffer_iovec vec[1];
        evbuffer_peek(input, sizeof(socks4_packet_t), NULL, vec, 1);

        socks4_packet_t *p = (socks4_packet_t *)vec[0].iov_base;

        if (p->vn != SOCKS4_VERSION) {
            bufferevent_free(bev);
            free(client);
            return;
        }

        if (p->cd == SOCKS4_CD_CONNECT) {
            printf("cd connect\n");

            struct evbuffer_iovec tmp[1];
            struct evbuffer_ptr ptr;

            int uidlen = 0;
            while (uidlen < 256) {
                if (evbuffer_ptr_set(input, &ptr,
                    sizeof(socks4_packet_t) + uidlen, EVBUFFER_PTR_SET) != 0)
                {
                    break;
                }

                if (evbuffer_peek(input, 1, &ptr, tmp, 1) <= 0)
                    break;

                char ch = *(char *)tmp[0].iov_base;
                if (ch == 0)
                    break;

                uidlen++;
            }

            printf("uidlen: %d\n", uidlen);
            
            // create dst socket

            evbuffer_drain(input, sizeof(socks4_packet_t) + uidlen + 1);
        }
        else if (p->cd == SOCKS4_CD_BIND) {
            printf("cd bind\n");  
        }
        else {
            bufferevent_free(bev);
            free(client);
            return;
        }
    }
}

static void
event_cb(struct bufferevent *bev, short events, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "Error from bufferevent\n");
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        bufferevent_free(bev);
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address UNUSED, int socklen UNUSED,
    void *ctx UNUSED)
{
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
            base, fd, BEV_OPT_CLOSE_ON_FREE);

    socks4_client_t *client = (socks4_client_t *)
        malloc(sizeof(socks4_client_t));

    if (!client) {
        fprintf(stderr, "Failed to allocate a socks4 client obj");
        return;
    }

    bufferevent_setcb(bev, read_cb, NULL, event_cb, (void *)client);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx UNUSED)
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
        fprintf(stderr, "inet_pton failed\n");
        return 1;
    }

    struct evconnlistener *listener = evconnlistener_new_bind(base,
        accept_conn_cb, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
        (struct sockaddr *)&sin, sizeof(sin));

    if (!listener) {
        fprintf(stderr, "Couldn't create listener\n");
        return 1;
    }

    evconnlistener_set_error_cb(listener, accept_error_cb);
    return 0;
}