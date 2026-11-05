/* SPDX-License-Identifier: GPL-2.0-or-later
 * Standalone firmware/device integration harness. Optional disks are read-only. */
#include "platform.h"
#include "pt_cpu_profile.h"
#include "devices.h"
#include "nvram.h"
#include "board.h"
#include "pci_address.h"
#include "radeon.h"
#include "stand_window.h"
#include "ppc.h"
#include "pt_agent.h"
#include "net.h"
#include "net_user.h"
#include "web.h"
#include "pointer_mailbox.h"
#include "optics/frontend.h"
#include "pt_gpu_memory.h"
#include "pt_boot_mode.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <mach/mach_time.h>
#include <pthread/qos.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <spawn.h>
#include <strings.h>
#include <sys/wait.h>
extern char **environ;

CPU *pt_hv_init(int);
void pt_hv_map(void *, uint32_t, uint64_t, int);
void pt_hv_stats(void);
void pt_hv_unmap(uint32_t, uint64_t);
int pt_hv_protect(uint64_t, uint64_t, unsigned);
uint64_t pt_hv_cntvct(void);
void pt_hv_vtimer_shift(uint64_t ns);
static PtClock clock_model;
static PtPic pic;
static PtFwCfg fw;
static PtEvent wake;
static PtEvent alarm_event;
static PtBus bus;
static uint64_t guest_ram_bytes = 2ull << 30; /* 2 GiB, the PowerBook5,8 maximum; --ram-mb sets another size */
static PtAgp agp;
static PtDevices peripherals;
/* Media change (the window's Media menu and drag and drop). A .iso/.cdr/.toast
 * goes to the CD/DVD drive as it is; any other image is attached by hdiutil without mounting and Tiger gets the raw device as
 * a read-only USB disk; a .zip is first unpacked into a fresh read-only HFS+ image. Host work (hdiutil, opening) runs on the
 * caller's thread; the CPU owner only swaps the prepared backend, and the old one is closed and detached on its own thread. */
typedef struct {
    PtBlock *block;
    char device[32], folder[PATH_MAX];
} PtMedia; /* device: hdiutil attachment; folder: zip scratch */
static _Atomic(PtMedia *) media_request[2]; /* [0] CD/DVD, [1] USB disk; a request without a block ejects */
static PtMedia *media_now[2]; /* CPU owner */
static int run_tool(const char *const argv[], char *out, size_t n) {
    char tmp[] = "/tmp/ptolemy-media-XXXXXX";
    int fd = out ? mkstemp(tmp) : -1, status = 0;
    pid_t pid;
    posix_spawn_file_actions_t a;
    if (out && fd < 0)
        return errno;
    if (fd >= 0)
        unlink(tmp);
    posix_spawn_file_actions_init(&a);
    if (fd >= 0)
        posix_spawn_file_actions_adddup2(&a, fd, 1);
    int e = posix_spawnp(&pid, argv[0], &a, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&a);
    if (!e && waitpid(pid, &status, 0) < 0)
        e = errno;
    if (!e && (!WIFEXITED(status) || WEXITSTATUS(status)))
        e = EIO;
    if (fd >= 0) {
        if (!e) {
            ssize_t k = pread(fd, out, n - 1, 0);
            out[k > 0 ? k : 0] = 0;
        }
        close(fd);
    }
    return e;
}
static void media_release(PtMedia *m) { /* closing waits for in-flight reads: never on the CPU owner */
    if (!m)
        return;
    if (m->block)
        pt_block_close(m->block);
    if (m->device[0]) {
        const char *argv[] = {"hdiutil", "detach", "-quiet", m->device, NULL};
        for (int k = 0; k < 5 && run_tool(argv, NULL, 0); k++)
            sleep(1);
    }
    if (m->folder[0]) {
        const char *argv[] = {"rm", "-rf", m->folder, NULL};
        (void)run_tool(argv, NULL, 0);
    }
    free(m);
}
static void *media_release_thread(void *m) {
    media_release(m);
    return NULL;
}
static void media_release_later(PtMedia *m) {
    pthread_t t;
    if (m && !pthread_create(&t, NULL, media_release_thread, m))
        pthread_detach(t);
    else
        media_release(m);
}
/* The window's Media menu (and tests): inserts a disk image into a removable slot or ejects it; images that need it
 * are attached with hdiutil first. */
