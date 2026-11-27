#define _GNU_SOURCE
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/functionfs.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define A_SYNC 0x434e5953
#define A_CNXN 0x4e584e43
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257
#define A_VERSION 0x01000000
#define MAX_PAYLOAD (64 * 1024)

struct amessage {
    uint32_t command;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t data_length;
    uint32_t data_check;
    uint32_t magic;
};

struct ffs_adb_descs {
    struct usb_functionfs_descs_head_v2 header;
    uint32_t fs_count;
    uint32_t hs_count;
    uint32_t ss_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio sink;
        struct usb_endpoint_descriptor_no_audio source;
    } __attribute__((packed)) fs_descs, hs_descs;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio sink;
        struct usb_ss_ep_comp_descriptor sink_comp;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_ss_ep_comp_descriptor source_comp;
    } __attribute__((packed)) ss_descs;
} __attribute__((packed));

static const struct ffs_adb_descs adb_descs = {
    .header = {
        .magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2,
        .length = sizeof(struct ffs_adb_descs),
        .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_HAS_SS_DESC,
    },
    .fs_count = 3,
    .hs_count = 3,
    .ss_count = 5,
    .fs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = 0xff,
            .bInterfaceSubClass = 0x42,
            .bInterfaceProtocol = 0x01,
            .iInterface = 1,
        },
        .sink = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 64,
        },
        .source = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 64,
        },
    },
    .hs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = 0xff,
            .bInterfaceSubClass = 0x42,
            .bInterfaceProtocol = 0x01,
            .iInterface = 1,
        },
        .sink = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 512,
        },
        .source = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 512,
        },
    },
    .ss_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = 0xff,
            .bInterfaceSubClass = 0x42,
            .bInterfaceProtocol = 0x01,
            .iInterface = 1,
        },
        .sink = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 1024,
        },
        .sink_comp = {
            .bLength = sizeof(struct usb_ss_ep_comp_descriptor),
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
            .bMaxBurst = 4,
        },
        .source = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 1024,
        },
        .source_comp = {
            .bLength = sizeof(struct usb_ss_ep_comp_descriptor),
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
            .bMaxBurst = 4,
        },
    },
};

struct ffs_adb_strings {
    struct usb_functionfs_strings_head header;
    uint16_t lang;
    char str[14];
} __attribute__((packed));

static const struct ffs_adb_strings adb_strings = {
    .header = {
        .magic = FUNCTIONFS_STRINGS_MAGIC,
        .length = sizeof(struct ffs_adb_strings),
        .str_count = 1,
        .lang_count = 1,
    },
    .lang = 0x0409,
    .str = "ADB Interface",
};

static int g_ep0 = -1, g_ep1 = -1, g_ep2 = -1;
static volatile int g_usb_enabled = 0;
static pthread_mutex_t g_tx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_stream_lock = PTHREAD_MUTEX_INITIALIZER;

static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) {
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t r = read(fd, p, len);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static uint32_t adb_checksum(const void *data, uint32_t len) {
    const uint8_t *p = data;
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; i++) s += p[i];
    return s;
}

static int send_pkt(uint32_t cmd, uint32_t arg0, uint32_t arg1, const void *data, uint32_t len) {
    struct amessage msg = {
        .command = htole32(cmd),
        .arg0 = htole32(arg0),
        .arg1 = htole32(arg1),
        .data_length = htole32(len),
        .data_check = htole32(len ? adb_checksum(data, len) : 0),
        .magic = htole32(cmd ^ 0xffffffffu),
    };
    pthread_mutex_lock(&g_tx_lock);
    int rc = 0;
    if (g_ep2 < 0 || !g_usb_enabled) rc = -1;
    else {
        rc = write_all(g_ep2, &msg, sizeof(msg));
        if (rc == 0 && len > 0) rc = write_all(g_ep2, data, len);
    }
    pthread_mutex_unlock(&g_tx_lock);
    return rc;
}

