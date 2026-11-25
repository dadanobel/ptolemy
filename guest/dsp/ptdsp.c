// SPDX-License-Identifier: GPL-2.0-or-later
// Ptolemy DrawSprocket shim: a library in the DrawSprocket games that Ptolemy Agent starts (menu DrawSprocket Games:
// DYLD_INSERT_LIBRARIES in the game's environment, guest/agent/Agent.m). Not in the session's environment: Tiger's
// loginwindow drops DYLD_* from ~/.MacOSX/environment.plist, and in Leopard a library named there and missing makes
// Finder start over and over and stops the Dock (u215).
//
// Tiger's DrawSprocket 2.0.54 (disassembled: ActivateDisplay) captures the display, puts the capture shield at screen
// saver level + 10 and binds the front buffer to it; only DSpSetWindowToFront (OS X only, level + 15) raises an
// application's own window above it. Games built before that call existed (Cro-Mag Rally 1.0.6, 2000) draw into their
// own windows under the shield: a black screen, on a real Mac too. The levels are kept by levels.h; this file is the
// window server's side of it and the way into the process.
//
// In a program without DrawSprocket (a game the user started from the agent by mistake, or a program the game starts)
// it does three things: one access() at start (the switch below), one look at each loaded image's name, and it stands
// between the program and seven window server calls, which it forwards unchanged. It arms itself only when
// DrawSprocket itself is loaded, and acts only once that sets a shield's level.
//
// The switch: ~/.MacOSX/ptdsp.off (made by hand; earlier builds' agent menu made it). With it the library registers
// nothing and forwards everything. PTDSP_DEBUG=1: every call on stderr.
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "levels.h"

static int debug;
static PtDsp dsp;

typedef int CGSConnection;
extern CGSConnection _CGSDefaultConnection(void);
extern CGError CGSSetWindowLevel(CGSConnection, int, int);
extern CGError CGSGetWindowLevel(CGSConnection, int, int *);
extern CGError CGSGetOnScreenWindowList(CGSConnection, CGSConnection, int, int *, int *);
// Orderings: forwarded with all eight argument registers (PowerPC: integer and pointer arguments in r3-r10; none of
// these has floating-point or stack arguments), then the sweep.
typedef long (*Call8)(long, long, long, long, long, long, long, long);
extern long CGSOrderWindow(long, long, long, long, long, long, long, long);
extern long CGSOrderWindowList(long, long, long, long, long, long, long, long);
extern long CGSOrderWindowWithGroup(long, long, long, long, long, long, long, long);
extern long CGSOrderFrontConditionally(long, long, long, long, long, long, long, long);

// The window server for levels.h: the connection the calls go through, and the result of the first level set (the
// program's own call; the ones after it are the shim's).
typedef struct {
    CGSConnection c;
    CGError first;
    int sets;
} Server;
/* Sets a window's level. The first call's result is kept: it answers the program's own call. */
static int server_set(void *o, int w, int level)
{
    Server *s = o;
    CGError e = CGSSetWindowLevel(s->c, w, level);
    if (!s->sets++)
        s->first = e;
    else if (debug)
        fprintf(stderr, "ptdsp: window %d to level %d (%d)\n", w, level, (int)e);
    return e != 0;
}
/* Reads a window's level. */
static int server_get(void *o, int w, int *level)
{
    return CGSGetWindowLevel(((Server *)o)->c, w, level) != 0;
}
/* The on-screen windows of this program's connection. */
static int server_list(void *o, int *list, int max, int *count)
{
    Server *s = o;
    return CGSGetOnScreenWindowList(s->c, s->c, max, list, count) != 0; /* this connection's own windows only */
}
/* Is this window the capture shield of one of the active displays? */
static int server_shield(void *o, int w)
{
    CGDirectDisplayID d[8];
    CGDisplayCount n = 0;
    (void)o;
    if (CGGetActiveDisplayList(8, d, &n))
        return 0;
    for (CGDisplayCount i = 0; i < n; i++)
        if ((int)CGShieldingWindowID(d[i]) == w)
            return 1;
    return 0;
}
#define SERVER(connection)                                                                                             \
    Server state = {connection, 0, 0};                                                                                 \
    const PtDspServer v = {server_set, server_get, server_list, server_shield, &state}

