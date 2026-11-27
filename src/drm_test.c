#define _GNU_SOURCE
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258
#endif

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

/* Minimal 8x8 ASCII bitmap font for key printable chars (32..126) */
static const uint8_t font8x8_basic[96][8] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!' - 32] = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    ['-' - 32] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['.' - 32] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/' - 32] = {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    [':' - 32] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    ['0' - 32] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1' - 32] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2' - 32] = {0x3C,0x66,0x06,0x1C,0x30,0x60,0x7E,0x00},
    ['3' - 32] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4' - 32] = {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    ['5' - 32] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6' - 32] = {0x3C,0x60,0x7C,0x66,0x66,0x66,0x3C,0x00},
    ['7' - 32] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8' - 32] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9' - 32] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    ['A' - 32] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    ['B' - 32] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    ['C' - 32] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['D' - 32] = {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    ['E' - 32] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    ['F' - 32] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    ['G' - 32] = {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},
    ['H' - 32] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    ['I' - 32] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['K' - 32] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    ['L' - 32] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    ['M' - 32] = {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    ['N' - 32] = {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    ['O' - 32] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['P' - 32] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    ['R' - 32] = {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
    ['S' - 32] = {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    ['T' - 32] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['U' - 32] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['V' - 32] = {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    ['W' - 32] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    ['X' - 32] = {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    ['Y' - 32] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    ['x' - 32] = {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
};

static void draw_char(uint32_t *fb, uint32_t pitch_px, uint32_t w, uint32_t h,
                      int x, int y, int scale, char ch, uint32_t fg, uint32_t bg) {
    if (ch < 32 || ch > 126) ch = ' ';
    const uint8_t *g = font8x8_basic[ch - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t c = (g[row] & (0x80 >> col)) ? fg : bg;
            for (int sy = 0; sy < scale; sy++) {
                int py = y + row * scale + sy;
                if (py < 0 || py >= (int)h) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    if (px < 0 || px >= (int)w) continue;
                    fb[py * pitch_px + px] = c;
                }
            }
        }
    }
}

static void draw_text(uint32_t *fb, uint32_t pitch_px, uint32_t w, uint32_t h,
                      int x, int y, int scale, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        draw_char(fb, pitch_px, w, h, x, y, scale, *s, fg, bg);
        x += 8 * scale;
        s++;
    }
}

static void draw_circle(uint32_t *fb, uint32_t pitch_px, uint32_t w, uint32_t h,
                        int cx, int cy, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= (int)h) continue;
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                int px = cx + dx;
                if (px >= 0 && px < (int)w) fb[py * pitch_px + px] = col;
            }
        }
    }
}