#define MAX_STREAMS 64
struct stream {
    int active;
    uint32_t local_id;
    uint32_t remote_id;
    int is_sync;
    int fd_in;
    int fd_out;
    pid_t pid;
    /* sync state */
    int sync_mode; /* 0=header, 1=path, 2=data */
    uint8_t sync_hdr[8];
    size_t sync_hdr_pos;
    char sync_cmd[5];
    uint32_t sync_len;
    char sync_path[512];
    size_t sync_path_pos;
    int sync_file_fd;
    uint32_t sync_mode_bits;
    uint32_t sync_rem;
};

static struct stream g_streams[MAX_STREAMS];
static uint32_t g_next_id = 1;

static struct stream *find_stream(uint32_t local_id) {
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_streams[i].active && g_streams[i].local_id == local_id) return &g_streams[i];
    }
    return NULL;
}

static void close_stream_locked(struct stream *s, int notify_host) {
    if (!s->active) return;
    uint32_t lid = s->local_id, rid = s->remote_id;
    if (s->fd_in >= 0) close(s->fd_in);
    if (s->fd_out >= 0 && s->fd_out != s->fd_in) close(s->fd_out);
    if (s->sync_file_fd >= 0) close(s->sync_file_fd);
    s->fd_in = s->fd_out = s->sync_file_fd = -1;
    if (s->pid > 0) waitpid(s->pid, NULL, WNOHANG);
    s->pid = 0;
    s->active = 0;
    if (notify_host) send_pkt(A_CLSE, lid, rid, NULL, 0);
}

static void *shell_reader_thread(void *arg) {
    uint32_t lid = (uint32_t)(uintptr_t)arg;
    char buf[16384];
    while (1) {
        pthread_mutex_lock(&g_stream_lock);
        struct stream *s = find_stream(lid);
        int fd = s ? s->fd_out : -1;
        uint32_t rid = s ? s->remote_id : 0;
        pthread_mutex_unlock(&g_stream_lock);
        if (fd < 0) break;

        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        if (send_pkt(A_WRTE, lid, rid, buf, (uint32_t)n) < 0) break;
    }
    pthread_mutex_lock(&g_stream_lock);
    struct stream *s = find_stream(lid);
    if (s) close_stream_locked(s, 1);
    pthread_mutex_unlock(&g_stream_lock);
    return NULL;
}

static void sync_send_msg(struct stream *s, const char *id4, uint32_t arg, const void *extra, uint32_t extra_len) {
    uint8_t pkt[8 + 512];
    memcpy(pkt, id4, 4);
    uint32_t v = htole32(arg);
    memcpy(pkt + 4, &v, 4);
    if (extra && extra_len && extra_len <= 512) {
        memcpy(pkt + 8, extra, extra_len);
        send_pkt(A_WRTE, s->local_id, s->remote_id, pkt, 8 + extra_len);
    } else {
        send_pkt(A_WRTE, s->local_id, s->remote_id, pkt, 8);
    }
}

