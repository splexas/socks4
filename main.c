#include <stdio.h>
#include <event2/event.h>
#include "socks4.h"

int main(void) {
    struct event_base *base = event_base_new();

    if (socks4_init(base, "127.0.0.1", 55555) != 0) {
        fprintf(stderr, "socks4_init failed");
        event_base_free(base);
        return 1;
    }

    event_base_dispatch(base);
    return 0;
}