static void draw_test_pattern(uint32_t *fb, uint32_t pitch_px, uint32_t w, uint32_t h) {
    static const uint32_t bars[7] = {
        0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
        0xFFFF00FF, 0xFFFF0000, 0xFF0000FF
    };
    uint32_t top_h = (h * 2) / 3;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = fb + y * pitch_px;
        for (uint32_t x = 0; x < w; x++) {
            if (y < 8 || y >= h - 8 || x < 8 || x >= w - 8) {
                row[x] = 0xFFFFFFFF;
            } else if (y < top_h) {
                row[x] = bars[(x * 7) / w];
            } else {
                uint8_t r = (x * 255) / w;
                uint8_t g = ((y - top_h) * 255) / (h - top_h + 1);
                uint8_t b = 128;
                row[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "PIXEL 7 LIVE POC %ux%u", w, h);
    draw_text(fb, pitch_px, w, h, 40, h / 2 - 40, 4, buf, 0xFFFFFFFF, 0xFF000000);
    draw_text(fb, pitch_px, w, h, 40, h / 2 + 20, 4, "DRM/KMS OK - TOUCH TEST", 0xFF00FF00, 0xFF000000);
}

int main(int argc, char **argv) {
    const char *card = argc > 1 ? argv[1] : "/dev/dri/card0";
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", card, strerror(errno));
        return 1;
    }

    if (xioctl(fd, DRM_IOCTL_SET_MASTER, NULL) < 0) {
        fprintf(stderr, "DRM_IOCTL_SET_MASTER warn: %s\n", strerror(errno));
    }

    struct drm_set_client_cap cap = { .capability = 2 /* UNIVERSAL_PLANES */, .value = 1 };
    xioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

    struct drm_mode_card_res res = {0};
    if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "GETRESOURCES failed: %s\n", strerror(errno));
        return 1;
    }
    printf("DRM: crtcs=%u connectors=%u encoders=%u fbs=%u\n",
           res.count_crtcs, res.count_connectors, res.count_encoders, res.count_fbs);

    uint32_t *crtcs = calloc(res.count_crtcs ? res.count_crtcs : 1, sizeof(uint32_t));
    uint32_t *conns = calloc(res.count_connectors ? res.count_connectors : 1, sizeof(uint32_t));
    uint32_t *encs  = calloc(res.count_encoders ? res.count_encoders : 1, sizeof(uint32_t));
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
    if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "GETRESOURCES(2) failed: %s\n", strerror(errno));
        return 1;
    }

    uint32_t chosen_conn = 0, chosen_crtc = 0;
    struct drm_mode_modeinfo chosen_mode = {0};

    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector conn = { .connector_id = conns[i] };
        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        printf("Connector %u: type=%u conn=%u modes=%u enc=%u\n",
               conn.connector_id, conn.connector_type, conn.connection,
               conn.count_modes, conn.encoder_id);
        if (conn.count_modes == 0) continue;

        struct drm_mode_modeinfo *modes = calloc(conn.count_modes, sizeof(*modes));
        uint32_t *c_encs = calloc(conn.count_encoders ? conn.count_encoders : 1, sizeof(uint32_t));
        uint32_t *props = calloc(conn.count_props ? conn.count_props : 1, sizeof(uint32_t));
        uint64_t *propv = calloc(conn.count_props ? conn.count_props : 1, sizeof(uint64_t));
        conn.modes_ptr = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)c_encs;
        conn.props_ptr = (uint64_t)(uintptr_t)props;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)propv;

        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0 && conn.connection == 1 && conn.count_modes > 0) {
            chosen_conn = conn.connector_id;
            chosen_mode = modes[0];
            for (uint32_t m = 0; m < conn.count_modes; m++) {
                if (modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                    chosen_mode = modes[m];
                    break;
                }
            }
            if (conn.encoder_id) {
                struct drm_mode_get_encoder enc = { .encoder_id = conn.encoder_id };
                if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id)
                    chosen_crtc = enc.crtc_id;
            }
            if (!chosen_crtc && conn.count_encoders > 0) {
                struct drm_mode_get_encoder enc = { .encoder_id = c_encs[0] };
                if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                    for (uint32_t c = 0; c < res.count_crtcs; c++) {
                        if (enc.possible_crtcs & (1u << c)) { chosen_crtc = crtcs[c]; break; }
                    }
                }
            }
            if (!chosen_crtc && res.count_crtcs > 0) chosen_crtc = crtcs[0];
        }
        free(modes); free(c_encs); free(props); free(propv);
        if (chosen_conn) break;
    }

    if (!chosen_conn || !chosen_crtc) {
        fprintf(stderr, "No connected DRM connector/CRTC found\n");
        return 1;
    }
    printf("Using conn=%u crtc=%u mode=%ux%u@%u\n",
           chosen_conn, chosen_crtc, chosen_mode.hdisplay, chosen_mode.vdisplay, chosen_mode.vrefresh);

    struct drm_mode_create_dumb dumb = {
        .width = chosen_mode.hdisplay,
        .height = chosen_mode.vdisplay,
        .bpp = 32,
    };
    if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) < 0) {
        fprintf(stderr, "CREATE_DUMB failed: %s\n", strerror(errno));
        return 1;
    }

    struct drm_mode_fb_cmd fb_cmd = {
        .width = dumb.width,
        .height = dumb.height,
        .pitch = dumb.pitch,
        .bpp = 32,
        .depth = 24,
        .handle = dumb.handle,
    };
    if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) {
        fprintf(stderr, "ADDFB failed: %s\n", strerror(errno));
        return 1;
    }

    struct drm_mode_map_dumb map_req = { .handle = dumb.handle };
    if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        fprintf(stderr, "MAP_DUMB failed: %s\n", strerror(errno));
        return 1;
    }

    uint32_t *fb = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_req.offset);
    if (fb == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        return 1;
    }

    uint32_t pitch_px = dumb.pitch / 4;
    draw_test_pattern(fb, pitch_px, dumb.width, dumb.height);
    msync(fb, dumb.size, MS_SYNC);

    struct drm_mode_crtc crtc = {
        .set_connectors_ptr = (uint64_t)(uintptr_t)&chosen_conn,
        .count_connectors = 1,
        .crtc_id = chosen_crtc,
        .fb_id = fb_cmd.fb_id,
        .x = 0,
        .y = 0,
        .mode_valid = 1,
        .mode = chosen_mode,
    };
    if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
        fprintf(stderr, "SETCRTC failed: %s\n", strerror(errno));
        return 1;
    }
    printf("SETCRTC succeeded! fb_id=%u\n", fb_cmd.fb_id);
    fflush(stdout);

    /* Open all /dev/input/event* devices for live touch feedback */
    struct pollfd pfds[16];
    int nfds = 0;
    int abs_max_x = dumb.width, abs_max_y = dumb.height;
    for (int i = 0; i < 16 && nfds < 16; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int ifd = open(path, O_RDONLY | O_NONBLOCK);
        if (ifd >= 0) {
            char name[128] = "unknown";
            ioctl(ifd, EVIOCGNAME(sizeof(name)), name);
            struct input_absinfo aix = {0}, aiy = {0};
            if (ioctl(ifd, EVIOCGABS(ABS_MT_POSITION_X), &aix) == 0 && aix.maximum > 0)
                abs_max_x = aix.maximum;
            if (ioctl(ifd, EVIOCGABS(ABS_MT_POSITION_Y), &aiy) == 0 && aiy.maximum > 0)
                abs_max_y = aiy.maximum;
            printf("Input %s: %s (maxX=%d maxY=%d)\n", path, name, abs_max_x, abs_max_y);
            pfds[nfds].fd = ifd;
            pfds[nfds].events = POLLIN;
            nfds++;
        }
    }
    fflush(stdout);

    int tx = -1, ty = -1;
    while (1) {
        if (nfds == 0) { sleep(10); continue; }
        int pr = poll(pfds, nfds, 5000);
        if (pr <= 0) continue;
        for (int i = 0; i < nfds; i++) {
            if (!(pfds[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(pfds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                        tx = (int)(((int64_t)ev.value * dumb.width) / (abs_max_x ? abs_max_x : 1));
                    else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                        ty = (int)(((int64_t)ev.value * dumb.height) / (abs_max_y ? abs_max_y : 1));
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && tx >= 0 && ty >= 0) {
                    draw_circle(fb, pitch_px, dumb.width, dumb.height, tx, ty, 18, 0xFFFF2222);
                    /* Refresh FB via DIRTYFB or page flip if panel is command-mode (DSI CMD) */
                    struct drm_mode_fb_dirty_cmd dirty = { .fb_id = fb_cmd.fb_id };
                    xioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
                }
            }
        }
    }
    return 0;
}