static void handle_sync_data(struct stream *s, const uint8_t *data, uint32_t len) {
    uint32_t pos = 0;
    while (pos < len) {
        if (s->sync_mode == 0) {
            size_t need = 8 - s->sync_hdr_pos;
            size_t take = (len - pos < need) ? (len - pos) : need;
            memcpy(s->sync_hdr + s->sync_hdr_pos, data + pos, take);
            s->sync_hdr_pos += take;
            pos += (uint32_t)take;
            if (s->sync_hdr_pos == 8) {
                memcpy(s->sync_cmd, s->sync_hdr, 4);
                s->sync_cmd[4] = '\0';
                uint32_t v;
                memcpy(&v, s->sync_hdr + 4, 4);
                s->sync_len = le32toh(v);
                s->sync_hdr_pos = 0;
                if (strcmp(s->sync_cmd, "QUIT") == 0) {
                    close_stream_locked(s, 1);
                    return;
                } else if (strcmp(s->sync_cmd, "DATA") == 0) {
                    s->sync_rem = s->sync_len;
                    s->sync_mode = (s->sync_rem > 0) ? 2 : 0;
                } else if (strcmp(s->sync_cmd, "DONE") == 0) {
                    if (s->sync_file_fd >= 0) {
                        fchmod(s->sync_file_fd, s->sync_mode_bits & 0777);
                        close(s->sync_file_fd);
                        s->sync_file_fd = -1;
                        sync_send_msg(s, "OKAY", 0, NULL, 0);
                    } else {
                        sync_send_msg(s, "FAIL", 11, "no open fd", 10);
                    }
                    s->sync_mode = 0;
                } else {
                    s->sync_path_pos = 0;
                    if (s->sync_len >= sizeof(s->sync_path)) s->sync_len = sizeof(s->sync_path) - 1;
                    s->sync_mode = 1;
                }
            }
        } else if (s->sync_mode == 1) {
            size_t need = s->sync_len - s->sync_path_pos;
            size_t take = (len - pos < need) ? (len - pos) : need;
            memcpy(s->sync_path + s->sync_path_pos, data + pos, take);
            s->sync_path_pos += take;
            pos += (uint32_t)take;
            if (s->sync_path_pos == s->sync_len) {
                s->sync_path[s->sync_len] = '\0';
                s->sync_mode = 0;
                if (strcmp(s->sync_cmd, "STAT") == 0) {
                    struct stat st;
                    uint32_t resp[4] = {0};
                    memcpy(resp, "STAT", 4);
                    if (lstat(s->sync_path, &st) == 0) {
                        resp[1] = htole32((uint32_t)st.st_mode);
                        resp[2] = htole32((uint32_t)st.st_size);
                        resp[3] = htole32((uint32_t)st.st_mtime);
                    }
                    send_pkt(A_WRTE, s->local_id, s->remote_id, resp, 16);
                } else if (strcmp(s->sync_cmd, "SEND") == 0) {
                    char *comma = strrchr(s->sync_path, ',');
                    s->sync_mode_bits = 0644;
                    if (comma) {
                        *comma = '\0';
                        s->sync_mode_bits = (uint32_t)strtoul(comma + 1, NULL, 10);
                    }
                    if (s->sync_file_fd >= 0) close(s->sync_file_fd);
                    s->sync_file_fd = open(s->sync_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (s->sync_file_fd < 0) {
                        const char *err = strerror(errno);
                        sync_send_msg(s, "FAIL", (uint32_t)strlen(err), err, (uint32_t)strlen(err));
                    }
                } else if (strcmp(s->sync_cmd, "RECV") == 0) {
                    int rfd = open(s->sync_path, O_RDONLY);
                    if (rfd < 0) {
                        const char *err = strerror(errno);
                        sync_send_msg(s, "FAIL", (uint32_t)strlen(err), err, (uint32_t)strlen(err));
                    } else {
                        uint8_t rbuf[8 + 32768];
                        ssize_t nr;
                        while ((nr = read(rfd, rbuf + 8, 32768)) > 0) {
                            memcpy(rbuf, "DATA", 4);
                            uint32_t l = htole32((uint32_t)nr);
                            memcpy(rbuf + 4, &l, 4);
                            send_pkt(A_WRTE, s->local_id, s->remote_id, rbuf, 8 + (uint32_t)nr);
                        }
                        close(rfd);
                        sync_send_msg(s, "DONE", 0, NULL, 0);
                    }
                } else {
                    sync_send_msg(s, "FAIL", 11, "unsupported", 11);
                }
            }
        } else if (s->sync_mode == 2) {
            uint32_t take = (len - pos < s->sync_rem) ? (len - pos) : s->sync_rem;
            if (s->sync_file_fd >= 0) write_all(s->sync_file_fd, data + pos, take);
            pos += take;
            s->sync_rem -= take;
            if (s->sync_rem == 0) s->sync_mode = 0;
        }
    }
}

static void handle_open(uint32_t remote_id, const char *svc) {
    pthread_mutex_lock(&g_stream_lock);
    struct stream *s = NULL;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!g_streams[i].active) { s = &g_streams[i]; break; }
    }
    if (!s) {
        pthread_mutex_unlock(&g_stream_lock);
        send_pkt(A_CLSE, 0, remote_id, NULL, 0);
        return;
    }
    memset(s, 0, sizeof(*s));
    s->active = 1;
    s->local_id = g_next_id++;
    if (g_next_id == 0) g_next_id = 1;
    s->remote_id = remote_id;
    s->fd_in = s->fd_out = s->sync_file_fd = -1;

    if (strncmp(svc, "sync:", 5) == 0) {
        s->is_sync = 1;
        uint32_t lid = s->local_id;
        pthread_mutex_unlock(&g_stream_lock);
        send_pkt(A_OKAY, lid, remote_id, NULL, 0);
        return;
    }

    if (strncmp(svc, "reboot:", 7) == 0) {
        uint32_t lid = s->local_id;
        pthread_mutex_unlock(&g_stream_lock);
        send_pkt(A_OKAY, lid, remote_id, NULL, 0);
        sync();
        if (strcmp(svc + 7, "bootloader") == 0) syscall(142 /* SYS_reboot */, 0xfee1dead, 672274793, 0xa1b2c3d4, "bootloader");
        else reboot(RB_AUTOBOOT);
        return;
    }

    if (strncmp(svc, "shell:", 6) == 0) {
        const char *cmd = svc + 6;
        int p_in[2], p_out[2];
        int use_pty = 0, master = -1, slave = -1;
        if (*cmd == '\0') {
            if (openpty(&master, &slave, NULL, NULL, NULL) == 0) use_pty = 1;
        }
        if (!use_pty) {
            if (pipe(p_in) < 0 || pipe(p_out) < 0) {
                s->active = 0;
                pthread_mutex_unlock(&g_stream_lock);
                send_pkt(A_CLSE, 0, remote_id, NULL, 0);
                return;
            }
        }
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            if (use_pty) {
                close(master);
                ioctl(slave, TIOCSCTTY, 0);
                dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
                if (slave > 2) close(slave);
            } else {
                close(p_in[1]); close(p_out[0]);
                dup2(p_in[0], 0); dup2(p_out[1], 1); dup2(p_out[1], 2);
                close(p_in[0]); close(p_out[1]);
            }
            setenv("PATH", "/sbin:/system/bin:/system/xbin:/bin:/usr/sbin:/usr/bin", 1);
            setenv("ANDROID_ROOT", "/system", 1);
            setenv("ANDROID_DATA", "/data", 1);
            setenv("LD_LIBRARY_PATH", "/system/lib64:/vendor/lib64", 1);
            setenv("TERM", "xterm-256color", 1);
            const char *sh = (access("/system/bin/sh", X_OK) == 0) ? "/system/bin/sh" : "/bin/sh";
            if (*cmd) execl(sh, "sh", "-c", cmd, NULL);
            else execl(sh, "sh", "-i", NULL);
            _exit(127);
        }
        if (use_pty) {
            close(slave);
            s->fd_in = master;
            s->fd_out = master;
        } else {
            close(p_in[0]); close(p_out[1]);
            s->fd_in = p_in[1];
            s->fd_out = p_out[0];
        }
        s->pid = pid;
        uint32_t lid = s->local_id;
        pthread_mutex_unlock(&g_stream_lock);
        send_pkt(A_OKAY, lid, remote_id, NULL, 0);
        pthread_t th;
        if (pthread_create(&th, NULL, shell_reader_thread, (void *)(uintptr_t)lid) == 0)
            pthread_detach(th);
        return;
    }

    s->active = 0;
    pthread_mutex_unlock(&g_stream_lock);
    send_pkt(A_CLSE, 0, remote_id, NULL, 0);
}

