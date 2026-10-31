/* SPDX-License-Identifier: GPL-2.0-or-later
 * The host side of protocol-2 transfers (src/agent.c) driven as the vCPU would drive it, with a real
 * PTA1 socket client: 0 B and multi-chunk files, repeated/gap/straddling chunks, repeated COMMIT, guest hash
 * mismatch, a host file changed mid-transfer, client disconnect (cancel), timeout, a version-1 guest, busy. */
#include "pt_agent.h"
#include "pt_sha256.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static char dir[64], sock[128];
typedef struct { const char *path, *name; unsigned seconds; int hangup; char reply[512]; unsigned kind, status; const char *text; } Client;
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void *client(void *opaque) {
    Client *c = opaque; struct sockaddr_un sa = {.sun_family = AF_UNIX}; strcpy(sa.sun_path, sock);
    int s = socket(AF_UNIX, SOCK_STREAM, 0); assert(s >= 0 && !connect(s, (struct sockaddr *)&sa, sizeof sa));
    uint8_t m[16 + 1024]; size_t len;
    if (c->text) {   /* op 3: clipboard text */
        uint32_t tn = (uint32_t)strlen(c->text); memcpy(m, "PTA1", 4); put32(m + 4, 3); put32(m + 8, tn); put32(m + 12, c->seconds);
        memcpy(m + 16, c->text, tn); len = 16 + tn;
    } else {
        uint32_t pn = (uint32_t)strlen(c->path), nn = (uint32_t)strlen(c->name);
        memcpy(m, "PTA1", 4); put32(m + 4, 2); put32(m + 8, 8 + pn + nn); put32(m + 12, c->seconds);
        put32(m + 16, pn); memcpy(m + 20, c->path, pn); put32(m + 20 + pn, nn); memcpy(m + 24 + pn, c->name, nn); len = 24 + pn + nn;
    }
    assert(write(s, m, len) == (ssize_t)len);
    if (c->hangup) { usleep(200000); close(s); return NULL; }
    uint8_t h[16]; assert(recv(s, h, 16, MSG_WAITALL) == 16 && !memcmp(h, "PTA1", 4));
    uint32_t n = get32(h + 12); c->kind = get32(h + 4); c->status = get32(h + 8); assert(n < sizeof c->reply);
    assert(recv(s, c->reply, n, MSG_WAITALL) == (ssize_t)n); c->reply[n] = 0; close(s); return NULL;
}
static void file(const char *path, const uint8_t *b, size_t n) { FILE *f = fopen(path, "wb"); assert(f && fwrite(b, 1, n, f) == n && !fclose(f)); }
static void sha(const uint8_t *b, size_t n, uint8_t out[32]) { PtSha256 h; pt_sha256_init(&h); pt_sha256_update(&h, b, n); pt_sha256_final(&h, out); }
static uint32_t begin(PtAgent *a, uint32_t t, uint8_t head[PTA_XHEAD]) {
    uint32_t id = 0; for (int i = 0; i < 200 && pt_agent_xbegin(a, t, head, &id) == PTA_AGAIN; i++) usleep(10000);
    assert(id); return id;
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    strcpy(dir, "/tmp/pt-agent-XXXXXX"); assert(mkdtemp(dir)); chmod(dir, 0700); snprintf(sock, sizeof sock, "%s/s", dir);
    PtAgent *a = pt_agent_open(sock); assert(a);
    uint32_t t; assert(pt_agent_register(a, PTA_VERSION, &t) == PTA_OK);
    static uint8_t data[200000], out[PTA_CHUNK], head[PTA_XHEAD], want[32], zero[32];
    for (unsigned i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 2654435761u >> 13);
    char big[96], empty[96]; snprintf(big, sizeof big, "%s/big", dir); snprintf(empty, sizeof empty, "%s/empty", dir);
    file(big, data, sizeof data); file(empty, data, 0);
    uint32_t n, id;
    /* 0 bytes */
    Client c0 = {empty, "empty.txt", 30, 0, "", 0, 0, NULL}; pthread_t th; pthread_create(&th, NULL, client, &c0);
    id = begin(a, t, head); assert(get32(head) == 0 && get32(head + 4) == 0 && get32(head + 40) == PTA_XFILE && get32(head + 44) == 9 && !memcmp(head + 48, "empty.txt", 9));
    sha(data, 0, want); assert(!memcmp(head + 8, want, 32));
    assert(pt_agent_xchunk(a, t, id, 0, out, &n) == PTA_OK && n == 0);
    assert(pt_agent_xcommit(a, t, id, want) == PTA_OK && pt_agent_xcommit(a, t, id, want) == PTA_OK);   /* repeatable */
    assert(pt_agent_xdone(a, t, id, 0, "/Users/u/Desktop/empty.txt") == PTA_OK);
    pthread_join(th, NULL); assert(c0.kind == 0 && c0.status == 0 && !strcmp(c0.reply, "published /Users/u/Desktop/empty.txt\n"));
    /* 200000 bytes: repeat, gap, straddle, then in order; a wrong guest hash is refused for good */
    Client c1 = {big, "big.bin", 30, 0, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c1);
    id = begin(a, t, head); sha(data, sizeof data, want); assert(get32(head + 4) == sizeof data && !memcmp(head + 8, want, 32));
    assert(pt_agent_xchunk(a, t, id, 0, out, &n) == PTA_OK && n == PTA_CHUNK && !memcmp(out, data, n));
    assert(pt_agent_xchunk(a, t, id, 0, out, &n) == PTA_OK && n == PTA_CHUNK);                    /* repeat: no effect */
    assert(pt_agent_xchunk(a, t, id, 2 * PTA_CHUNK, out, &n) == PTA_INVALID);                      /* gap */
    assert(pt_agent_xchunk(a, t, id, PTA_CHUNK - 10, out, &n) == PTA_INVALID);                     /* across the end */
    assert(pt_agent_xchunk(a, t, id + 1, PTA_CHUNK, out, &n) == PTA_DENIED);                       /* another id */
    for (uint32_t off = PTA_CHUNK; off < sizeof data; off += n) assert(pt_agent_xchunk(a, t, id, off, out, &n) == PTA_OK && !memcmp(out, data + off, n));
    assert(pt_agent_xcommit(a, t, id, zero) == PTA_INVALID && pt_agent_xcommit(a, t, id, want) == PTA_INVALID);
    assert(pt_agent_xdone(a, t, id, 5, "") == PTA_OK);
    pthread_join(th, NULL); assert(c1.status == 1 && strstr(c1.reply, "SHA-256 mismatch"));
    /* the same file in order: published; the transfer slot is free again */
    Client c2 = {big, "big.bin", 30, 0, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c2); id = begin(a, t, head);
    for (uint32_t off = 0; off < sizeof data; off += n) assert(pt_agent_xchunk(a, t, id, off, out, &n) == PTA_OK);
    assert(pt_agent_xcommit(a, t, id, want) == PTA_OK && pt_agent_xdone(a, t, id, 0, "/D/big.bin") == PTA_OK);
    pthread_join(th, NULL); assert(c2.status == 0 && !strcmp(c2.reply, "published /D/big.bin\n"));
    /* the host file changes after BEGIN: COMMIT refuses */
    Client c3 = {big, "big.bin", 30, 0, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c3); id = begin(a, t, head);
    data[150000] ^= 1; file(big, data, sizeof data);
    for (uint32_t off = 0; off < sizeof data; off += n) assert(pt_agent_xchunk(a, t, id, off, out, &n) == PTA_OK);
    assert(pt_agent_xcommit(a, t, id, want) == PTA_INVALID && pt_agent_xdone(a, t, id, 7, "") == PTA_OK);
    pthread_join(th, NULL); assert(c3.status == 1 && strstr(c3.reply, "changed during the transfer"));
    /* the client disconnects: the guest sees CANCELLED, reports DONE, the slot frees */
    Client c4 = {big, "big.bin", 30, 1, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c4); id = begin(a, t, head);
    pthread_join(th, NULL); usleep(100000);
    assert(pt_agent_xchunk(a, t, id, 0, out, &n) == PTA_CANCELLED && pt_agent_xcommit(a, t, id, want) == PTA_CANCELLED);
    assert(pt_agent_xdone(a, t, id, 1, "") == PTA_OK);
    /* clipboard text (op 3): Cyrillic and a character outside the BMP, as UTF-8, from memory */
    static const char text[] = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xf0\x9f\x98\x80";
    Client cc = {"", "", 30, 0, "", 0, 0, text}; pthread_create(&th, NULL, client, &cc); id = begin(a, t, head);
    sha((const uint8_t *)text, strlen(text), want);
    assert(get32(head + 4) == strlen(text) && get32(head + 40) == PTA_XCLIPBOARD && !memcmp(head + 8, want, 32));
    assert(pt_agent_xchunk(a, t, id, 0, out, &n) == PTA_OK && n == strlen(text) && !memcmp(out, text, n));
    assert(pt_agent_xcommit(a, t, id, want) == PTA_OK && pt_agent_xdone(a, t, id, 0, "clipboard") == PTA_OK);
    pthread_join(th, NULL); assert(cc.status == 0 && !strcmp(cc.reply, "clipboard set\n"));
    sha(data, sizeof data, want);
    /* a timeout before the guest starts: answered by the host alone */
    Client c5 = {big, "big.bin", 1, 0, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c5);
    for (int i = 0; i < 25; i++) { usleep(100000); uint32_t k; pt_agent_check(a, t, 0, &k); }   /* heartbeat */
    pthread_join(th, NULL); assert(c5.status == 1 && strstr(c5.reply, "timed out"));
    /* busy, then a version-1 guest */
    Client c6 = {big, "a.bin", 30, 0, "", 0, 0, NULL}, c7 = {big, "b.bin", 30, 0, "", 0, 0, NULL};
    pthread_create(&th, NULL, client, &c6); id = begin(a, t, head);
    pthread_t th2; pthread_create(&th2, NULL, client, &c7); pthread_join(th2, NULL); assert(c7.kind == 2 && strstr(c7.reply, "another command"));
    assert(pt_agent_xdone(a, t, id, 9, "") == PTA_OK); pthread_join(th, NULL); assert(c6.status == 1);
    assert(pt_agent_unregister(a, t) == PTA_OK && pt_agent_register(a, 0, &t) == PTA_OK);
    Client c8 = {big, "c.bin", 30, 0, "", 0, 0, NULL}; pthread_create(&th, NULL, client, &c8); pthread_join(th, NULL);
    assert(c8.kind == 2 && strstr(c8.reply, "cannot receive files"));
    Client c8t = {"", "", 30, 0, "", 0, 0, "x"}; pthread_create(&th, NULL, client, &c8t); pthread_join(th, NULL); assert(c8t.kind == 2);
    Client c9 = {"relative", "x", 30, 0, "", 0, 0, NULL}; assert(pt_agent_unregister(a, t) == PTA_OK && pt_agent_register(a, 2, &t) == PTA_OK);
    pthread_create(&th, NULL, client, &c9); pthread_join(th, NULL); assert(c9.kind == 2 && strstr(c9.reply, "invalid transfer"));
    pt_agent_close(a); unlink(big); unlink(empty); rmdir(dir);
    puts("PASS: agent transfers: 0 B / 3-chunk files, repeat/gap/straddle chunks, repeated COMMIT, guest hash mismatch, "
         "host file changed, cancel by disconnect, clipboard text, timeout, busy, version-1 guest, bad path");
    return 0;
}
