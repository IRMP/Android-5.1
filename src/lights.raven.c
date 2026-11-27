#define LOG_TAG "lights.raven"
#include "bionic_compat.h"
#include <hardware/hardware.h>
#include <hardware/lights.h>

typedef struct raven_light_dev {
    struct light_device_t dev;
    char bl_path[256];
    int max_bl;
} raven_light_dev_t;

static pthread_mutex_t g_lights_lock = PTHREAD_MUTEX_INITIALIZER;

static int read_sys_int(const char *path, int def) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return def;
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return def;
    buf[n] = '\0';
    int v = (int)strtol(buf, NULL, 10);
    return v > 0 ? v : def;
}

static int set_light_backlight(struct light_device_t *dev, struct light_state_t const *state) {
    raven_light_dev_t *ld = (raven_light_dev_t *)dev;
    if (!state || ld->bl_path[0] == '\0') return 0;

    unsigned int c = state->color;
    int bri = (int)(((77u * ((c >> 16) & 0xffu)) +
                     (150u * ((c >> 8) & 0xffu)) +
                     (29u * (c & 0xffu))) >> 8);
    int hw_val = (bri * ld->max_bl) / 255;
    if (bri > 0 && hw_val == 0) hw_val = 1;

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", hw_val);
    pthread_mutex_lock(&g_lights_lock);
    int fd = open(ld->bl_path, O_WRONLY);
    if (fd >= 0) {
        write(fd, buf, (size_t)len);
        close(fd);
    }
    pthread_mutex_unlock(&g_lights_lock);
    return 0;
}

static int set_light_noop(struct light_device_t *dev, struct light_state_t const *state) {
    (void)dev; (void)state;
    return 0;
}

static int lights_close(struct hw_device_t *dev) {
    free(dev);
    return 0;
}

static int lights_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    raven_light_dev_t *ld = (raven_light_dev_t *)calloc(1, sizeof(raven_light_dev_t));
    if (!ld) return -ENOMEM;
    ld->dev.common.tag = HARDWARE_DEVICE_TAG;
    ld->dev.common.version = 0;
    ld->dev.common.module = (struct hw_module_t *)module;
    ld->dev.common.close = lights_close;

    if (strcmp(name, LIGHT_ID_BACKLIGHT) == 0) {
        ld->dev.set_light = set_light_backlight;
        ld->max_bl = 4095;
        DIR *d = opendir("/sys/class/backlight");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                snprintf(ld->bl_path, sizeof(ld->bl_path), "/sys/class/backlight/%s/brightness", de->d_name);
                char maxp[256];
                snprintf(maxp, sizeof(maxp), "/sys/class/backlight/%s/max_brightness", de->d_name);
                ld->max_bl = read_sys_int(maxp, 4095);
                break;
            }
            closedir(d);
        }
        ALOGI("lights.raven backlight=%s max=%d", ld->bl_path, ld->max_bl);
    } else {
        ld->dev.set_light = set_light_noop;
    }
    *device = &ld->dev.common;
    return 0;
}

static struct hw_module_methods_t lights_methods = {
    .open = lights_open,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = 1,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Pixel 6 Pro Backlight Lights HAL",
    .author = "IRMP",
    .methods = &lights_methods,
};
