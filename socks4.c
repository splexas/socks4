#include "socks4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include <netinet/in.h>

#define UNUSED __attribute__((unused))

static inline void socks4_client_free(socks4_client_t *client)
{
    /*
    Calling bufferevent_free does not necessarily release the bufferevent.
    Reason being there are pending callback (to be executed) for the bufferevent. Meaning the bufferevent will be released only after all pending callbacks have been executed.
    */
    bufferevent_setcb(client->base, NULL, NULL, NULL, NULL);
    bufferevent_free(client->base);
    if (client->dst != NULL) {
        bufferevent_setcb(client->dst, NULL, NULL, NULL, NULL);
        bufferevent_free(client->dst);
    }
    free(client);
}

static void
dst_read_cb(struct bufferevent *bev, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    struct evbuffer *input = bufferevent_get_input(bev);

    if (bufferevent_write_buffer(client->base,
        input) != 0)
    {
        fprintf(stderr, "Failed to forward client -> destination\n");
        socks4_client_free(client);
        return;
    }
}

static void
dst_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    if (events & BEV_EVENT_CONNECTED) {
        fprintf(stdout, "Destination bufferevent connected!\n");

        struct sockaddr_in sin; 
        socklen_t len = sizeof(sin);
        if (getpeername(bufferevent_getfd(bev), (struct sockaddr *)&sin,
            &len) != 0)
        {
            socks4_client_free(client);
            return;
        }

        socks4_packet_t reply;
        reply.vn = 0; // reply code
        reply.cd = SOCKS4_CD_GRANTED;
        reply.dstport = sin.sin_port;
        reply.dstip = sin.sin_addr.s_addr;  

        if (bufferevent_write(client->base, (const void *)&reply,
            sizeof(reply)) != 0)
        {
            socks4_client_free(client);
            return;
        }

        client->dst = bev;
        return;
    }
    if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "Error from destination bufferevent\n");
        socks4_client_free(client);
        return;
    }
    if (events & BEV_EVENT_EOF) {
        fprintf(stderr, "EOF from destination bufferevent\n");
        socks4_client_free(client);
        return;
    }
}

static void
read_cb(struct bufferevent *bev, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    // client->base should not be NULL, client->dst could be.

    struct evbuffer *input = bufferevent_get_input(bev);

    /* -- debug -- */
    // todo: remove when useless
    /*char data[1000];
    evbuffer_copyout(input, data, 1000);
    int len = evbuffer_get_length(input);
    for (int i = 0; i < len; i++)
        printf("%d ", data[i]);
    printf("\n");*/
    /* ----- */

    if (client->dst == NULL) {
        /* peek first 8 bytes (socks4_packet_t) */
        struct evbuffer_iovec vec[1];
        if (evbuffer_peek(input, sizeof(socks4_packet_t), NULL, vec, 1) <= 0) {
            socks4_client_free(client);
            return;
        }

        socks4_packet_t *p = (socks4_packet_t *)vec[0].iov_base;

        if (p->vn != SOCKS4_VERSION) {
            socks4_client_free(client);
            return;
        }

        if (p->cd == SOCKS4_CD_CONNECT) {
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

            // create dst socket
            struct sockaddr_in sin;
            sin.sin_family = AF_INET;
            sin.sin_port = p->dstport;
            sin.sin_addr.s_addr = p->dstip;

            // draining because data was copied into sin
            evbuffer_drain(input, sizeof(socks4_packet_t) + uidlen + 1);

            struct event_base *base = bufferevent_get_base(bev);
            struct bufferevent *dstbev = bufferevent_socket_new(
                base, -1, BEV_OPT_CLOSE_ON_FREE);

            bufferevent_setcb(dstbev, dst_read_cb, NULL, dst_event_cb,
                (void *)client);
            bufferevent_enable(dstbev, EV_READ | EV_WRITE);
            
            if (bufferevent_socket_connect(dstbev,
                (struct sockaddr *)&sin, sizeof(sin)) < 0) {
                bufferevent_free(dstbev); // client->dst isnt yet set
                socks4_client_free(client);
                return;
            }
        }
        else if (p->cd == SOCKS4_CD_BIND) {
            printf("cd bind\n");  
        }
        else {
            socks4_client_free(client);
            return;
        }
    }
    else {
        if (bufferevent_write_buffer(client->dst, input) != 0) {
            fprintf(stderr, "Failed to forward destination -> client\n");
            socks4_client_free(client);
            return;
        }
    }
}

static void
event_cb(struct bufferevent *bev UNUSED, short events, void *ctx)
{
    socks4_client_t *client = (socks4_client_t *)ctx;
    if (events & BEV_EVENT_ERROR) {
        fprintf(stderr, "Error from socks4 client bufferevent\n");
        socks4_client_free(client);
        return;
    }
    if (events & BEV_EVENT_EOF) {
        fprintf(stderr, "EOF from socks4 client bufferevent\n");
        socks4_client_free(client);
        return;
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

    if (client == NULL) {
        fprintf(stderr, "Failed to allocate a socks4 client obj");
        bufferevent_free(bev);
        return;
    }

    /* init client */ 
    client->base = bev;
    client->dst = NULL;

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

    if (listener == NULL) {
        fprintf(stderr, "Couldn't create listener\n");
        return 1;
    }

    evconnlistener_set_error_cb(listener, accept_error_cb);
    return 0;
}