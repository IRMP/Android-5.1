#define LOG_TAG "power.raven"
#include "bionic_compat.h"
#include <hardware/hardware.h>
#include <hardware/power.h>

static pthread_mutex_t g_pwr_lock = PTHREAD_MUTEX_INITIALIZER;

static int write_sysfs(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    size_t len = 0;
    while (val[len]) len++;
    ssize_t r = write(fd, val, len);
    close(fd);
    return (r > 0) ? 0 : -1;
}

static void apply_cpufreq_gov(const char *gov) {
    DIR *d = opendir("/sys/devices/system/cpu/cpufreq");
    if (!d) return;
    struct dirent *de;
    char p[256];
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "policy", 6) != 0) continue;
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", de->d_name);
        write_sysfs(p, gov);
    }
    closedir(d);
}

static void apply_devfreq_gov(const char *gov) {
    DIR *d = opendir("/sys/class/devfreq");
    if (!d) return;
    struct dirent *de;
    char p[256];
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        snprintf(p, sizeof(p), "/sys/class/devfreq/%s/governor", de->d_name);
        write_sysfs(p, gov);
    }
    closedir(d);
}

static void raven_power_init(struct power_module *module) {
    (void)module;
    pthread_mutex_lock(&g_pwr_lock);
    apply_cpufreq_gov("performance");
    apply_devfreq_gov("performance");
    pthread_mutex_unlock(&g_pwr_lock);
    ALOGI("power.raven initialized: performance governor across GS101 clusters + devfreq");
}

static void raven_set_interactive(struct power_module *module, int on) {
    (void)module;
    pthread_mutex_lock(&g_pwr_lock);
    if (on) {
        apply_cpufreq_gov("performance");
        apply_devfreq_gov("performance");
    } else {
        apply_cpufreq_gov("schedutil");
    }
    pthread_mutex_unlock(&g_pwr_lock);
}

static void raven_power_hint(struct power_module *module, power_hint_t hint, void *data) {
    (void)module;
    if (hint == POWER_HINT_LOW_POWER) {
        int low = data ? (*(int *)data) : 0;
        pthread_mutex_lock(&g_pwr_lock);
        apply_cpufreq_gov(low ? "schedutil" : "performance");
        pthread_mutex_unlock(&g_pwr_lock);
    } else if (hint == POWER_HINT_INTERACTION || hint == POWER_HINT_VSYNC) {
        /* Keep max clocks pinned during UI frames & touch */
    }
}

static void raven_set_feature(struct power_module *module, feature_t feature, int state) {
    (void)module; (void)feature; (void)state;
}

static int raven_power_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    (void)module; (void)name; (void)device;
    return -EINVAL;
}

static struct hw_module_methods_t power_module_methods = {
    .open = raven_power_open,
};

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_3,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "Pixel 6 Pro Max-Performance Power HAL",
        .author = "IRMP",
        .methods = &power_module_methods,
    },
    .init = raven_power_init,
    .setInteractive = raven_set_interactive,
    .powerHint = raven_power_hint,
    .setFeature = raven_set_feature,
};