static void *ep0_thread(void *arg) {
    (void)arg;
    struct usb_functionfs_event ev[4];
    while (1) {
        ssize_t n = read(g_ep0, ev, sizeof(ev));
        if (n <= 0) { usleep(50000); continue; }
        int cnt = (int)(n / sizeof(ev[0]));
        for (int i = 0; i < cnt; i++) {
            switch (ev[i].type) {
            case FUNCTIONFS_ENABLE:
                g_usb_enabled = 1;
                break;
            case FUNCTIONFS_DISABLE:
                g_usb_enabled = 0;
                break;
            case FUNCTIONFS_SETUP:
                if (ev[i].u.setup.wLength == 0) {
                    ssize_t ign = read(g_ep0, NULL, 0);
                    (void)ign;
                }
                break;
            default:
                break;
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    g_ep0 = open("/dev/usb-ffs/adb/ep0", O_RDWR);
    if (g_ep0 < 0) { perror("open ep0"); return 1; }
    if (write_all(g_ep0, &adb_descs, sizeof(adb_descs)) < 0) { perror("write descs"); return 1; }
    if (write_all(g_ep0, &adb_strings, sizeof(adb_strings)) < 0) { perror("write strings"); return 1; }

    g_ep1 = open("/dev/usb-ffs/adb/ep1", O_RDWR);
    g_ep2 = open("/dev/usb-ffs/adb/ep2", O_RDWR);
    if (g_ep1 < 0 || g_ep2 < 0) { perror("open ep1/ep2"); return 1; }

    if (argc >= 3) {
        /* Bind UDC after descriptors are written */
        int ufd = open(argv[1], O_WRONLY);
        if (ufd >= 0) {
            write_all(ufd, argv[2], strlen(argv[2]));
            close(ufd);
        }
    }

    pthread_t th0;
    pthread_create(&th0, NULL, ep0_thread, NULL);
    pthread_detach(th0);

    uint8_t *payload = malloc(MAX_PAYLOAD + 1);
    struct amessage msg;
    while (1) {
        if (!g_usb_enabled) { usleep(50000); continue; }
        if (read_all(g_ep1, &msg, sizeof(msg)) < 0) {
            usleep(50000);
            continue;
        }
        uint32_t cmd = le32toh(msg.command);
        uint32_t arg0 = le32toh(msg.arg0);
        uint32_t arg1 = le32toh(msg.arg1);
        uint32_t len = le32toh(msg.data_length);
        if (len > MAX_PAYLOAD) continue;
        if (len > 0 && read_all(g_ep1, payload, len) < 0) continue;
        payload[len] = '\0';

        if (cmd == A_CNXN) {
            pthread_mutex_lock(&g_stream_lock);
            for (int i = 0; i < MAX_STREAMS; i++) close_stream_locked(&g_streams[i], 0);
            pthread_mutex_unlock(&g_stream_lock);
            const char *banner = "device::ro.product.name=panther;ro.product.model=Pixel 7;ro.product.device=panther;";
            send_pkt(A_CNXN, A_VERSION, MAX_PAYLOAD, banner, (uint32_t)strlen(banner) + 1);
        } else if (cmd == A_OPEN) {
            handle_open(arg0, (const char *)payload);
        } else if (cmd == A_WRTE) {
            pthread_mutex_lock(&g_stream_lock);
            struct stream *s = find_stream(arg1);
            if (s) {
                if (s->is_sync) {
                    handle_sync_data(s, payload, len);
                } else if (s->fd_in >= 0 && len > 0) {
                    write_all(s->fd_in, payload, len);
                }
                uint32_t lid = s->local_id, rid = s->remote_id;
                int still = s->active;
                pthread_mutex_unlock(&g_stream_lock);
                if (still) send_pkt(A_OKAY, lid, rid, NULL, 0);
            } else {
                pthread_mutex_unlock(&g_stream_lock);
            }
        } else if (cmd == A_CLSE) {
            pthread_mutex_lock(&g_stream_lock);
            struct stream *s = find_stream(arg1);
            if (s) close_stream_locked(s, 0);
            pthread_mutex_unlock(&g_stream_lock);
        }
    }
    return 0;
}