static int media_change(const char *path, int slot) {
    PtMedia *m = calloc(1, sizeof *m);
    if (!m)
        return ENOMEM;
    int e = 0;
    if (path) {
        const char *dot = strrchr(path, '.');
        char raw[PATH_MAX];
        snprintf(raw, sizeof raw, "%s", path);
        struct stat
                st; /* an .iso/.cdr that is not whole 2048-byte sectors (hdiutil's UDTO .cdr) is attached as a disk */
        slot = dot && (!strcasecmp(dot, ".iso") || !strcasecmp(dot, ".cdr") || !strcasecmp(dot, ".toast")) &&
                               !stat(path, &st) && !(st.st_size % 2048)
                       ? 0
                       : 1;
        if (!slot && peripherals.optical.locked && peripherals.optical.present && peripherals.optical.block) {
            free(m);
            return EPERM;
        } /* as pt_devices_optical */
        if (dot && !strcasecmp(dot, ".zip")) {
            const char *tmpdir = getenv("TMPDIR");
            snprintf(m->folder, sizeof m->folder, "%s/ptolemy-zip-XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
            if (!mkdtemp(m->folder)) {
                e = errno;
                m->folder[0] = 0;
            } else {
                char files[PATH_MAX + 16], image[PATH_MAX + 16];
                snprintf(files, sizeof files, "%s/files", m->folder);
                snprintf(image, sizeof image, "%s/image.dmg", m->folder);
                char vol[256];
                const char *b = strrchr(path, '/');
                snprintf(vol, sizeof vol, "%s", b ? b + 1 : path);
                char *x = strrchr(vol, '.');
                if (x && x != vol)
                    *x = 0; /* the disk's name: the zip's */
                const char *unzip[] = {"ditto", "-x", "-k", path, files, NULL},
                           *make[] = {"hdiutil", "create", "-quiet",  "-srcfolder", files, "-volname", vol,
                                      "-fs",     "HFS+",   "-format", "UDRO",       image, NULL};
                /* A zip made without Unix modes (the owner's Cinebench R10) unpacks an application's executable without x,
                 * and Tiger or Leopard then refuses to open it: an app bundle's Contents/MacOS files become executable. */
                const char *modes[] = {"find",  files, "-path", "*.app/Contents/MacOS/*",
                                       "-type", "f",   "-exec", "chmod",
                                       "a+x",   "{}",  "+",     NULL};
                if (!(e = run_tool(unzip, NULL, 0)) && !(e = run_tool(modes, NULL, 0)) &&
                    !(e = run_tool(make, NULL, 0)))
                    snprintf(raw, sizeof raw, "%s", image);
            }
        }
        if (!e && slot == 1) {
            char out[4096];
            const char *attach[] = {"hdiutil",   "attach",      "-nomount", "-readonly",
                                    "-noverify", "-noautofsck", raw,        NULL};
            if (!(e = run_tool(attach, out, sizeof out))) {
                char *d = strstr(out, "/dev/disk");
                size_t k = d ? strcspn(d, " \t\n") : 0;
                if (!d || k >= sizeof m->device)
                    e = EIO;
                else {
                    memcpy(m->device, d, k);
                    m->device[k] = 0;
                    snprintf(raw, sizeof raw, "/dev/r%s", m->device + 5);
                }
            }
        }
        if (!e && !(m->block = slot ? pt_block_open(raw, 0, &wake) : pt_block_open_optical(raw, &wake)))
            e = errno ? errno : EIO;
        if (e) {
            media_release(m);
            return e;
        }
    } else if (slot < 0 || slot > 1) {
        free(m);
        return EINVAL;
    } else if (!slot && peripherals.optical.locked && peripherals.optical.present && peripherals.optical.block) {
        free(m);
        return EPERM;
    }
    PtMedia *none = NULL;
    if (!atomic_compare_exchange_strong(&media_request[slot], &none, m)) {
        media_release(m);
        return EBUSY;
    }
    fprintf(stderr, "media: %s %s\n", path ? path : "eject", slot ? "(USB disk)" : "(CD/DVD)");
    return 0;
}
static void media_apply(CPU *c) { /* CPU owner, each service turn */
    for (int slot = 0; slot < 2; slot++) {
        PtMedia *m = atomic_load(&media_request[slot]);
        if (!m)
            continue;
        if (m->block)
            pt_block_set_kick(m->block, &c->irq);
        PtBlock *old = NULL;
        int e = slot ? pt_devices_storage(&peripherals, m->block, 1, &old)
                     : pt_devices_optical(&peripherals, m->block, &old);
        if (e == EBUSY)
            continue;
        atomic_store(&media_request[slot], NULL);
        if (e) {
            fprintf(stderr, "media: refused (%s)\n", strerror(e));
            media_release_later(m);
            continue;
        }
        PtMedia *gone = media_now[slot];
        if (!gone && old && (gone = calloc(1, sizeof *gone)))
            gone->block = old;
        else if (!gone && old)
            pt_block_close(old);
        media_now[slot] = m->block ? m : NULL;
        if (!m->block)
            free(m);
        media_release_later(gone);
    }
}
static int devices_enabled;
/* PCI memory BARs on the PCI bridge: macio, OHCI; slot 14 (Radeon VRAM, MMIO, I/O) is set by add_radeon_regions. */
typedef struct {
    unsigned slot, reg;
    uint32_t size, flags, base;
    int probe;
    unsigned region;
} PtBar;
static PtBar bars[7] = {{12, 0x10, 0x80000, 0, 0x80000000u, 0, 0},
                        {13, 0x10, 0x100, 0, 0x80080000u, 0, 0},
                        {14, 0x10, 16u << 20, 8, 0x81000000u, 0, 0},
                        {14, 0x18, 0x1000, 0, 0x80081000u, 0, 0}};
static unsigned nbars = 2;
static int cpu970;
#ifdef PT_ENABLE_970
#include "g5_bridge.h"
static PtG5Bridge g5_bridge;
#endif
static PtNetRtl network;
static PtNetHost *network_host;
static PtUserNet *network_user;
static int network_user_enabled;
static int network_enabled, network_shared;
static const char *network_socket;
static int network_access(void *, uint32_t, uint8_t *, unsigned, int);
static void network_irq(void *opaque, int level) {
    (void)opaque;
    pt_pic_line(&pic, 30, 1, level);
}
static int radeon_fast;
static int radeon_enabled;
static PtRadeon *radeon;
/* Every device access to guest RAM passes the GPU's coherence owner first (a page the GPU still owns makes the device
 * retry later). A write drops the CPU reservation: it is kept as an EA, so conservatively; a spurious stwcx. failure is
 * architecturally allowed. Code coherence stays with the guest's icbi, as on hardware DMA. */
static int ram_before(void *opaque, uint32_t at, size_t n, int write) {
    if (radeon) {
        int e = optics_frontend_ram_before(radeon, at, n, write);
        if (e)
            return e;
    } /* locks against the CP thread */
    if (write) {
        CPU *c = opaque;
        c->reserve = 0;
#ifdef PT_ENABLE_970
        if (c->is970 && ppc970_owner_write(c->bus970, at, n))
            return EIO;
#endif
    }
    return 0;
}
static PtRamHooks ram_hooks = {ram_before, NULL};
static uint64_t gpu_at = UINT64_MAX;
static void radeon_configure(void);
static PtGpuMemory radeon_memory = {128u << 20, 256u << 20, 0x90000000u};
/* End of the PCI memory window the ROM advertises in "ranges" (own OpenBIOS fork: 1.25 GiB from 0x80000000); the
 * Radeon BARs must lie inside it, also the register BAR at 0xc0000000 with 256 MiB VRAM. */
static const uint64_t pci_window_end = 0xd0000000ull;
static size_t radeon_mapped_bytes;
static int (*vram_fault)(void *, uint64_t, uint64_t, int, uint64_t, uint32_t);
static void *vram_fault_device; /* guarded VRAM route */
static void radeon_direct_map(void *opaque, uint8_t *ptr, size_t n, int enable);
static uint32_t vram_mapped = 0;
static int vram_remap;
static const char *shot_dir;
static uint64_t shot_at = UINT64_MAX, shot_period;
static unsigned shot_n;
static PtNvram nvram;
static uint64_t nvram_at = UINT64_MAX;
static int nvram_error;
static uint8_t config[32][256], cfgmask[32][256];
static uint32_t cfg_index[3];
static uint8_t scc_reg[2], scc_wr[2][16];
static _Atomic int stop_timer;
static _Atomic uint64_t alarm_ns;
static uint64_t end_ns;
static FILE *trace;
/* PT_TRACE_RANGE=lo-hi[,lo-hi...] (hex, up to 4) keeps only those physical addresses;
 * PT_TRACE_PC_BELOW=hex drops accesses made from guest PCs at or above it (firmware). */
static unsigned trace_range[8] = {0, 0xffffffffu}, trace_ranges = 1, trace_pc_below = 0xffffffffu;
static int trace_keep(unsigned a, unsigned pc) {
    if (pc >= trace_pc_below)
        return 0;
    for (unsigned k = 0; k < trace_ranges; k++)
        if (a >= trace_range[2 * k] && a < trace_range[2 * k + 1])
            return 1;
    return 0;
}
static uint64_t seq;
static uint64_t trace_written;
enum {
    TAIL = 200000
};
static int tail;
static struct {
    uint64_t seq, ns, tb, val;
    uint32_t pc, pa;
    uint8_t n, wr, known;
} ring[TAIL];
static int rx = -1;
static _Atomic int stand_stop, pause_request;
static int window_mode;
static PtVtime vtime; /* the machine's virtual uptime */
/* PT_AUDIO_STATS=1: where the CPU owner's service gaps come from (longest MMIO and hypercall, ticker lateness) */
static int audio_stats = -1;
static uint64_t mmio_max, hyper_max;
static uint32_t mmio_max_pa;
static _Atomic uint64_t alarm_late;
static uint64_t host_ns(void);
static PtCursorMailbox pointer_mailbox;
static PtAgent *agent;
static const char *agent_socket;
static int agent_default;
/* The display NDRV's hypercall: hardware cursor (guest/ndrv/display.c), supervisor only: r0 'PTCU', r3 1 = image
 * (r4 address, r5 width, r6 height; 0 x 0 none), 2 = position (r4 x, r5 y, r6 visible). r3 = 0 when taken. */
#include "display_ndrv.inc"
static PtHwCursor hw_cursor = {.lock = PTHREAD_MUTEX_INITIALIZER};
static int guest_words(CPU *c, uint32_t a, uint32_t *out, uint32_t n) { /* n bytes of big-endian words; 0 when read */
    for (uint32_t i = 0, k; i < n; i += k) { /* page by page, as the kernel maps it */
        uint32_t ra = a + i;
        int wr;
        const uint8_t *p;
        k = 4096 - ((a + i) & 4095);
        if (k > n - i)
            k = n - i;
        if (((c->msr & MSR_DR) && pt_xlate(c, a + i, 0, 0, &ra, &wr)) || !(p = pmem(c, ra, k)))
            return 1;
        for (uint32_t j = 0; j < k; j += 4)
            out[(i + j) / 4] = be32(p + j);
    }
    return 0;
}
/* Also 3 = display mode (r4 width, r5 height, r6 depth 8/16/32; returns the framebuffer's address, 0 =
 * refused), 4 = colour table (r4 address of 0x00RRGGBB words, r5 first entry, r6 count) and 5 = output gamma (r4 address
 * of 256 0x00RRGGBB words, the output level of each input level; r6 256). */
static unsigned gamma_curves; /* ramps that are not the identity (fades, calibration), reported at exit */
static uint64_t now_ns(void);
/* Hypercall from the guest's cursor code (Ptolemy's NDRV): passes the hardware cursor image and position to the
 * window. */
static int cursor_call(CPU *c, uint32_t instruction) {
    if (instruction != 0x00005043u || c->gpr[0] != 0x50544355u)
        return 0;
    uint32_t op = c->gpr[3], a = c->gpr[4], w = c->gpr[5], h = c->gpr[6], n = w * h * 4;
    static uint32_t image[64 * 64];
    c->gpr[3] = 1;
    if (c->msr & MSR_PR)
        return 1;
    if (op == 2) {
        atomic_store(&hw_cursor.pos, (uint64_t)(uint16_t)a | (uint64_t)(uint16_t)w << 16 | (uint64_t)(h != 0) << 32);
        c->gpr[3] = 0;
        static int log = -1;
        static unsigned last = 9;
        if (log < 0)
            log = getenv("PT_CURSOR_LOG") != NULL; /* development: the hardware cursor's visibility */
        if (log && (h != 0) != last) {
            last = h != 0;
            fprintf(stderr, "cursor %.3f s: hardware cursor %s\n", (double)now_ns() / 1e9, last ? "shown" : "hidden");
        }
        return 1;
    }
    if (op == 5) {
        uint32_t t[256];
        if (h == 256 && !(a & 3) && !guest_words(c, a, t, sizeof t)) {
            int linear = 1;
            for (uint32_t i = 0; i < 256; i++)
                linear &= t[i] == i * 0x010101u;
            pthread_mutex_lock(&hw_cursor.lock);
            memcpy(hw_cursor.gamma, t, sizeof t);
            hw_cursor.gamma_on = !linear;
            hw_cursor.gamma_gen++;
            pthread_mutex_unlock(&hw_cursor.lock);
            c->gpr[3] = 0;
            gamma_curves += !linear;
            if (getenv("PT_PALETTE_LOG"))
                fprintf(stderr, "gamma %.3f s: %s, 64 %06x 128 %06x 255 %06x\n", (double)now_ns() / 1e9,
                        linear ? "identity" : "curve", t[64], t[128], t[255]);
        }
        return 1;
    }
    if (op == 3) {
        uint32_t off;
        c->gpr[3] = radeon && !pt_radeon_display_mode(radeon, a, w, h, &off) ? bars[2].base + off : 0;
        return 1;
    } /* the framebuffer's address, 0 refused */
    if (op == 4) {
        uint32_t rgb[256];
        if (radeon && w < 256 && h && h <= 256 - w && !(a & 3) && !guest_words(c, a, rgb, h * 4)) {
            c->gpr[3] = pt_radeon_palette(radeon, w, h, rgb) ? 1 : 0;
            static int log = -1;
            if (log < 0)
                log = getenv("PT_PALETTE_LOG") != NULL; /* development: each colour table the display driver sets */
            if (log) {
                uint32_t f = 2166136261u;
                for (uint32_t i = 0; i < h; i++)
                    f = (f ^ rgb[i]) * 16777619u;
                fprintf(stderr, "palette %.3f s: %u+%u: %06x .. %06x hash %08x\n", (double)now_ns() / 1e9, w, h, rgb[0],
                        rgb[h - 1], f);
            }
        }
        return 1;
    }
    if (op != 1 || w > 64 || h > 64 || (a & 3) || guest_words(c, a, image, n))
        return 1;
    pthread_mutex_lock(&hw_cursor.lock);
    memcpy(hw_cursor.image, image, n);
    hw_cursor.w = w;
    hw_cursor.h = h;
    hw_cursor.gen++;
    pthread_mutex_unlock(&hw_cursor.lock);
    c->gpr[3] = 0;
    return 1;
}
/* Dispatches the guest's private hypercalls: cursor, Optics diagnostics and the Tools agent protocol. */
static int private_call(CPU *c, uint32_t instruction) {
    if (cursor_call(c, instruction))
        return 1;
    if (agent && instruction == PTA_INSN && c->gpr[0] == PTA_MAGIC && c->gpr[3] == PTA_OPTICS) {
        OpticsStatus s;
        uint8_t wire[OPTICS_DIAG_BYTES];
        optics_frontend_status(radeon, &s);
        if (optics_status_encode(&s, wire))
            (void)pt_agent_optics_publish(agent, wire, sizeof wire);
    }
    uint64_t t0 = audio_stats > 0 ? host_ns() : 0;
    unsigned op = c->gpr[3], state = c->gpr[5];
    int r = pt_agent_hypercall(c, instruction, agent);
    if (r && instruction == PTA_INSN && c->gpr[0] == PTA_MAGIC && c->gpr[3] == PTA_OK) {
        if (op == PTA_INPUT) {
            pt_cursor_publish(&pointer_mailbox, state, pt_cursor_now());
            static int log = -1;
            static unsigned last = 9;
            if (log < 0)
                log = getenv("PT_CURSOR_LOG") != NULL; /* development: CGCursorIsVisible as the agent reports it */
            if (log && state != last) {
                last = state;
                fprintf(stderr, "cursor %.3f s: the agent reports %s\n", (double)now_ns() / 1e9,
                        state == 2   ? "hidden"
                        : state == 1 ? "visible"
                                     : "unknown");
            }
        } else if (op == PTA_BYE || op == PTA_HELLO)
            pt_cursor_publish(&pointer_mailbox, PT_CURSOR_UNKNOWN, pt_cursor_now());
    }
    if (audio_stats > 0) {
        uint64_t d = host_ns() - t0;
        if (d > hyper_max)
            hyper_max = d;
    }
    return r;
}
static const char *of_vars[16];
static unsigned n_of_vars;
static int boot_cd;
static const char *disk_rw;
static void shot(void);
/* --input-script FILE (tests): timed guest input, CPU thread. Lines "SEC abs X Y BUTTONS" (0..32767), "SEC rel DX DY
 * BUTTONS" (the relative mouse, as a captured window sends it), "SEC key USB-USAGE PRESSED", "SEC shot"; SEC from machine
 * start, ascending, or "+SEC" after the previous line ran. The file is followed (read again every 100 ms at its end),
 * so a test can append lines while the guest runs and play interactively. */
static FILE *script;
static uint64_t script_at = UINT64_MAX, script_t0;
static char script_line[128];
static void script_next(void) {
    double t;
    script_at = UINT64_MAX;
    while (script && fgets(script_line, sizeof script_line, script))
        if (sscanf(script_line, "%lf", &t) == 1) {
            script_at = script_line[strspn(script_line, " \t")] == '+' ? now_ns() + (uint64_t)(t * 1e9)
                                                                       : script_t0 + (uint64_t)(t * 1e9);
            return;
        }
    if (script) {
        clearerr(script);
        script_line[0] = 0;
        script_at = now_ns() + 100000000ull;
    }
}
/* Runs one line of the --input-script (pointer, key, shot or media event) and schedules the next. */
static void script_run(void) {
    double t;
    unsigned a = 0, b = 0, c = 0;
    char op[8];
    PtInputEvent e = {0};
    if (sscanf(script_line, "%lf %7s %u %u %u", &t, op, &a, &b, &c) >= 2) {
        if (!strcmp(op, "shot")) {
            if (shot_dir)
                shot();
        } else if (
                !strcmp(op,
                        "media")) { /* T media SLOT PATH|-: test path of the window's Media menu (runs hdiutil here, tests only) */
            int at = 0;
            if (sscanf(script_line, "%lf %7s %u %n", &t, op, &a, &at) >= 3 && at) {
                char *p = script_line + at;
                p[strcspn(p, "\n")] = 0;
                int e = media_change(strcmp(p, "-") ? p : NULL, (int)a);
                if (e)
                    fprintf(stderr, "media: %s: %s\n", p, strerror(e));
            }
        } else {
            if (!strcmp(op, "abs"))
                e = (PtInputEvent){.type = PT_INPUT_ABSOLUTE, .x = a, .y = b, .buttons = c};
            else if (!strcmp(op, "rel"))
                e = (PtInputEvent){.type = PT_INPUT_RELATIVE, .x = (int32_t)a, .y = (int32_t)b, .buttons = c};
            else
                e = (PtInputEvent){.type = PT_INPUT_KEY, .usage = a, .pressed = b};
            pt_input_send(&peripherals.input, &e);
        }
    }
    script_next();
}

static uint64_t host_ns(void) { /* stops while the Mac sleeps; mach_absolute_time's domain */
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
static uint64_t now_ns(void) {
    return pt_vtime_now(&vtime, host_ns());
} /* guest time; the fast TB follows it through the vtimer offset */
static void fatal(const char *s) {
    perror(s);
    exit(1);
}
/* Clock state belongs to the CPU. The timer receives only an absolute deadline. */
static void schedule_alarm(void) {
    uint64_t delay = pt_clock_deadline(&clock_model), deadline = end_ns;
    if (delay != UINT64_MAX && clock_model.last_ns < deadline && delay < deadline - clock_model.last_ns)
        deadline = clock_model.last_ns + delay;
    if (devices_enabled && pt_devices_deadline(&peripherals) < deadline)
        deadline = pt_devices_deadline(&peripherals);
    if (nvram_at < deadline)
        deadline = nvram_at;
    if (shot_at < deadline)
        deadline = shot_at;
    if (gpu_at < deadline)
        deadline = gpu_at;
    if (script_at < deadline)
        deadline = script_at;
    if (network_enabled && pt_net_rtl_deadline(&network) < deadline)
        deadline = pt_net_rtl_deadline(&network);
    deadline = pt_vtime_host(&vtime, deadline); /* the one virtual -> host conversion */
    if (atomic_exchange(&alarm_ns, deadline) != deadline)
        pt_event_signal(&alarm_event);
}
static void kick(void *opaque) {
    CPU *c = opaque;
    atomic_store_explicit(&c->irq, 1, memory_order_release);
    pt_event_signal(&wake);
}
int sys_call(CPU *c) {
    ppc_exception(c, 0xc00, c->pc, 0);
    return 0;
}
static void poll_rx(void) {
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
    if (rx < 0 && poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
        uint8_t c;
        if (read(STDIN_FILENO, &c, 1) == 1)
            rx = c;
    }
}
/* The JIT's mftb/mfdec fast path bypasses env_spr. Its coefficients MUST
 * be initialized even in this standalone board, or udelay sees a frozen TB.
 * Coefficients as the engine reads them (ppc.h tb_a/tb_m), without changing the CPU engine. */
static uint64_t fast_tb(CPU *c) {
    return c->tb_a + (uint64_t)(((unsigned __int128)pt_hv_cntvct() * c->tb_m) >> 32);
}
static void sync_fast_clock(CPU *c) {
    if (c->is970)
        return;
    mach_timebase_info_data_t t;
    mach_timebase_info(&t);
    c->tb_m = (uint64_t)(((unsigned __int128)t.numer << 32) / (40ull * t.denom));
    c->tb_a = pt_clock_tb(&clock_model) - (uint64_t)(((unsigned __int128)pt_hv_cntvct() * c->tb_m) >> 32);
    c->dec_w = pt_clock_dec(&clock_model);
    c->dec_tbw = fast_tb(c);
}
/* Engine side of mfspr/mtspr for the timer registers (DEC, TBL/TBU): keeps the clock model and the fast timebase in
 * step. */
static uint32_t spr(CPU *c, int n, int wr, uint32_t v) {
    pt_clock_update(&clock_model, now_ns());
    if (n == 22) {
        if (wr) {
            pt_clock_write_dec(&clock_model, v);
            c->dec_w = v;
            c->dec_tbw = fast_tb(c);
        }
        schedule_alarm();
        if (clock_model.dec_pending)
            kick(c);
        return c->dec_w - (uint32_t)(fast_tb(c) - c->dec_tbw);
    }
    if (clock_model.dec_pending)
        kick(c);
    if (wr) {
        pt_clock_write_tb(&clock_model, n == 269 || n == 285, v);
        sync_fast_clock(c);
    }
    uint64_t tb = fast_tb(c);
    return (n == 269 || n == 285) ? tb >> 32 : tb;
}
/* Pause at the CPU owner's service point: nothing guest-visible advances (virtual time, TB/DEC through the
 * vtimer offset, devices, audio play head, OHCI frames). Other threads keep finishing their own work (GPU, block I/O);
 * the window may stop the machine while paused. No reset, no GPU epoch change. */
static void guest_pause(void) {
    pt_vtime_pause(&vtime, host_ns());
    atomic_store(&alarm_ns, UINT64_MAX);
    uint32_t stopped = devices_enabled ? atomic_exchange(&peripherals.pcm.stopped, 1)
                                       : 0; /* the host FIFO's drain is no underrun */
    while (atomic_load(&pause_request) && !atomic_load(&stand_stop)) {
        uint64_t g = pt_event_generation(&wake);
        if (!atomic_load(&pause_request) || atomic_load(&stand_stop))
            break;
        pt_event_wait(&wake, g, 250000000); /* signals change the request without a wake */
    }
    if (devices_enabled)
        atomic_store(&peripherals.pcm.stopped, stopped);
    uint64_t d = pt_vtime_resume(&vtime, host_ns());
    pt_hv_vtimer_shift(d);
    fprintf(stderr, "ptolemy: paused %.3f s\n", d / 1e9);
}
/* Called by the CPU at interrupt checks: pauses for the window, ends the run at --seconds, advances the clock and
 * devices, and returns the pending external or decrementer interrupt. */
static uint32_t irq(CPU *c, uint32_t ack) {
    if (atomic_load(&pause_request) && !atomic_load(&stand_stop))
        guest_pause();
    uint64_t now = now_ns();
    pt_clock_update(&clock_model, now);
    if (now >= end_ns)
        c->halted = 1;
    if (ack & PEND_DEC)
        clock_model.dec_pending = 0;
    while (devices_enabled && now >= script_at)
        script_run();
    {
        static uint64_t at;
        if (audio_stats < 0)
            audio_stats = getenv("PT_AUDIO_STATS") && atoi(getenv("PT_AUDIO_STATS"));
        if (audio_stats > 0 && devices_enabled && now >= at) {
            if (at) {
                pt_devices_audio_stats(&peripherals);
                fprintf(stderr,
                        "vcpu: longest MMIO %.2f ms at %08x, longest hypercall %.2f ms, alarm late max %.2f ms\n",
                        mmio_max / 1e6, mmio_max_pa, hyper_max / 1e6, atomic_exchange(&alarm_late, 0) / 1e6);
                mmio_max = hyper_max = 0;
            }
            at = now + 5000000000ull;
        }
    }
    {
        extern int jit_stats_on;
        static uint64_t snap_at, t0;
        if (jit_stats_on && now >= snap_at) {
            if (!t0)
                t0 = now;
            else {
                fprintf(stderr, "== PT_STATS %.0f s\n", (now - t0) / 1e9);
                jit_snap();
                pt_hv_stats();
                if (devices_enabled)
                    pt_devices_audio_stats(&peripherals);
            }
            snap_at = now + 5000000000ull;
        }
    } // a snapshot every 5 s
    if (devices_enabled) {
        peripherals.usb.bus_master = !!(config[13][4] & 4);
        pt_devices_service(&peripherals, now);
        media_apply(c);
        unsigned request = pt_pmu_take_power(&peripherals.pmu);
        if (request) {
            c->halted = 1;
            if (request == PT_POWER_RESET) {
                fprintf(stderr, "Guest requested reset\n");
                c->exit_code = PT_EXIT_RESTART;
            } else if (request != PT_POWER_OFF) {
                fprintf(stderr, "Guest requested sleep; sleep is not implemented, the machine stops\n");
                c->exit_code = 96;
            }
        }
    }
    if (network_enabled) {
        network.bus_master = !!(config[15][4] & 4);
        pt_net_rtl_service(&network);
    }
    if (atomic_load(&stand_stop))
        c->halted = 1;
    if (vram_remap) {
        vram_remap = 0;
        if (radeon && vram_mapped && vram_fault)
            pt_radeon_remap(radeon);
    } /* the CPU view follows the BAR */
    if (radeon) {
        pt_radeon_service(radeon, now);
        gpu_at = now + 1000000;
    }
    if (shot_dir && now >= shot_at) {
        shot();
        shot_at += shot_period;
    }
    if (nvram.store && now >= nvram_at) {
        int e = pt_nvram_service(&nvram);
        if (e && e != EAGAIN)
            nvram_error = e;
        nvram_at = nvram_error ? UINT64_MAX : now + 250000000;
    }
    schedule_alarm();
    uint32_t r = (clock_model.dec_pending ? PEND_DEC : 0) | (pt_pic_output(&pic) ? PEND_EXT : 0);
    return r;
}
static void idle(CPU *c) {
    uint64_t g = pt_event_generation(&wake);
    uint32_t pending = irq(c, 0);
    if (!(pending && (c->msr & MSR_EE)) && !c->halted)
        pt_event_wait(&wake, g, UINT64_MAX);
}
/* Timer thread: sleeps until the next device or clock deadline and then raises the CPU's interrupt check. */
static void *ticker(void *p) {
    CPU *c = p;
    while (!atomic_load(&stop_timer)) {
        uint64_t g = pt_event_generation(&alarm_event);
        if (atomic_load(&stop_timer))
            break;
        uint64_t deadline = atomic_load(&alarm_ns), now = host_ns(); /* host domain (schedule_alarm converted) */
        if (deadline != UINT64_MAX && now >= deadline) {
            /* Disarm before waking: a concurrent CPU rearm must not be lost. */
            if (atomic_compare_exchange_strong(&alarm_ns, &deadline, UINT64_MAX)) {
                kick(c);
                if (now - deadline > atomic_load(&alarm_late))
                    atomic_store(&alarm_late, now - deadline);
            }
        } else {
            pt_event_wait(&alarm_event, g, deadline == UINT64_MAX ? UINT64_MAX : deadline - now);
        }
    }
    return 0;
}
/* Writes the current Radeon frame as a PPM screenshot (--shots and the input script's shot). */
static void radeon_shot(void) {
    PtRadeonFrame f;
    if (!pt_radeon_frame_acquire(radeon, &f))
        return;
    if (!f.pixels) {
        pt_radeon_frame_release(radeon, &f);
        return;
    } /* a native GPU frame without CPU pixels */
    static uint32_t cursor[64 * 64];
    uint32_t cw, ch;
    uint64_t pos = atomic_load(&hw_cursor.pos); /* the hardware cursor on top */
    static uint32_t gamma[256];
    int gamma_on; /* and the display's output gamma, as the window shows it */
    pthread_mutex_lock(&hw_cursor.lock);
    cw = hw_cursor.w;
    ch = hw_cursor.h;
    memcpy(cursor, hw_cursor.image, sizeof cursor);
    gamma_on = hw_cursor.gamma_on;
    memcpy(gamma, hw_cursor.gamma, sizeof gamma);
    pthread_mutex_unlock(&hw_cursor.lock);
    char name[1024];
    snprintf(name, sizeof name, "%s/shot-%04u.ppm", shot_dir, shot_n++);
    FILE *out = fopen(name, "wb");
    if (out) {
        fprintf(out, "P6\n%u %u\n255\n", f.width, f.height);
        for (unsigned y = 0; y < f.height; y++)
            for (unsigned x = 0; x < f.width; x++) {
                const uint8_t *a = f.pixels + (size_t)y * f.line;
                uint8_t rgb[3];
                if (f.bpp == 32) {
                    a += x * 4;
                    rgb[0] = a[f.big_endian ? 1 : 2];
                    rgb[1] = a[1 + f.big_endian];
                    rgb[2] = a[f.big_endian ? 3 : 0];
                } else if (f.bpp == 24) {
                    a += x * 3;
                    for (int k = 0; k < 3; k++)
                        rgb[k] = a[f.big_endian ? k : 2 - k];
                } else if (f.bpp == 15 || f.bpp == 16) {
                    uint32_t v = f.big_endian ? pt_read_be(a + x * 2, 2) : pt_read_le(a + x * 2, 2);
                    rgb[0] = ((v >> (f.bpp == 15 ? 10 : 11)) & 31) * 255 / 31;
                    rgb[1] = ((v >> 5) & (f.bpp == 15 ? 31 : 63)) * 255 / (f.bpp == 15 ? 31 : 63);
                    rgb[2] = (v & 31) * 255 / 31;
                } else
                    memcpy(rgb, f.palette[a[x]], 3);
                int64_t cx = (int64_t)x - (int16_t)pos, cy = (int64_t)y - (int16_t)(pos >> 16);
                if (pos >> 32 && cx >= 0 && cy >= 0 && cx < cw && cy < ch) {
                    uint32_t c = cursor[cy * cw + cx], alpha = c >> 24;
                    for (unsigned k = 0; k < 3; k++)
                        rgb[k] = c == 0x00ffffffu
                                         ? 255 - rgb[k]
                                         : (rgb[k] * (255 - alpha) + ((c >> (16 - 8 * k)) & 255) * alpha) / 255;
                }
                if (gamma_on)
                    for (unsigned k = 0; k < 3; k++)
                        rgb[k] = gamma[rgb[k]] >> (16 - 8 * k) & 255;
                fwrite(rgb, 1, 3, out);
            }
        fclose(out);
    }
    if (getenv("PT_SHOT_RAW")) { /* development: the scanout's bytes as published, with "w h bpp line big_endian" first */
        snprintf(name, sizeof name, "%s/shot-%04u.raw", shot_dir, shot_n - 1);
        FILE *raw = fopen(name, "wb");
        if (raw) {
            fprintf(raw, "%u %u %u %u %d\n", f.width, f.height, f.bpp, f.line, f.big_endian);
            fwrite(f.pixels, 1, (size_t)f.line * f.height, raw);
            fclose(raw);
        }
    }
    pt_radeon_frame_release(radeon, &f);
}
static void shot(void) {
    if (radeon)
        radeon_shot();
} /* PPM snapshot, CPU owner. */
static void pci_init(void) {
    memset(cfgmask, 0, sizeof cfgmask);
    memset(config, 0xff, sizeof config);
    memset(config[11], 0, 256);
    memset(config[12], 0, 256);
    pt_write_le(config[11], 4, 0x001f106b);
    config[11][11] = 6;
    config[11][8] = 0;
    if (cpu970)
        pt_write_le(config[11], 4, 0x004b106b);
    pt_agp_init(config[11], cfgmask[11], &agp);
    config[11][0x4b] = 0x7f; /* kMacRISCPCIAddressSelect, PCI memory 0x80000000-0xefffffff */
    pt_write_le(config[12], 4, 0x0022106b);
    config[12][11] = 0xff;
    pt_write_le(config[12] + 0x10, 4, 0x80000000);
    cfgmask[11][4] = cfgmask[12][4] = 7;
    pt_write_le(cfgmask[12] + 0x10, 4, 0xfff80000);
    if (devices_enabled) {
        memset(config[13], 0, 256);
        pt_write_le(config[13], 4, 0x003f106b);
        config[13][9] = 0x10;
        config[13][10] = 3;
        config[13][11] = 0x0c;
        config[13][0x3d] = 1;
        config[13][0x3c] = 0x1c;
        pt_write_le(config[13] + 0x10, 4, bars[1].base);
        pt_write_le(cfgmask[13] + 0x10, 4, 0xffffff00u);
        cfgmask[13][4] = 6;
    }
    if (radeon_enabled) {
        memset(config[14], 0, 256);
        pt_write_le(config[14], 4, 0x4e441002);
        config[14][11] = 3;
        config[14][6] = 0x10;
        config[14][0x34] = 0x50;
        config[14][0x3c] = 0x1d;
        config[14][0x3d] = 1;
        pt_write_le(config[14] + 0x2c, 4, 0x4e441002);
        cfgmask[14][4] = 7;
        config[14][0x50] = 1;
        config[14][0x51] = 0x58;
        pt_write_le(config[14] + 0x52, 2, 0x601);
        cfgmask[14][0x54] = 3;
        config[14][0x58] = 2;
        config[14][0x5a] = 0x20;
        pt_write_le(config[14] + 0x5c, 4, 0x1f000207);
        pt_write_le(config[14] + 0x60, 4, 0x1f000304);
        memset(cfgmask[14] + 0x60, 255, 4);
    }
    if (network_enabled)
        pt_net_rtl_pci(config[15], cfgmask[15]);
}
/* A guest write to a PCI BAR: moves the device's I/O or memory window, within the ranges the board allows. */
static void relocate_bar(unsigned i, uint32_t value) {
    PtBar *b = &bars[i];
    uint32_t base = value & ~(b->size - 1);
    if (b->flags & 1) {
        if (!network_enabled) {
            if (base >= 0x1000 && (uint64_t)base + b->size <= 0x10000)
                b->base = base;
            return;
        }
        if (base < 0x1000 || (uint64_t)base + b->size > 0x10000)
            goto reject;
        for (unsigned j = 0; j < nbars; j++)
            if (j != i && (bars[j].flags & 1) && base < (uint64_t)bars[j].base + bars[j].size &&
                (uint64_t)base + b->size > bars[j].base)
                goto reject;
        b->base = base;
        return;
    }
    if (base == b->base)
        return;
    if (base < guest_ram_bytes || (uint64_t)base + b->size > 0xf0000000ull)
        goto reject;
    if (radeon_enabled && b->slot == 14 && (base < 0x80000000u || (uint64_t)base + b->size > pci_window_end))
        goto reject;
    for (unsigned j = 0; j < bus.count; j++)
        if (j != b->region) {
            PtRegion *r = &bus.regions[j];
            if ((uint64_t)base < r->base + r->size && (uint64_t)base + b->size > r->base)
                goto reject;
        }
    b->base = base;
    bus.regions[b->region].base = base;
    if (b->slot == 14 && b->reg == 0x10)
        vram_remap = 1;
    return;
reject:
    /* Fail closed rather than mapping over RAM/ROM/another device. */
    config[b->slot][4] &= ~(b->flags & 1 ? 1u : 2u);
    fprintf(stderr, "Rejected PCI BAR (slot %u, reg %02x) assignment %08x; address decode disabled\n", b->slot, b->reg,
            value);
}
/* Configuration cycles on the three Uni-North PCI bridges: the index register picks device and register, the data
 * register reads or writes it. */
static int pci_access(uint32_t a, uint8_t *b, unsigned n, int wr) {
    for (unsigned bridge = 0; bridge < 3; bridge++) {
        uint32_t index = 0xf0800000u + bridge * 0x02000000u, data = index + 0x400000;
        if (a >= index && a < index + 4 && n == 4) {
            if (wr)
                cfg_index[bridge] = pt_read_le(b, 4);
            else
                pt_write_le(b, 4, cfg_index[bridge]);
            return 1;
        }
        if (a >= data && (uint64_t)a + n <= data + 4096ull) {
            uint32_t ca = pt_pci_address(cfg_index[bridge], a - data);
            unsigned bus = (ca >> 16) & 255, slot = (ca >> 11) & 31, fn = (ca >> 8) & 7, reg = ca & 255;
            if (bridge != (cpu970 ? 0u : 1u) || bus || fn ||
                (slot != 11 && slot != 12 && !(devices_enabled && slot == 13) && !(radeon_enabled && slot == 14) &&
                 !(network_enabled && slot == 15)) ||
                reg + n > 256) {
                if (!wr)
                    memset(b, 255, n);
                return 1;
            }
            for (unsigned i = 0; i < nbars; i++) {
                PtBar *bar = &bars[i];
                if (bar->slot != slot || reg >= bar->reg + 4 || reg + n <= bar->reg)
                    continue;
                if (wr) {
                    if (reg == bar->reg && n == 4 && pt_read_le(b, 4) == UINT32_MAX)
                        bar->probe = 1;
                    else {
                        uint8_t bytes[4];
                        pt_write_le(bytes, 4, bar->base);
                        for (unsigned k = 0; k < n; k++)
                            if (reg + k >= bar->reg && reg + k < bar->reg + 4)
                                bytes[reg + k - bar->reg] = b[k];
                        bar->probe = 0;
                        relocate_bar(i, pt_read_le(bytes, 4));
                    }
                } else {
                    memcpy(b, config[slot] + reg, n);
                    uint8_t bytes[4];
                    pt_write_le(bytes, 4, (bar->probe ? ~(bar->size - 1) : bar->base) | bar->flags);
                    for (unsigned k = 0; k < n; k++)
                        if (reg + k >= bar->reg && reg + k < bar->reg + 4)
                            b[k] = bytes[reg + k - bar->reg];
                }
                if (wr && radeon && slot == 14)
                    radeon_configure();
                return 1;
            }
            if (wr)
                for (unsigned i = 0; i < n; i++) {
                    uint8_t mask = cfgmask[slot][reg + i];
                    config[slot][reg + i] = (config[slot][reg + i] & ~mask) | (b[i] & mask);
                }
            else
                memcpy(b, config[slot] + reg, n);
            if (wr && slot == 11)
                pt_agp_update(&agp, config[11], reg, n);
            if (wr && radeon && (slot == 11 || slot == 14))
                radeon_configure();
            return 1;
        }
    }
    return 0;
}
/* Accesses to the board's PCI memory windows (mac-io, Radeon, network card): routes them to the device that owns
 * the address. */
static int board_access(void *base, uint32_t offset, uint8_t *b, unsigned n, int wr) {
    uint32_t a = (uint32_t)(uintptr_t)base + offset;
    if (((uintptr_t)base == 0x80000000u && !(config[12][4] & 2)) ||
        ((uintptr_t)base == 0x80080000u && !(config[13][4] & 2))) {
        if (!wr)
            memset(b, 255, n);
        return -1;
    }
    int known = 1;
    uint64_t value = wr ? pt_read_be(b, n) : 0;
    if (a == 0xf0000510 && n == 2) {
        if (wr)
            pt_fw_select(&fw, value);
        else
            memset(b, 0, n);
    } else if (a == 0xf0000512 && n == 1) {
        if (!wr)
            b[0] = pt_fw_read(&fw);
    } else if (a >= 0xfff04000 && (uint64_t)a + n <= 0xfff08000) {
        pt_nvram_access(&nvram, a - 0xfff04000, b, n, wr);
        if (wr && nvram.store && nvram_at == UINT64_MAX && !nvram_error)
            nvram_at = now_ns();
    } else if (pci_access(a, b, n, wr)) {
    } else if (devices_enabled && (uintptr_t)base == 0x80080000u && n == 4 && !(offset & 3)) {
        if (wr) {
            pt_ohci_write(&peripherals.usb, offset, pt_read_le(b, 4));
            peripherals.service_requested = 1;
        } else
            pt_write_le(b, 4, pt_ohci_read(&peripherals.usb, offset));
    } else if (devices_enabled && (uintptr_t)base == 0x80000000u &&
               pt_devices_macio(&peripherals, offset, b, n, wr, now_ns()) == 0) {
    } else if (a >= 0x80040000 && a <= 0x8007f0fc && n == 4) {
        if (wr)
            pt_pic_write(&pic, a - 0x80040000, pt_read_le(b, n));
        else
            pt_write_le(b, n, pt_pic_read(&pic, a - 0x80040000));
    } else if (((a >= 0x80013000 && a < 0x80013040) || (a >= 0x80012000 && a < 0x80012008)) && n == 1) {
        unsigned legacy = a < 0x80013000;
        unsigned ch = legacy ? (a >> 1) & 1 : (a >> 5) & 1, data = legacy ? (a >> 2) & 1 : (a >> 4) & 1;
        if (data) {
            if (wr) {
                putchar(b[0]);
                fflush(stdout);
            } else {
                poll_rx();
                b[0] = rx < 0 ? 0 : (uint8_t)rx;
                rx = -1;
            }
        } else {
            if (wr) {
                if (scc_reg[ch]) {
                    scc_wr[ch][scc_reg[ch]] = b[0];
                    scc_reg[ch] = 0;
                } else
                    scc_reg[ch] = b[0] & 15;
            } else {
                poll_rx();
                b[0] = scc_reg[ch] == 1 ? 1 : scc_reg[ch] ? 0 : 4 | (rx >= 0);
                scc_reg[ch] = 0;
            }
        }
    } else {
        known = 0;
        /* Absent ATA devices return floating bus, other unimplemented
         * macio controls read zero. Every such access is in the trace. */
        if (!wr)
            memset(b, (a >= 0x80020000 && a < 0x80022000) ? 255 : 0, n);
    }
    return known ? 0 : -1;
}
static void radeon_configure(void) {
    for (unsigned i = 0; i < nbars; i++)
        if (bars[i].slot == 14)
            pt_write_le(config[14] + bars[i].reg, 4, bars[i].base | bars[i].flags);
    pt_radeon_config(radeon, config[14], &agp);
}
/* Maps or unmaps the Radeon's VRAM aperture straight into the guest, so CPU access to the frame buffer avoids
 * exits. */
static void radeon_direct_map(void *opaque, uint8_t *ptr, size_t n, int enable) {
    CPU *c = opaque;
    if (!enable) {
        if (vram_mapped) {
            pt_hv_unmap(vram_mapped, radeon_mapped_bytes);
            vram_mapped = 0;
            radeon_mapped_bytes = 0;
            c->npreg = 3;
        }
        return;
    }
    /* enable 2 = guarded (Ptolemy's frontend): stage-2 only, no interpreter alias (interpreter accesses keep trapping) */
    if (enable >= 2) {
        if (vram_mapped)
            return;
        uint32_t at = bars[2].base + (enable == 3 ? (uint32_t)n : 0); /* 3: aperture 1 (8/16-bit framebuffer) */
        pt_hv_map(ptr, at, n, 0);
        vram_mapped = at;
        radeon_mapped_bytes = n;
        return;
    }
    pt_hv_map(ptr, bars[2].base, n, 0);
    vram_mapped = bars[2].base;
    radeon_mapped_bytes = n;
    c->preg[3] = (PRegion){bars[2].base, n, ptr, 0};
    c->npreg = 4;
    /* IPA and PPC mappings do not change. Stage-2 unmap performs the host TLB
     * invalidation; do not flush the entire PPC JIT on each mapping grant. */
}
static void radeon_irq(void *opaque, int level) {
    (void)opaque;
    pt_pic_line(&pic, 0x1d, 0, level);
}
/* Guarded direct VRAM of Ptolemy's frontend. Protection of aperture-0 groups; nothing mapped means every
 * access traps already. The device's fault route runs on stage-2 faults inside the mapping, before the device route. */
static int radeon_vram_protect(void *opaque, uint64_t off, uint64_t n, unsigned prot) {
    (void)opaque;
    if (!vram_mapped)
        return 0;
    if (off > radeon_mapped_bytes || n > radeon_mapped_bytes - off)
        return -1;
    return pt_hv_protect(vram_mapped + off, n, prot);
}
static int board_guard(CPU *c, uint32_t ipa, uint64_t far, int write, uint64_t pc, uint32_t in) {
    (void)c;
    if (!vram_fault || !vram_mapped || ipa < vram_mapped || (uint64_t)ipa - vram_mapped >= radeon_mapped_bytes)
        return 0;
    return vram_fault(vram_fault_device, (uint64_t)ipa - vram_mapped, far, write, pc, in);
}
static void radeon_vram_guard(void *opaque, int (*fault)(void *, uint64_t, uint64_t, int, uint64_t, uint32_t),
                              void *device) {
    CPU *c = opaque;
    vram_fault = fault;
    vram_fault_device = device;
    c->env_guard = fault ? board_guard : NULL;
}
/* A Radeon write reached guest RAM (CP write-back, 2D into GART). The CPU keeps its lwarx reservation as an
 * EA, so it is dropped conservatively on any device write: a spurious stwcx. failure is architecturally allowed. */
/* With the CP thread this runs beside the vCPU: a racing stwcx. may still succeed against a write-back slot (no driver
 * uses lwarx/stwcx. there); the store itself is atomic. */
static void radeon_ram_written(void *opaque, uint64_t pa, size_t n) {
    CPU *c = opaque;
    (void)pa;
    (void)n;
#ifdef PT_ENABLE_970
    if (c->is970) {
        if (ppc970_owner_write(c->bus970, pa, n)) {
            c->halted = 1;
            c->exit_code = 74;
        }
        return;
    }
#endif
    __atomic_store_n(&c->reserve, 0, __ATOMIC_RELAXED);
}
#ifdef PT_ENABLE_970
/* 970 bring-up: tells Optics before a CPU access to guest RAM that the GPU may also use. */
static int radeon_ram970(CPU *c, uint64_t pa, unsigned n, int write) {
    (void)c;
    if (!radeon || pa >= guest_ram_bytes)
        return 0;
    /* No concurrent CP or direct guest-RAM GPU stores in this bring-up profile. */
    int e = optics_frontend_ram_before(radeon, pa, n, write);
    if (e != EAGAIN)
        return e;
    uint64_t start = now_ns();
    while (e == EAGAIN) {
        pt_radeon_service(radeon, now_ns());
        if (now_ns() - start > 5000000000ull) {
            fprintf(stderr, "970 RAM/GPU ownership did not retire\n");
            return EIO;
        }
        struct timespec delay = {0, 100000};
        nanosleep(&delay, NULL);
        e = optics_frontend_ram_before(radeon, pa, n, write);
    }
    return e;
}
#endif
static int radeon_access(void *opaque, uint32_t off, uint8_t *b, unsigned n, int wr) {
    if (!(config[14][4] & 2)) {
        if (!wr)
            memset(b, 255, n);
        return -1;
    }
    int e = (uintptr_t)opaque == 2 ? pt_radeon_vram(radeon, off, b, n, wr) : pt_radeon_mmio(radeon, off, b, n, wr);
    if (e && !wr)
        memset(b, 0, n);
    return e ? -1 : 0;
}
/* Legacy I/O port accesses: the network card's I/O BAR when it is enabled, otherwise nothing answers. */
static int isa_access(void *opaque, uint32_t port, uint8_t *b, unsigned n, int wr) {
    port += (uint32_t)(uintptr_t)opaque;
    int r = -1;
    if (network_enabled && (config[15][4] & 1))
        for (unsigned i = 0; i < nbars; i++) {
            PtBar *bar = &bars[i];
            if (bar->slot == 15 && (bar->flags & 1) && port >= bar->base &&
                (uint64_t)port + n <= (uint64_t)bar->base + bar->size) {
                return network_access(NULL, port - bar->base, b, n, wr);
            }
        }
    if (radeon && (config[14][4] & 1)) {
        if (port >= bars[4].base && (uint64_t)port + n <= bars[4].base + bars[4].size)
            r = pt_radeon_mmio(radeon, port - bars[4].base, b, n, wr);
        else
            r = pt_radeon_port(radeon, port, b, n, wr);
    }
    if (r && !wr)
        memset(b, 255, n);
    return r;
}
static int network_access(void *opaque, uint32_t offset, uint8_t *bytes, unsigned n, int write) {
    unsigned decode = opaque ? 2 : 1;
    if (!(config[15][4] & decode)) {
        if (!write)
            memset(bytes, 255, n);
        return -1;
    }
    network.bus_master = !!(config[15][4] & 4);
    return pt_net_rtl_access(&network, offset, bytes, n, write) ? -1 : 0;
}
static void add_network_regions(void) {
    if (!radeon_enabled) {
        PtRegion isa = {0xf2000000u, 0x10000, isa_access, NULL, "isa-io"};
        if (pt_bus_add(&bus, isa))
            fatal("network I/O window");
    }
    bars[nbars++] = (PtBar){15, 0x10, 0x100, 1, 0x2000, 0, UINT32_MAX};
    bars[nbars++] = (PtBar){15, 0x14, 0x100, 0, 0x800a0000u, 0, bus.count};
    PtRegion memory = {0x800a0000u, 0x100, network_access, (void *)1, "rtl8139-mmio"};
    if (pt_bus_add(&bus, memory))
        fatal("network BAR");
    pt_write_le(config[15] + 0x10, 4, 0x2001);
    pt_write_le(config[15] + 0x14, 4, memory.base);
}
static void add_pci_io_region(void) {
    if (cpu970) {
        /* AppleMacRiscPCI derives config ports from the OF I/O base. Keep
         * fw_cfg's three bytes separate from the G5 PCI port aperture. */
        PtRegion low = {0xf0000000u, 0x510, isa_access, NULL, "pci-io-low"};
        PtRegion high = {0xf0000513u, 0x10000 - 0x513, isa_access, (void *)0x513, "pci-io-high"};
        if (pt_bus_add(&bus, low) || pt_bus_add(&bus, high))
            fatal("G5 PCI I/O window");
    } else {
        PtRegion r = {0xf2000000u, 0x10000, isa_access, NULL, "isa-io"};
        if (pt_bus_add(&bus, r))
            fatal("PCI I/O window");
    }
}
static void trace_line(const typeof(ring[0]) *e) {
    fprintf(trace,
            "{\"seq\":%llu,\"host_ns\":%llu,\"guest_TB\":%llu,\"pc\":%u,\"pa\":%u,\"size\":%d,\"rw\":\"%c\",\"value\":%llu,\"known\":%s}\n",
            (unsigned long long)e->seq, (unsigned long long)e->ns, (unsigned long long)e->tb, e->pc, e->pa, e->n,
            e->wr ? 'w' : 'r', (unsigned long long)e->val, e->known ? "true" : "false");
    trace_written++;
}
/* Every guest MMIO access ends here (JIT stage-2 faults and interpreter accesses): ROM writes are ignored, the rest
 * goes to the device bus. */
static void mmio(CPU *c, uint32_t a, uint8_t *b, int n, int wr) {
    uint64_t value = wr ? pt_read_be(b, n) : 0;
    /* JIT stage-2 permission faults and interpreter ROM stores converge here. */
    int rom_write = wr && pmem(c, a, n) && !pmem_write(c, a, n);
    uint64_t t0 = audio_stats > 0 ? host_ns() : 0;
    int known = rom_write || pt_bus_access(&bus, a, b, n, wr) == 0;
    if (audio_stats > 0) {
        uint64_t d = host_ns() - t0;
        if (d > mmio_max) {
            mmio_max = d;
            mmio_max_pa = a;
        }
    }
    /* Bounded diagnostic capture, never a silent claim of a complete trace. */
    if (trace && (tail || (trace_written < 100000 && trace_keep(a, c->pc)))) {
        uint64_t now = now_ns();
        pt_clock_update(&clock_model, now);
        typeof(ring[0]) e = {seq,
                             now,
                             pt_clock_tb(&clock_model),
                             wr ? value : pt_read_be(b, n),
                             c->pc,
                             a,
                             (uint8_t)n,
                             (uint8_t)wr,
                             (uint8_t)known};
        if (tail)
            ring[seq % TAIL] = e;
        else
            trace_line(&e);
    }
    seq++;
    /* Status/data reads can acknowledge IRQ or complete PIO. Defer service to
     * the CPU scheduling boundary; never recurse into devices from MMIO. */
    if ((devices_enabled && peripherals.service_requested) || (wr && a >= 0xfff04000 && a < 0xfff08000 && nvram.store))
        atomic_store_explicit(&c->irq, 1, memory_order_release);
}
/* Registers the board's MMIO ranges (mac-io, fw_cfg, PCI bridges, Radeon) on the device bus. */
static void bus_init(void) {
    static const struct {
        uint32_t base;
        uint64_t size;
        const char *name;
    } ranges[] = {{0x80000000, 0x80000, "macio"},
                  {0xf0000510, 3, "fw_cfg"},
                  {0xfff04000, 0x4000, "nvram"},
                  {0xf0800000, 0x1000, "agp-config-index"},
                  {0xf0c00000, 0x1000, "agp-config-data"},
                  {0xf2800000, 0x1000, "pci-config-index"},
                  {0xf2c00000, 0x1000, "pci-config-data"},
                  {0xf4800000, 0x1000, "internal-pci-index"},
                  {0xf4c00000, 0x1000, "internal-pci-data"},
                  {0xf8000000, 0x1000, "uninorth-control"}};
    for (unsigned i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        if (i == 0)
            bars[0].region = bus.count;
        PtRegion r = {ranges[i].base, ranges[i].size, board_access, (void *)(uintptr_t)ranges[i].base, ranges[i].name};
        if (cpu970 && r.base == 0xf8000000)
            r.size = 0x80000;
        if (pt_bus_add(&bus, r)) {
            fprintf(stderr, "Invalid memory map\n");
            exit(2);
        }
    }
}
static void add_usb_region(void) {
    bars[1].region = bus.count;
    PtRegion r = {bars[1].base, bars[1].size, board_access, (void *)(uintptr_t)0x80080000u, "ohci"};
    if (pt_bus_add(&bus, r)) {
        fprintf(stderr, "Invalid OHCI region\n");
        exit(2);
    }
}
static void add_radeon_regions(void) {
    nbars = 5;
    bars[2] = (PtBar){14, 0x10, radeon_memory.aperture, 8, radeon_memory.base, 0, bus.count};
    bars[3] = (PtBar){14, 0x18, 0x10000, 0, 0x80090000u, 0, bus.count + 1};
    bars[4] = (PtBar){14, 0x14, 0x100, 1, 0x1000, 0, UINT32_MAX};
    PtRegion fb = {bars[2].base, bars[2].size, radeon_access, (void *)2, "radeon-vram"};
    PtRegion io = {bars[3].base, bars[3].size, radeon_access, (void *)3, "radeon-mmio"};
    if (pt_bus_add(&bus, fb) || pt_bus_add(&bus, io)) {
        fprintf(stderr, "Invalid Radeon regions\n");
        exit(2);
    }
    add_pci_io_region();
}
/* Loads the OpenBIOS ELF image into guest memory at its link addresses. */
static void load_rom(uint8_t *ram, const char *path) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st))
        fatal(path);
    if (st.st_size < 52 || st.st_size > 4 * 1024 * 1024) {
        fprintf(stderr, "Invalid firmware length\n");
        exit(2);
    }
    uint8_t *f = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (f == MAP_FAILED)
        fatal("firmware mmap");
    if (memcmp(f, "\177ELF\1\2", 6) || pt_read_be(f + 18, 2) != 20) {
        fprintf(stderr, "Expected PPC32 BE ELF\n");
        exit(2);
    }
    uint64_t off = pt_read_be(f + 28, 4), count = pt_read_be(f + 44, 2), entsize = pt_read_be(f + 42, 2);
    if (entsize < 32 || off + count * entsize > (uint64_t)st.st_size) {
        fprintf(stderr, "Invalid ELF program headers\n");
        exit(2);
    }
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *p = f + off + i * entsize;
        if (pt_read_be(p, 4) != 1)
            continue;
        uint64_t src = pt_read_be(p + 4, 4), addr = pt_read_be(p + 12, 4), len = pt_read_be(p + 16, 4),
                 mem = pt_read_be(p + 20, 4);
        if (len > mem || src + len > (uint64_t)st.st_size || addr < 0xfff00000 || addr + mem > (1ull << 32)) {
            fprintf(stderr, "Firmware segment outside ROM\n");
            exit(2);
        }
        memcpy(ram + addr, f + src, len);
    }
    munmap(f, st.st_size);
}
static const char *usb_disk = NULL, *usb_save = NULL;
static int overlay; /* --overlay, in-memory disk overlays */
static const char *rom = 0, *disk = NULL, *nvram_path = NULL, *cd_path = NULL;
int interpreter = 0, audio = 0, product_boot = 0;
unsigned ram_mb = 2048;
const char *boot_device = NULL;
unsigned seconds = 15;
static int cpu_given;
/* One identity per CPU profile, applied at CPU reset. L1 sizes and the 32-byte line come from the
 * ROM's per-PVR table (32 KiB I/D for all three); clocks are board properties (PowerBook5,8, fw_cfg). Only the
 * 7447A models its L2: L2CR enabled as the PowerBook firmware leaves it (XNU init745X keys the 512 KiB L2 on L2E). */