/* CGSSetWindowLevel, as the program sees it: levels.h decides, and the program gets its own call's result. */
static CGError my_set_level(CGSConnection c, int w, int level)
{
    SERVER(c);
    int before = dsp.shield;
    pt_dsp_set_level(&dsp, &v, w, level);
    if (dsp.shield != before)
        fprintf(stderr, "ptdsp: capture shield %d at level %d: this program's windows go above it\n", dsp.shield,
                dsp.shield_level);
    else if (debug)
        fprintf(stderr, "ptdsp: CGSSetWindowLevel %d %d %d (shield %d)\n", c, w, level, dsp.shield);
    return state.first;
}
/* After the display is released: the shim's window levels go back to what they were. */
static void released(void)
{
    SERVER(_CGSDefaultConnection());
    state.sets = 1; /* none of these is the program's own call */
    int n = pt_dsp_released(&dsp, &v);
    if (n)
        fprintf(stderr, "ptdsp: display released, %d window levels given back\n", n);
}
/* CGDisplayRelease, then the levels given back. */
static CGDisplayErr my_release(CGDirectDisplayID d)
{
    CGDisplayErr e = CGDisplayRelease(d);
    released();
    return e;
}
/* CGReleaseAllDisplays, then the levels given back. */
static CGDisplayErr my_release_all(void)
{
    CGDisplayErr e = CGReleaseAllDisplays();
    released();
    return e;
}
#define ORDER(name, original)                                                                                          \
    static long name(long a, long b, long c, long d, long e, long f, long g, long h)                                  \
    {                                                                                                                  \
        long r = ((Call8)original)(a, b, c, d, e, f, g, h);                                                            \
        if (debug)                                                                                                     \
            fprintf(stderr, "ptdsp: " #original " %ld %ld %ld\n", a, b, c);                                         \
        if (dsp.shield) {                                                                                              \
            SERVER(_CGSDefaultConnection());                                                                           \
            state.sets = 1;                                                                                            \
            pt_dsp_ordered(&dsp, &v);                                                                                  \
        }                                                                                                              \
        return r;                                                                                                      \
    }
ORDER(my_order, CGSOrderWindow)
ORDER(my_order_list, CGSOrderWindowList)
ORDER(my_order_group, CGSOrderWindowWithGroup)
ORDER(my_order_front, CGSOrderFrontConditionally)

__attribute__((used)) static const struct {
    const void *replacement, *original;
} interposers[] __attribute__((section("__DATA,__interpose"))) = {
        {(const void *)my_set_level, (const void *)CGSSetWindowLevel},
        {(const void *)my_release, (const void *)CGDisplayRelease},
        {(const void *)my_release_all, (const void *)CGReleaseAllDisplays},
        {(const void *)my_order, (const void *)CGSOrderWindow},
        {(const void *)my_order_list, (const void *)CGSOrderWindowList},
        {(const void *)my_order_group, (const void *)CGSOrderWindowWithGroup},
        {(const void *)my_order_front, (const void *)CGSOrderFrontConditionally},
};

/* dyld interposing does not reach a prebound image whose pointers dyld leaves as they are (Tiger's DrawSprocket and
 * HIToolbox are prebound), so once DrawSprocket is in the process each image's symbol pointers to the calls above are
 * also set here, by name, as dyld would have bound them: lazy and non-lazy pointer sections, the indirect symbol
 * table. */
static const struct {
    const char *name;
    const void *replacement;
} rebind[] = {{"_CGSSetWindowLevel", (const void *)my_set_level},
              {"_CGDisplayRelease", (const void *)my_release},
              {"_CGReleaseAllDisplays", (const void *)my_release_all},
              {"_CGSOrderWindow", (const void *)my_order},
              {"_CGSOrderWindowList", (const void *)my_order_list},
              {"_CGSOrderWindowWithGroup", (const void *)my_order_group},
              {"_CGSOrderFrontConditionally", (const void *)my_order_front}};
static const char *image_name(const struct mach_header *mh) /* the install name, from the image itself */
{
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize))
        if (lc->cmd == LC_ID_DYLIB)
            return (const char *)lc + ((const struct dylib_command *)lc)->dylib.name.offset;
    return "";
}
/* Points one image's lazy and non-lazy symbol pointers for the calls in rebind at the shim's replacements. */
static void point(const struct mach_header *mh, intptr_t slide)
{
    Dl_info self;
    if (dladdr((const void *)point, &self) && self.dli_fbase == (const void *)mh)
        return; /* this library's own pointers stay on the originals */
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    const struct segment_command *linkedit = NULL;
    const struct symtab_command *st = NULL;
    const struct dysymtab_command *dst = NULL;
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd == LC_SEGMENT && !strcmp(((const struct segment_command *)lc)->segname, SEG_LINKEDIT))
            linkedit = (const struct segment_command *)lc;
        else if (lc->cmd == LC_SYMTAB)
            st = (const struct symtab_command *)lc;
        else if (lc->cmd == LC_DYSYMTAB)
            dst = (const struct dysymtab_command *)lc;
    }
    if (mh->magic != MH_MAGIC || !linkedit || !st || !dst || !dst->nindirectsyms)
        return;
    const char *base = (const char *)(slide + linkedit->vmaddr - linkedit->fileoff);
    const struct nlist *sym = (const struct nlist *)(base + st->symoff);
    const char *str = base + st->stroff;
    const uint32_t *ind = (const uint32_t *)(base + dst->indirectsymoff);
    unsigned done = 0;
    lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd != LC_SEGMENT)
            continue;
        const struct segment_command *seg = (const struct segment_command *)lc;
        const struct section *sec = (const struct section *)(seg + 1);
        for (uint32_t k = 0; k < seg->nsects; k++, sec++) {
            uint32_t type = sec->flags & SECTION_TYPE;
            if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS)
                continue;
            const void **ptr = (const void **)(slide + sec->addr);
            for (uint32_t j = 0; j < sec->size / sizeof(void *); j++) {
                uint32_t si = ind[sec->reserved1 + j];
                if (si & (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS) || si >= st->nsyms)
                    continue;
                const char *name = str + sym[si].n_un.n_strx;
                for (unsigned r = 0; r < sizeof rebind / sizeof rebind[0]; r++)
                    if (!strcmp(name, rebind[r].name)) {
                        ptr[j] = rebind[r].replacement;
                        done++;
                    }
            }
        }
    }
    if (debug && done)
        fprintf(stderr, "ptdsp: %u pointers set in %s\n", done, image_name(mh));
}
/* dyld's add-image callback: arms the shim when DrawSprocket arrives, then rebinds every image, those already there
 * and those that come later. */
