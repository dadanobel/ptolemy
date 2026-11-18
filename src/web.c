/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The portable half of the web gateway (web_macos.m): request-head parsing of untrusted guest bytes and
 * the https:// -> http:// body edit. */
#include "web.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static char *line(char **at, char *end) { /* next line, NUL-terminated without its CR LF; NULL at the end */
    char *s = *at, *nl = s < end ? memchr(s, '\n', (size_t)(end - s)) : NULL;
    if (!nl)
        return NULL;
    *at = nl + 1;
    if (nl > s && nl[-1] == '\r')
        nl--;
    *nl = 0;
    return s;
}
/* Parses the request line and headers of an HTTP/1.x request from an old browser in the guest. */
int pt_web_parse(char *head, size_t n, PtWebRequest *r) {
    memset(r, 0, sizeof *r);
    if (!head || !n || n > PT_WEB_HEAD || memchr(head, 0, n))
        return EINVAL;
    char *at = head, *end = head + n, *l = line(&at, end);
    if (!l)
        return EINVAL;
    r->method = l;
    char *sp = strchr(l, ' ');
    if (!sp)
        return EINVAL;
    *sp = 0;
    r->target = sp + 1;
    sp = strchr(r->target, ' ');
    if (!sp)
        return EINVAL;
    *sp = 0;
    r->version = sp + 1;
    if (!*r->method || !*r->target || strncmp(r->version, "HTTP/1.", 7))
        return EINVAL;
    for (const char *m = r->method; *m; m++)
        if (*m < 'A' || *m > 'Z')
            return EINVAL; /* GET, POST, HEAD, CONNECT... */
    for (char *h; (h = line(&at, end)) && *h;) {
        char *colon = strchr(h, ':');
        if (!colon || colon == h)
            return EINVAL;
        if (r->headers == PT_WEB_HEADERS)
            return E2BIG;
        *colon = 0;
        char *v = colon + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        r->name[r->headers] = h;
        r->value[r->headers++] = v;
    }
    return 0;
}
const char *pt_web_header(const PtWebRequest *r, const char *name) {
    for (unsigned i = 0; i < r->headers; i++)
        if (!strcasecmp(r->name[i], name))
            return r->value[i];
    return NULL;
}
size_t pt_web_downgrade(uint8_t *b, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        if (n - i >= 8 && !memcmp(b + i, "https://", 8)) {
            memcpy(b + o, "http://", 7);
            o += 7;
            i += 8;
        } else if (n - i >= 10 && !memcmp(b + i, "https:\\/\\/", 10)) {
            memcpy(b + o, "http:\\/\\/", 9);
            o += 9;
            i += 10;
        } else
            b[o++] = b[i++];
    }
    return o;
}
/* The guest browser's profile: Safari 5 (Leopard's, after its updates), Safari 4 (Tiger's) or anything older. */
int pt_web_profile(const char *ua) {
    if (ua && strstr(ua, "Version/5."))
        return PT_WEB_SAFARI5;
    if (ua && strstr(ua, "Version/4."))
        return PT_WEB_SAFARI4;
    return PT_WEB_LEGACY;
}
static const uint8_t *find_ci(const uint8_t *p, const uint8_t *end, const char *s) { /* case-insensitive, ASCII */
    size_t k = strlen(s);
    for (; (size_t)(end - p) >= k; p++)
        if (!strncasecmp((const char *)p, s, k))
            return p;
    return NULL;
}
typedef struct {
    uint8_t *b;
    size_t n, cap;
} Out;
static int put(Out *o, const void *p, size_t k) {
    if (o->n + k > o->cap) {
        size_t c = o->cap * 2 + k;
        uint8_t *b = realloc(o->b, c);
        if (!b)
            return -1;
        o->b = b;
        o->cap = c;
    }
    memcpy(o->b + o->n, p, k);
    o->n += k;
    return 0;
}
/* One attribute of a tag [p, end): name and value spans; returns the position after it, or NULL at the tag's end. */
static const uint8_t *attribute(const uint8_t *p, const uint8_t *end, const uint8_t **name, size_t *nlen,
                                const uint8_t **value, size_t *vlen) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '/'))
        p++;
    if (p >= end)
        return NULL;
    *name = p;
    while (p < end && *p != '=' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '/')
        p++;
    *nlen = (size_t)(p - *name);
    *value = NULL;
    *vlen = 0;
    if (p < end && *p == '=') {
        p++;
        if (p < end && (*p == '"' || *p == '\'')) {
            uint8_t q = *p++;
            *value = p;
            while (p < end && *p != q)
                p++;
            *vlen = (size_t)(p - *value);
            if (p < end)
                p++;
        } else {
            *value = p;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                p++;
            *vlen = (size_t)(p - *value);
        }
    }
    return p;
}
static int named(const uint8_t *n, size_t k, const char *s) {
    return k == strlen(s) && !strncasecmp((const char *)n, s, k);
}
/* <img> whose real address a script would set (data-src, data-lazy-src, data-original, or only srcset): src gets it. */
static int image(Out *o, const uint8_t *tag, const uint8_t *end) { /* tag: "<img", end: its '>' */
    const uint8_t *p = tag + 4, *name, *value, *src = NULL, *lazy = NULL, *set = NULL;
    size_t nlen, vlen, srclen = 0, lazylen = 0, setlen = 0;
    while ((p = attribute(p, end, &name, &nlen, &value, &vlen)))
        if (value && named(name, nlen, "src"))
            src = value, srclen = vlen;
        else if (value && vlen && !lazy &&
                 (named(name, nlen, "data-src") || named(name, nlen, "data-lazy-src") || named(name, nlen, "data-original")))
            lazy = value, lazylen = vlen;
        else if (value && vlen && named(name, nlen, "srcset"))
            set = value, setlen = vlen;
    if (!lazy && set && !src) { /* the first candidate of srcset: "a.jpg 1x, b.jpg 2x" */
        lazy = set;
        while (lazylen < setlen && set[lazylen] != ' ' && set[lazylen] != ',')
            lazylen++;
    }
    if (!lazy)
        return put(o, tag, (size_t)(end - tag));
    if (src) /* the placeholder's value replaced */
        return put(o, tag, (size_t)(src - tag)) || put(o, lazy, lazylen) ||
               put(o, src + srclen, (size_t)(end - src - srclen));
    return put(o, "<img src=\"", 10) || put(o, lazy, lazylen) || put(o, "\"", 1) || put(o, tag + 4, (size_t)(end - tag - 4));
}
uint8_t *pt_web_html(const uint8_t *in, size_t n, int profile, size_t *out) {
    (void)profile; /* the same edits for every profile so far: their scripts do not run on any of them */
    Out o = {malloc(n + 64), 0, n + 64};
    const uint8_t *p = in, *end = in + n;
    if (!o.b)
        return NULL;
    while (p < end) {
        const uint8_t *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) {
            if (put(&o, p, (size_t)(end - p)))
                goto fail;
            break;
        }
        if (put(&o, p, (size_t)(lt - p)))
            goto fail;
        const uint8_t *gt = memchr(lt, '>', (size_t)(end - lt)); /* ponytail: a '>' inside a quoted value ends the tag early; only src is moved, so the worst case is an unedited image */
        if (!gt) {
            if (put(&o, lt, (size_t)(end - lt)))
                goto fail;
            break;
        }
        size_t k = (size_t)(gt - lt);
        if (k >= 4 && !strncasecmp((const char *)lt, "<img", 4) && (lt[4] == ' ' || lt[4] == '\t' || lt[4] == '\n' || lt[4] == '\r')) {
            if (image(&o, lt, gt))
                goto fail;
            p = gt;
            continue;
        }
        if ((k >= 7 && !strncasecmp((const char *)lt, "<script", 7)) || (k >= 6 && !strncasecmp((const char *)lt, "<style", 6))) {
            const uint8_t *close = find_ci(gt, end, lt[1] == 's' || lt[1] == 'S' ? (lt[2] == 'c' || lt[2] == 'C' ? "</script" : "</style") : "</script");
            const uint8_t *stop = close ? close : end; /* scripts and styles pass unedited (markup in their strings stays) */
            if (put(&o, lt, (size_t)(stop - lt)))
                goto fail;
            p = stop;
            if (!close)
                break;
            const uint8_t *cgt = memchr(close, '>', (size_t)(end - close));
            if (put(&o, close, (size_t)((cgt ? cgt + 1 : end) - close)))
                goto fail;
            p = cgt ? cgt + 1 : end;
            continue;
        }
        if (k == 9 && !strncasecmp((const char *)lt, "<noscript", 9)) { /* unwrapped, unless it redirects ("enable JavaScript" pages) */
            const uint8_t *close = find_ci(gt, end, "</noscript>");
            const uint8_t *refresh = close ? find_ci(gt, close, "http-equiv") : NULL;
            if (close && !refresh) {
                p = gt + 1;
                continue;
            }
            if (close) { /* kept whole, with its closing tag */
                const uint8_t *cgt = memchr(close, '>', (size_t)(end - close));
                const uint8_t *stop = cgt ? cgt + 1 : end;
                if (put(&o, lt, (size_t)(stop - lt)))
                    goto fail;
                p = stop;
                continue;
            }
        }
        if (k == 10 && !strncasecmp((const char *)lt, "</noscript", 10)) {
            p = gt + 1;
            continue;
        }
        if (put(&o, lt, k))
            goto fail;
        p = gt;
    }
    *out = o.n;
    return o.b;
fail:
    free(o.b);
    return NULL;
}