static const struct {
    const char *name;
    uint32_t pvr, l2cr;
} cpus[] = {{"750", 0x00080301, 0}, {"7400", 0x000c0209, 0}, {"7447a", 0x80030102, 0x80000000u}};
#ifdef PT_ENABLE_970
static uint64_t ticks970(CPU *c) {
    (void)c;
    return now_ns() / 40;
}
static int mmio970(CPU *c, uint64_t address, uint8_t *bytes, int n, int write) {
    if (address >= 0xf8000000 && address < 0xf8080000) {
        int e = pt_g5_bridge_access(&g5_bridge, (uint32_t)address - 0xf8000000, bytes, (unsigned)n, write);
        if (e)
            fprintf(stderr, "G5 bridge refused %s %llx/%d\n", write ? "write" : "read", (unsigned long long)address, n);
        return e;
    }
    if (address >> 32)
        return -1;
    mmio(c, (uint32_t)address, bytes, n, write);
    return 0;
}
/* A reference CPU has no native timer exit. Poll board work at most once per
 * millisecond; DEC is evaluated separately on every architectural boundary. */
static uint32_t irq970(CPU *c, uint32_t ack) {
    static uint64_t due, report;
    uint64_t now = now_ns();
    if (now >= due || atomic_load(&stand_stop)) {
        irq(c, ack);
        due = now + 1000000;
        if (env_on("PT_970_TRACE") && now >= report) {
            fprintf(stderr,
                    "970 progress: pc=%016llx instructions=%llu sdr1=%016llx srr0=%016llx srr1=%016llx dar=%016llx dsisr=%08x\n",
                    (unsigned long long)ppc_pc64(c), (unsigned long long)c->icount,
                    (unsigned long long)ppc_spr64(c, SPR_SDR1), (unsigned long long)ppc_spr64(c, SPR_SRR0),
                    (unsigned long long)ppc_spr64(c, SPR_SRR1), (unsigned long long)ppc_spr64(c, SPR_DAR),
                    c->spr[SPR_DSISR]);
            report = now + 5000000000ull;
        }
    }
    return pt_pic_output(&pic) ? PEND_EXT : 0;
}
#endif
static unsigned cpu_model;
static int machine(void);
static void stop_signal(int sig) {
    (void)sig;
    atomic_store(&stand_stop, 1);
} /* SIGTERM/SIGINT stop in order, with statistics */
static void pause_signal(int sig) {
    atomic_store(&pause_request, sig == SIGUSR1);
} /* SIGUSR1 pause, SIGUSR2 resume (tests, headless) */
/* The resolved run configuration, printed once by the machine thread after CLI parsing, product defaults and the
 * window's EDID mode choice: the record a result is tied to. */
