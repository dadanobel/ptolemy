/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pt_event_poll: the waiter's look before it sleeps grows after sleeps that were soon over, is caught by a signal
 * inside it, and is gone after a long sleep. Times are far apart (a millisecond against looks of 20 ms and more and a
 * limit of 640 ms), so a busy machine does not change the outcome; it takes two seconds. cc -Iinclude tests/event-poll.c src/platform.c -pthread */
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static PtEvent e;
static useconds_t after;
static void *signaller(void *unused) {
    (void)unused;
    usleep(after);
    pt_event_signal(&e);
    return NULL;
}
static void one(useconds_t us, int timed) { /* a wait that a signal ends after us microseconds */
    pthread_t t;
    uint64_t g = pt_event_generation(&e);
    after = us;
    assert(!pthread_create(&t, 0, signaller, 0));
    assert(pt_event_wait(&e, g, timed ? 2000000000 : UINT64_MAX) == 1);
    pthread_join(t, 0);
}
int main(void) {
    pt_event_init(&e);
    one(1000, 0);
    assert(!e.poll_ns); /* off until asked for */
    pt_event_poll(&e, 640000000);
    one(1000, 0);
    assert(e.poll_ns == 20000000); /* slept, over within the limit: a look of limit/32 */
    one(1000, 0);
    assert(e.poll_ns == 20000000); /* the signal came inside the look: no sleep, nothing to learn */
    one(50000, 1);
    assert(e.poll_ns == 40000000); /* past the look, within the limit: twice as long */
    one(1000, 0);
    assert(e.poll_ns == 40000000);
    one(800000, 0);
    assert(!e.poll_ns); /* a long sleep: the waiter is idle */
    uint64_t g = pt_event_generation(&e);
    assert(pt_event_wait(&e, g, 1000000) == 0 && !e.poll_ns); /* a timeout is not a signal */
    pt_event_signal(&e);
    assert(pt_event_wait(&e, g, UINT64_MAX) == 1 && !e.poll_ns); /* already signalled: no wait at all */
    for (unsigned i = 0; i < 6; i++) /* each sleep a little past the look: 20, 40, ... 640 ms, the limit */
        one(e.poll_ns / 1000 + 30000, 0);
    assert(e.poll_ns == 640000000);
    one(1000, 0);
    assert(e.poll_ns == 640000000);
    pt_event_destroy(&e);
    puts("event-poll ok");
    return 0;
}
