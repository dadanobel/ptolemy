/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "web.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int parse(const char *text, PtWebRequest *r) {
    static char b[PT_WEB_HEAD];
    size_t n = strlen(text);
    memcpy(b, text, n);
    return pt_web_parse(b, n, r);
}
int main(void) {
    PtWebRequest r;
    assert(!parse("GET /a?b=1 HTTP/1.1\r\nHost: example.com\r\nuser-agent:  Safari/412\r\n\r\n", &r));
    assert(!strcmp(r.method, "GET") && !strcmp(r.target, "/a?b=1") && !strcmp(r.version, "HTTP/1.1") && r.headers == 2);
    assert(!strcmp(pt_web_header(&r, "HOST"), "example.com") &&
           !strcmp(pt_web_header(&r, "User-Agent"), "Safari/412") && !pt_web_header(&r, "Cookie"));
    assert(!parse("GET http://example.com/ HTTP/1.0\n\n", &r) && !strcmp(r.target, "http://example.com/") &&
           !r.headers);
    assert(parse("GET / HTTP/2\r\n\r\n", &r) == EINVAL && parse("GET /\r\n\r\n", &r) == EINVAL &&
           parse(" / HTTP/1.1\r\n\r\n", &r) == EINVAL && parse("get / HTTP/1.1\r\n\r\n", &r) == EINVAL);
    assert(parse("GET / HTTP/1.1\r\nno colon\r\n\r\n", &r) == EINVAL &&
           parse("GET / HTTP/1.1\r\n: v\r\n\r\n", &r) == EINVAL);
    char many[PT_WEB_HEAD] = "GET / HTTP/1.1\r\n";
    for (unsigned i = 0; i <= PT_WEB_HEADERS; i++)
        strcat(many, "X: y\r\n");
    strcat(many, "\r\n");
    assert(parse(many, &r) == E2BIG);
    char nul[] = "GET / HTTP/1.1\r\nA: b\0c\r\n\r\n";
    assert(pt_web_parse(nul, sizeof nul - 1, &r) == EINVAL);
    uint8_t page[] = "<a href=\"https://a.org/x\">https://https://</a>{\"u\":\"https:\\/\\/b.org\"} https:/";
    size_t n = pt_web_downgrade(page, sizeof page - 1);
    const char *want = "<a href=\"http://a.org/x\">http://http://</a>{\"u\":\"http:\\/\\/b.org\"} https:/";
    assert(n == strlen(want) && !memcmp(page, want, n));
    assert(pt_web_profile("Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; en-us) AppleWebKit/534.59.10 (KHTML, like Gecko) Version/5.1.10 Safari/534.59.10") == PT_WEB_SAFARI5 &&
           pt_web_profile("Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; en) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.1.3 Safari/531.22.7") == PT_WEB_SAFARI4 &&
           pt_web_profile("Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412 (KHTML, like Gecko) Safari/412") == PT_WEB_LEGACY &&
           pt_web_profile(NULL) == PT_WEB_LEGACY);
    static const char *html[][2] = {
        {"<p><IMG class=a src=\"blank.gif\" data-src=\"/real.jpg\" alt=x></p>", "<p><IMG class=a src=\"/real.jpg\" data-src=\"/real.jpg\" alt=x></p>"},
        {"<img data-lazy-src='a.png' width=3>", "<img src=\"a.png\" data-lazy-src='a.png' width=3>"},
        {"<img srcset=\"s.jpg 1x, l.jpg 2x\">", "<img src=\"s.jpg\" srcset=\"s.jpg 1x, l.jpg 2x\">"},
        {"<img src=\"k.jpg\" srcset=\"l.jpg 2x\">", "<img src=\"k.jpg\" srcset=\"l.jpg 2x\">"},
        {"<img src=k.jpg>", "<img src=k.jpg>"},
        {"a<noscript><img src=\"n.jpg\"></noscript>b", "a<img src=\"n.jpg\">b"},
        {"<NOSCRIPT><meta http-equiv=\"refresh\" content=\"0;url=/nojs\"></NOSCRIPT>", "<NOSCRIPT><meta http-equiv=\"refresh\" content=\"0;url=/nojs\"></NOSCRIPT>"},
        {"<script>var s='<img data-src=\"x\">';</script><img data-src=\"y\">", "<script>var s='<img data-src=\"x\">';</script><img src=\"y\" data-src=\"y\">"},
        {"<style>a<b{}</style><noscript>t</noscript>", "<style>a<b{}</style>t"},
        {"<img data-src=\"z\"", "<img data-src=\"z\""},
        {"<script>unterminated <img data-src=q>", "<script>unterminated <img data-src=q>"},
    };
    for (unsigned i = 0; i < sizeof html / sizeof *html; i++) {
        size_t m = 0;
        uint8_t *o = pt_web_html((const uint8_t *)html[i][0], strlen(html[i][0]), PT_WEB_SAFARI5, &m);
        if (!o || m != strlen(html[i][1]) || memcmp(o, html[i][1], m)) {
            printf("html case %u: got \"%.*s\"\n", i, (int)m, o ? (char *)o : "");
            return 1;
        }
        free(o);
    }
    uint32_t seed = 7; /* random heads: never crash, never read past n (ASan) */
    for (unsigned i = 0; i < 20000; i++) {
        size_t len = 1 + i % 300;
        char *b = malloc(len);
        for (size_t j = 0; j < len; j++) {
            seed = seed * 1664525 + 1013904223;
            b[j] = " :\r\nGET/HTTP1.ab"[seed >> 28];
        }
        (void)pt_web_parse(b, len, &r);
        (void)pt_web_downgrade((uint8_t *)b, len);
        for (size_t j = 0; j < len; j++) {
            seed = seed * 1664525 + 1013904223;
            b[j] = "<>imgsrcdata-=\"' noscript/"[seed % 25];
        }
        size_t m;
        free(pt_web_html((const uint8_t *)b, len, (int)(i % 3), &m));
        free(b);
    }
    puts("PASS: web gateway request heads (origin/absolute form, limits, NUL), https:// downgrade, profiles, HTML edits (lazy images, noscript, scripts kept), fuzzed heads and pages");
    return 0;
}