static int main_argc;
static char **main_argv;
static void print_config(void) {
    extern char **environ;
    static const char *const pre[] = {"PT_", "OPTICS_", "MTL_"};
    fprintf(stderr, "ptolemy config: pvr=%08x args:", cpu970 ? 0x00440100 : cpus[cpu_model].pvr);
    for (int i = 1; i < main_argc; i++)
        fprintf(stderr, " %s", main_argv[i]);
    fprintf(stderr, " env:");
    for (char **e = environ; *e; e++)
        for (unsigned k = 0; k < sizeof pre / sizeof *pre; k++)
            if (!strncmp(*e, pre[k], strlen(pre[k]))) {
                fprintf(stderr, " %s", *e);
                break;
            }
    fputc('\n', stderr);
}
/* Parses the command line, builds the machine (memory, devices, firmware, window), runs the CPU until it stops,
 * then reports and cleans up. */
int main(int argc, char **argv) {
    main_argc = argc;
    main_argv = argv;
    const char *edid = getenv("PT_EDID_MODE");
    char *edid_mode = edid ? strdup(edid) : NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            rom = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            char *end;
            unsigned long t = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || argv[i][0] < '0' || argv[i][0] > '9' || *end || t > 3600)
                return 2;
            seconds = t;
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            int fd = open(argv[++i], O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0 || !(trace = fdopen(fd, "w")))
                fatal("trace");
            {
                const char *r = getenv("PT_TRACE_RANGE");
                unsigned k = 0;
                int used = 0;
                while (r && k < 4 && sscanf(r, "%x-%x%n", &trace_range[2 * k], &trace_range[2 * k + 1], &used) == 2) {
                    k++;
                    r += used;
                    if (*r++ != ',')
                        break;
                }
                if (k)
                    trace_ranges = k;
                else {
                    trace_range[0] = 0;
                    trace_range[1] = 0xffffffffu;
                }
                if ((r = getenv("PT_TRACE_PC_BELOW")))
                    trace_pc_below = (unsigned)strtoul(r, NULL, 16);
            }
        } else if (!strcmp(argv[i], "--interpreter"))
            interpreter = 1;
        else if (!strcmp(argv[i], "--nvram") && i + 1 < argc)
            nvram_path = argv[++i];
        else if (!strcmp(argv[i], "--cd") && i + 1 < argc) {
            cd_path = argv[++i];
            devices_enabled = 1;
        } else if (!strcmp(argv[i], "--boot")) {
            product_boot = 1;
            devices_enabled = 1;
        } else if (!strcmp(argv[i], "--boot-device") && i + 1 < argc)
            boot_device = argv[++i];
        else if (!strcmp(argv[i], "--ram-mb") && i + 1 < argc) {
            char *end;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (*end || value > 2048 || pt_board_ram(value, &guest_ram_bytes))
                return 2;
            ram_mb = value;
        } else if (!strcmp(argv[i], "--devices"))
            devices_enabled = 1;
        else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) {
            const char *v = argv[++i];
            cpu_given = 1;
#ifdef PT_ENABLE_970
            if (!strcmp(v, "970mp")) {
                cpu970 = interpreter = 1;
                continue;
            }
#endif
            for (cpu_model = 0; cpu_model < 3 && strcmp(v, cpus[cpu_model].name); cpu_model++)
                ;
            if (cpu_model == 3)
                return 2;
        } else if (!strcmp(argv[i], "--radeon")) {
            radeon_enabled = devices_enabled = 1;
        } else if (!strcmp(argv[i], "--radeon-fast")) {
            radeon_fast = radeon_enabled = devices_enabled = 1;
        } else if (!strcmp(argv[i], "--vram-mb") && i + 1 < argc) {
            char *end;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (*end || (value != 128 && value != 256) || !pt_gpu_memory((unsigned)value, &radeon_memory)) {
                fprintf(stderr, "mac99 supports --vram-mb 128 or 256; larger VRAM needs a different board map\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--of") && i + 1 < argc && n_of_vars < 16 && strchr(argv[i + 1], '='))
            of_vars[n_of_vars++] = argv[++i];
        else if (!strcmp(argv[i], "--boot-cd"))
            boot_cd = 1;
        else if (!strcmp(argv[i], "--usb-disk") && i + 1 < argc) {
            usb_disk = argv[++i];
            devices_enabled = 1;
        } else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            devices_enabled = 1;
            disk_rw = argv[++i];
        } else if (!strcmp(argv[i], "--window")) {
            window_mode = devices_enabled = 1;
        } else if (!strcmp(argv[i], "--input-script") && i + 1 < argc) {
            if (!(script = fopen(argv[++i], "r")))
                fatal("input script");
        } else if (!strcmp(argv[i], "--shots") && i + 2 < argc) {
            shot_dir = argv[++i];
            shot_period = strtoull(argv[++i], NULL, 10) * 1000000000ull;
            if (!shot_period)
                return 2;
        } else if (!strcmp(argv[i], "--audio")) {
            devices_enabled = 1;
            audio = 1;
        } else if (!strcmp(argv[i], "--network-user"))
            network_user_enabled = devices_enabled = 1;
        else if (!strcmp(argv[i], "--network-shared"))
            network_shared = devices_enabled = 1;
        else if (!strcmp(argv[i], "--network-socket") && i + 1 < argc) {
            network_socket = argv[++i];
            devices_enabled = 1;
        } else if (!strcmp(argv[i], "--agent-socket") && i + 1 < argc)
            agent_socket = argv[++i];
        else if (!strcmp(argv[i], "--disk-readonly") && i + 1 < argc) {
            devices_enabled = 1;
            disk = argv[++i];
        } else if (!strcmp(argv[i], "--overlay"))
            overlay = 1;
        else if (!strcmp(argv[i], "--usb-save") && i + 1 < argc)
            usb_save = argv[++i];
        else {
            fprintf(stderr,
                    "Usage: %s --rom FILE [--seconds N] [--trace NEW.jsonl] [--interpreter] [--devices] [--disk-readonly RAW] [--audio] [--network-shared | --network-socket PATH | --network-user] [--nvram FILE] [--ram-mb 512..2048] [--boot] [--boot-device OF-PATH] [--cd RAW-ISO-OR-RDISK] [--radeon | --radeon-fast] [--vram-mb 128|256] [--cpu 750|7400|7447a] [--usb-disk CLONED-RAW] [--input-script FILE] [--agent-socket PRIVATE-PATH] [--shots DIR SECONDS] [--window] [--of NAME=VALUE]... [--boot-cd] [--disk CLONED-RAW] [--overlay] [--usb-save NEW-RAW]\n",
                    argv[0]);
            return 2;
        }
    }
    if (network_shared + (network_socket != NULL) + network_user_enabled > 1) {
        fprintf(stderr, "Choose one network backend\n");
        return 2;
    }
    const char *netflag = getenv("PT_DEV_NET");
    network_enabled = (network_shared || network_socket || network_user_enabled) && (!netflag || !strcmp(netflag, "1"));
    if ((network_shared || network_socket || network_user_enabled) && !network_enabled)
        fprintf(stderr, "network: disabled by PT_DEV_NET\n");
    if (window_mode && !radeon_enabled) {
        radeon_enabled = 1;
        radeon_fast = !cpu970;
    } /* the window always shows the Radeon path */
    if (usb_save && (!overlay || !usb_disk)) {
        fprintf(stderr, "--usb-save needs --overlay and --usb-disk\n");
        return 2;
    }
    if (!rom) {
        fprintf(stderr, "--rom FILE is required\n");
        return 2;
    }
    if (cpu970 && (radeon_fast || network_enabled || product_boot)) {
        fprintf(stderr,
                "970MP bring-up requires trapped --radeon; fast VRAM, network and the product profile are refused\n");
        return 2;
    }
    tail = getenv("PT_TRACE_TAIL") != NULL;
    /* PT_CHECK copies CPU and invokes SPR callbacks for its reference CPU.
     * That is not safe with this asynchronous timer and shared device state. */
    if (env_on("PT_CHECK")) {
        fprintf(stderr, "PT_CHECK is unsupported in the live firmware harness; use the engine regression fixtures\n");
        return 2;
    }
    _Static_assert(sizeof(_Atomic uint32_t) == 4 && ATOMIC_INT_LOCK_FREE == 2,
                   "JIT IRQ ABI requires a lock-free 32-bit cell");
    if (window_mode && !cpu_given)
        cpu_model = 2;
    pt_cpu_fast_defaults(cpus[cpu_model].pvr, window_mode);
    if (window_mode) {
        /* A window always offers the host end of Ptolemy Tools, in a private per-process socket; the Tools agent in
         * Tiger starts on and keeps Stop visible. */
        static char tools_socket[104];
        const char *home = getenv("HOME");
        if (!agent_socket && home &&
            snprintf(tools_socket, sizeof tools_socket, "%s/.ptolemy-agent", home) < (int)sizeof tools_socket) {
            mkdir(tools_socket, 0700);
            size_t n = strlen(tools_socket);
            if (snprintf(tools_socket + n, sizeof tools_socket - n, "/window.%d", (int)getpid()) <
                (int)(sizeof tools_socket - n)) {
                unlink(tools_socket);
                agent_socket = tools_socket;
                agent_default = 1;
            }
        }
        PtStandWindow w = {machine,      &peripherals.input, &stand_stop, &pause_request, &radeon,
                           agent_socket, &pointer_mailbox,   &agent,      media_change,   &hw_cursor};
        int rc = pt_stand_window_main(&w);
        if (rc != PT_EXIT_RESTART)
            return rc;
        /* Restart: machine() has closed disks, NVRAM, devices and the GPU; a fresh process gets a fresh
         * HVF VM, RAM and device state without a re-entry path through this file's one-shot globals. An --overlay
         * disk lives only in this process's memory, so restarting would discard the guest's writes: stop instead. */
        if (overlay) {
            fprintf(stderr, "Restart: not available with --overlay (guest disk writes are only in memory); stopped\n");
            return rc;
        }
        const char *policy =
                getenv("PT_RESTART"); /* exit: a launcher continues itself (e.g. after the Tiger installer) */
        if (policy && !strcmp(policy, "exit")) {
            fprintf(stderr, "Restart: exit %d as PT_RESTART=exit asks\n", rc);
            return rc;
        }
        if (edid_mode)
            setenv("PT_EDID_MODE", edid_mode, 1);
        else
            unsetenv("PT_EDID_MODE"); /* the window may have rewritten it */
        char path[PATH_MAX];
        uint32_t size = sizeof path;
        if (!_NSGetExecutablePath(path, &size)) {
            fprintf(stderr, "Restart: re-executing %s\n", path);
            fflush(NULL);
            execv(path, argv);
        }
        perror("Restart");
        return rc;
    }
    return machine();
}
/* The vCPU belongs to the thread that creates it: everything from here runs on the machine thread. */
static int machine(void) {
    print_config();
    CPU *c = pt_hv_init(1);
    uint8_t *ram = mmap(0, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ram == MAP_FAILED || mprotect(ram, guest_ram_bytes, PROT_READ | PROT_WRITE) ||
        mprotect(ram + 0xfff00000, 0x100000, PROT_READ | PROT_WRITE))
        fatal("RAM");
    load_rom(ram, rom);
    pt_nvram_init(&nvram);
    if (nvram_path) {
        int e = pt_nvram_open(&nvram, nvram_path);
        if (e) {
            errno = e;
            fatal("NVRAM open");
        }
    }
    {
        const char *flash = getenv("PT_NVRAM_FLASH");
        unsigned changes = 0;
        int e = pt_nvram_prepare(&nvram, !flash || strcmp(flash, "0"), &changes);
        if (e) {
            fprintf(stderr,
                    "nvram: preparation failed; the original file was not changed. Check %s and its .legacy backup (%s)\n",
                    nvram_path ? nvram_path : "in-memory NVRAM", strerror(e));
            errno = e;
            fatal("NVRAM preparation");
        }
        if (changes & PT_NVRAM_BACKUP)
            fprintf(stderr, "nvram: verified pre-migration backup: %s.legacy\n", nvram_path);
        if (changes & PT_NVRAM_REPAIRED)
            fprintf(stderr, "nvram: the Open Firmware partitions were damaged; rebuilt with defaults\n");
        if (changes & PT_NVRAM_UUID_ADDED)
            fprintf(stderr, "nvram: platform-uuid added to the guest's common partition\n");
    }
    if (product_boot || boot_device) {
        int e = pt_board_boot(&nvram, product_boot, boot_device);
        if (e) {
            errno = e;
            fatal("boot configuration");
        }
    }
    if (radeon_enabled) {
        /* First the display NDRV (fw_cfg PT_FW_BLOB) into the screen node: Tiger's display driver then has a
         * hardware cursor (guest/ndrv). */
        /* The boot-command, in Open Firmware's Forth:
         *   ptb ( -- byte )      read one byte from the fw_cfg data port (0xf0000512)
         *   ptn ( -- addr len )  select fw_cfg item 0x8010 (the display driver), read its 32-bit little-endian
         *                        length, then that many bytes into newly allocated memory
         *   ptd ( -- )           attach those bytes to the "screen" node as driver,AAPL,MacOS,PowerPC
         * then run ptd, give the G4 board's root node its "compatible" list (PowerBook5,8, MacRISC, MacRISC2,
         * Power Macintosh) and boot. */
        static const char prefix[] =
                ": ptb h# f0000512 c@ ; "
                ": ptn h# 8010 h# f0000510 w! "
                "ptb ptb h# 8 lshift or ptb h# 10 lshift or ptb h# 18 lshift or "
                "dup alloc-mem swap 2dup bounds ?do ptb i c! loop ; "
                ": ptd ptn \" screen\" find-package if >r encode-bytes \" driver,AAPL,MacOS,PowerPC\" r> encode-property "
                "else 2drop then ; "
                "ptd ";
        static const char g4_root[] = "\" /\" find-device "
                                      "\" PowerBook5,8\" encode-string \" MacRISC\" encode-string encode+ "
                                      "\" MacRISC2\" encode-string encode+ \" Power Macintosh\" encode-string encode+ "
                                      "\" compatible\" property device-end boot";
        char command[1536];
        snprintf(command, sizeof command, "%s%s", prefix, cpu970 ? "boot" : g4_root);
        int e = pt_nvram_set_of(&nvram, "boot-command", command);
        if (e) {
            errno = e;
            fatal("Radeon boot command");
        }
    }
    for (unsigned i = 0; i < n_of_vars; i++) {
        char name[64];
        const char *eq = strchr(of_vars[i], '=');
        size_t len = (size_t)(eq - of_vars[i]);
        if (len >= sizeof name) {
            errno = EINVAL;
            fatal("--of");
        }
        memcpy(name, of_vars[i], len);
        name[len] = 0;
        int e = pt_nvram_set_of(&nvram, name, eq + 1);
        if (e) {
            errno = e;
            fatal("--of");
        }
    }
    fprintf(stderr, "RAM: %u MiB; profile: %s\n", ram_mb, product_boot ? "PowerBook5,8" : "firmware console");
    pci_init();
    pt_pic_init(&pic);
    pt_fw_init(&fw, guest_ram_bytes);
    bus_init();
    if (cpu970) {
        fw.data[0][6][0] = 3;
        pt_write_le(fw.data[1][4], 4, 2000000000);
    }
    if (radeon_enabled) {
        fw.data[0][4][0] = fw.data[0][4][1] = 0; /* FW_CFG_NOGRAPHIC=0: OpenBIOS initialises the display */
        fw.blob = display_ndrv;
        fw.blob_len = sizeof display_ndrv;
    }
    if (boot_cd)
        fw.data[0][12][0] = 'd'; /* FW_CFG_BOOT_DEVICE: OpenBIOS boot-device = cd */
    if (devices_enabled)
        add_usb_region();
    if (radeon_enabled)
        add_radeon_regions();
    if (network_enabled)
        add_network_regions();
    pt_event_init(&wake);
    pt_event_init(&alarm_event);
    if (network_enabled) {
        uint8_t mac[6];
        int error = 0;
        PtNetIo transport;
        if (network_user_enabled) {
            network_user = pt_user_net_open(&wake, &c->irq, mac, &error);
            transport = pt_user_net_io(network_user);
        } else {
            network_host = network_socket ? pt_net_host_socket(network_socket, &wake, &c->irq, mac, &error)
                                          : pt_net_host_open(&wake, &c->irq, mac, &error);
            transport = pt_net_host_io(network_host);
        }
        if (!network_host && !network_user) {
            fprintf(stderr, "network: backend unavailable (status %d)\n", error);
            return 1;
        }
        if (network_user)
            fprintf(stderr, "network: rootless NAT (DHCP, DNS/UDP, outbound TCP, ping)\n");
        {
            const char *v = getenv("PT_WEB"); /* PT_WEB=0: the guest's port 80 goes straight out */
            if (network_user && !(v && !strcmp(v, "0"))) {
                uint16_t port = pt_web_start();
                pt_user_net_web(network_user, port);
                fprintf(stderr, "network: web gateway for old browsers %s\n",
                        port ? "on (http:// fetched over HTTPS by the Mac, pages rewritten)" : "unavailable");
            }
        }
        ram_hooks.opaque = c;
        pt_net_rtl_init(&network, (PtRam){ram, guest_ram_bytes, &ram_hooks}, mac, transport, network_irq, NULL);
        network.clock = now_ns;
        fprintf(stderr, "network: RTL8139, slot 15, IRQ 30, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1],
                mac[2], mac[3], mac[4], mac[5]);
    }
    const PtBootMode *bm = pt_boot_mode(getenv("PT_EDID_MODE"));
    unsigned boot_w = bm ? bm->width : 1024, boot_h = bm ? bm->height : 768; /* = fw_cfg (pt_fw_init) */
    if (radeon_enabled) {
        PtRadeonConfig r = {.ram = {ram, guest_ram_bytes, NULL},
                            .wake = &wake,
                            .kick = &c->irq,
                            .irq = radeon_irq,
                            .width = boot_w,
                            .height = boot_h,
                            .vram_mb = radeon_memory.vram >> 20,
                            .direct_map = radeon_fast ? radeon_direct_map : NULL,
                            .map_opaque = c,
                            .ram_written = radeon_ram_written,
                            .vram_protect = radeon_vram_protect,
                            .vram_guard = radeon_vram_guard,
                            /* The CP on its own thread; diagnostic switch OPTICS_CP_THREAD=0 runs it on this thread */
                            .cp_thread =
                                    !cpu970 && (!getenv("OPTICS_CP_THREAD") || atoi(getenv("OPTICS_CP_THREAD")) != 0)};
        int e = pt_radeon_create(&radeon, &r);
        if (e) {
            errno = e;
            fatal("Radeon initialization");
        }
        radeon_configure();
        gpu_at = now_ns();
    }
    if (devices_enabled) {
        ram_hooks.opaque = c;
        int e = pt_devices_init(&peripherals, (PtRam){ram, guest_ram_bytes, &ram_hooks}, &bus, &pic, &wake, &c->irq,
                                disk, audio, now_ns(), (uint32_t)time(NULL));
        if (e) {
            errno = e;
            fatal("device initialization");
        }
        if (disk_rw) { /* a clone (cp -c / clonefile), or any raw with --overlay */
            if (disk) {
                fprintf(stderr, "--disk and --disk-readonly are exclusive\n");
                exit(2);
            }
            PtBlock *b = pt_block_open(disk_rw, overlay ? PT_BLOCK_OVERLAY : 1, &wake);
            if (!b)
                fatal("writable disk");
            if ((e = pt_devices_disk(&peripherals, b, &c->irq))) {
                errno = e;
                fatal("writable disk");
            }
        }
        if (usb_disk) {
            PtBlock *b = pt_block_open(usb_disk, overlay ? PT_BLOCK_OVERLAY : 1, &wake), *old = NULL;
            if (!b)
                fatal("USB disk clone");
            if (usb_save && (e = pt_block_set_save(b, usb_save))) {
                errno = e;
                fatal("--usb-save");
            }
            pt_block_set_kick(b, &c->irq);
            if ((e = pt_devices_storage(&peripherals, b, 0, &old))) {
                pt_block_close(b);
                errno = e;
                fatal("USB storage");
            }
            if (old)
                pt_block_close(old);
        }
        if (cd_path) {
            PtBlock *cd = pt_block_open_optical(cd_path, &wake);
            if (!cd)
                fatal("optical backend");
            pt_block_set_kick(cd, &c->irq);
            PtBlock *old = NULL;
            if ((e = pt_devices_optical(&peripherals, cd, &old))) {
                errno = e;
                fatal("optical insert");
            }
            peripherals.optical.unit_attention = 0; /* initial medium precedes enumeration */
        }
    }
    pt_clock_init(&clock_model, now_ns());
    sync_fast_clock(c);
    end_ns = seconds ? now_ns() + (uint64_t)seconds * 1000000000 : UINT64_MAX;
    pt_pic_set_notify(&pic, kick, c);
    atomic_init(&alarm_ns, pt_vtime_host(&vtime, end_ns));
    {
        struct sigaction sa = {0};
        sa.sa_handler = stop_signal;
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
        sa.sa_handler = pause_signal;
        sigaction(SIGUSR1, &sa, NULL);
        sigaction(SIGUSR2, &sa, NULL);
    }
    c->mem = ram;
    c->sys = 2;
    c->pc = 0xfff00100;
    c->msr = MSR_IP;
    c->spr[287] = cpus[cpu_model].pvr;
    c->spr[1017] = cpus[cpu_model].l2cr;
    for (int i = 0; i < 8; i++)
        c->cr[i] = cr_make(0, 0, 0, 0);
    c->preg[0] = (PRegion){0, guest_ram_bytes, ram, 0};
    c->preg[1] = (PRegion){0xfff00000, 0x4000, ram + 0xfff00000, 1};
    c->preg[2] = (PRegion){0xfff08000, 0xf8000, ram + 0xfff08000, 1};
    c->npreg = 3;
    for (int i = 0; i < c->npreg; i++)
        pt_hv_map(c->preg[i].host, c->preg[i].base, c->preg[i].size, c->preg[i].read_only);
    if (shot_dir)
        shot_at = now_ns() + shot_period;
    if (script) {
        script_t0 = now_ns();
        script_next();
    }
    c->env_mmio = mmio;
    c->env_spr = spr;
    c->env_irq = irq;
    c->env_idle = idle;
#ifdef PT_ENABLE_970
    Pt970Bus *bus970 = NULL;
    if (cpu970) {
        Pt970Region regions[3];
        for (unsigned i = 0; i < 3; i++)
            regions[i] = (Pt970Region){c->preg[i].base, c->preg[i].size, c->preg[i].host, c->preg[i].read_only};
        bus970 = ppc970_bus_create(regions, 3);
        c->env_ticks970 = ticks970;
        c->env_irq = irq970;
        c->env_mmio970 = mmio970;
        c->env_ram970 = radeon_ram970;
        if (!bus970 || ppc970_attach(c, bus970, 0, 0xfff00000)) {
            errno = EINVAL;
            fatal("970MP reference bus");
        }
        fprintf(stderr, "970MP: single-owner U3 bring-up, 64-bit reset at %016llx\n", (unsigned long long)ppc_pc64(c));
    }
#endif
    if (agent_socket) {
        agent = pt_agent_open(agent_socket);
        if (!agent && !agent_default)
            fatal("development agent socket");
        if (!agent)
            fprintf(stderr, "Ptolemy Tools: host end unavailable at %s (%s)\n", agent_socket, strerror(errno));
    }
    if (agent || radeon_enabled)
        c->env_hypercall = private_call;
    pthread_t thread;
    if (pthread_create(&thread, 0, ticker, c))
        fatal("timer thread");
    pt_stand_window_ready();
    /* PT_QOS=1: the CPU thread asks for the user-interactive class, so macOS keeps it on performance cores;
     * Half-Life runs without it sometimes drop 8-17%. =0 or unset: unchanged. */
    if (getenv("PT_QOS") && atoi(getenv("PT_QOS")))
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (interpreter)
        while (!c->halted) {
            interp_step(c);
            if (!c->is970 && atomic_exchange(&c->irq, 0))
                ppc_irq(c);
        }
    else
        jit_run(c);
    atomic_store(&stop_timer, 1);
    pt_event_signal(&alarm_event);
    pthread_join(thread, 0);
#ifdef PT_ENABLE_970
    if (bus970)
        fprintf(stderr, "970MP stopped: PC=%016llx MSR=%016llx instructions=%llu exception=%x unsupported=%08x\n",
                (unsigned long long)ppc_pc64(c), (unsigned long long)ppc_msr64(c), (unsigned long long)c->icount,
                c->exception970, c->unsupported970);
#endif
    pt_stand_window_detach();
    if (network_user) {
        pt_user_net_close(network_user);
        network_user = NULL;
        network_enabled = 0;
    }
    if (network_host) {
        int error = pt_net_host_close(network_host);
        network_host = NULL;
        network_enabled = 0;
        if (error) {
            fprintf(stderr, "network: close failed (%d)\n", error);
            c->exit_code = 1;
        }
    }
    if (ppc_has_vec(c))
        ppc_altivec_stats();
    fprintf(stderr, "\nStopped: pc=%08x; %s; disk=%s\n", c->pc,
            devices_enabled ? "experimental peripherals" : "firmware baseline",
            disk      ? "read-only raw"
            : disk_rw ? (overlay ? "memory overlay" : "writable clone")
                      : "none");
    if (hw_cursor.gamma_gen)
        fprintf(stderr, "display: %u gamma ramps, %u not the identity\n", hw_cursor.gamma_gen, gamma_curves);
    if (shot_dir)
        shot();
    int nvram_close_error = pt_nvram_close(&nvram);
    if (nvram_close_error || nvram_error) {
        fprintf(stderr, "NVRAM persistence error: %s\n", strerror(nvram_close_error ? nvram_close_error : nvram_error));
        c->exit_code = 1;
    }
    c->env_hypercall = NULL;
    if (agent) {
        pt_agent_close(agent);
        agent = NULL;
    }
    if (radeon) {
        pt_radeon_close(radeon);
        radeon = NULL;
    }
    if (devices_enabled && pt_devices_close(&peripherals))
        c->exit_code = 1;
#ifdef PT_ENABLE_970
    /* GPU/DMA retirement callbacks still need the bus while devices close. */
    if (bus970)
        ppc970_bus_destroy(bus970);
#endif
    if (env_on("PT_STATS"))
        pt_hv_stats();
    if (trace && tail)
        for (uint64_t i = seq > TAIL ? seq - TAIL : 0; i < seq; i++)
            trace_line(&ring[i % TAIL]);
    if (trace) {
        fprintf(stderr, "MMIO capture: %llu written, %llu omitted after limit\n", (unsigned long long)trace_written,
                (unsigned long long)(seq - trace_written));
        if (fclose(trace))
            fatal("trace close");
    }
    pt_pic_destroy(&pic);
    pt_event_destroy(&wake);
    pt_event_destroy(&alarm_event);
    return c->exit_code;
}