static void added(const struct mach_header *mh, intptr_t slide)
{
    if (dsp.armed) {
        point(mh, slide);
        return;
    }
    if (!strstr(image_name(mh), "DrawSprocket"))
        return; /* a program without DrawSprocket: nothing more than this look, ever */
    dsp.armed = 1;
    if (debug)
        fprintf(stderr, "ptdsp: DrawSprocket loaded in %d, %u images\n", (int)getpid(), (unsigned)_dyld_image_count());
    point(mh, slide);
    for (uint32_t i = 0; i < _dyld_image_count(); i++) /* those loaded before it (setting a pointer twice is the same) */
        point(_dyld_get_image_header(i), _dyld_get_image_vmaddr_slide(i));
}
__attribute__((constructor)) static void ptdsp(void)
{
    /* DYLD_INSERT_LIBRARIES stays set: LaunchCFMApp runs again in the same process (exec), and the game runs in that
     * second image (u132e: unset here, the shim saw only the first). */
    char off[1024];
    const char *home = getenv("HOME");
    debug = getenv("PTDSP_DEBUG") != NULL;
    if (home && snprintf(off, sizeof off, "%s/.MacOSX/ptdsp.off", home) < (int)sizeof off && !access(off, F_OK)) {
        if (debug)
            fprintf(stderr, "ptdsp: switched off (%s)\n", off);
        return;
    }
    _dyld_register_func_for_add_image(added); /* called for the images already loaded too */
}
