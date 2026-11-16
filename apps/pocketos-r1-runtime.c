/* PocketOS R1 runtime services: read-only hardware discovery and policy.
 * No root requirement, no register/mixer/charger/mount/radio writes.
 * Only an explicitly requested watcher stays resident.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pocketos-hardware.h"

#define MAX_PATH 2048
#define MAX_STATE 4096
static const char *root = "";

static int path_for(char *out, size_t size, const char *suffix)
{
    int n = snprintf(out, size, "%s%s", root, suffix);
    return n >= 0 && (size_t)n < size;
}

static int exists(const char *suffix)
{
    char path[MAX_PATH];
    struct stat st;
    return path_for(path, sizeof(path), suffix) && stat(path, &st) == 0;
}

static int any_match(const char *suffix)
{
    char path[MAX_PATH];
    glob_t g = {0};
    if (!path_for(path, sizeof(path), suffix)) return 0;
    int yes = glob(path, GLOB_NOSORT, NULL, &g) == 0 && g.gl_pathc > 0;
    globfree(&g);
    return yes;
}

/* Only fixed sysfs attributes; reject whitespace/control characters in values
 * except ordinary spaces, and never print raw input events or radio addresses. */
static int read_value(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, (int)size, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    size_t n = strlen(out);
    while (n && isspace((unsigned char)out[n-1])) out[--n] = '\0';
    for (size_t i = 0; i < n; i++)
        if (!isprint((unsigned char)out[i])) return 0;
    return n > 0 && n < size-1;
}

static int read_attr(const char *suffix, char *out, size_t size)
{
    char path[MAX_PATH];
    return path_for(path, sizeof(path), suffix) && read_value(path, out, size);
}

static long number(const char *text, long min, long max)
{
    char *end;
    if (!text || !*text) return -1;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno || *end || n < min || n > max) return -1;
    return n;
}

struct state {
    long percent, brightness, max_brightness;
    int battery_ambiguous, charging, external_power;
    int audio, framebuffer, input, sd_candidate, sd_mounted, wifi, bluetooth, suspend;
    const char *battery_source;
};

static void scan_power(struct state *s)
{
    char path[MAX_PATH], attr[MAX_PATH], value[128];
    glob_t g = {0};
    int online_known = 0, online = 0;
    struct hw_battery battery=hw_battery_read(root);
    s->percent=battery.percent;s->charging=battery.charging;
    s->battery_ambiguous=battery.ambiguous;s->battery_source=battery.source;
    if (!path_for(path, sizeof(path), "/sys/class/power_supply/*")) return;
    if (glob(path, 0, NULL, &g) != 0) { globfree(&g); return; }
    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *p = g.gl_pathv[i];
        if (strlen(p) + 20 >= sizeof(attr)) continue;
        snprintf(attr, sizeof(attr), "%s/type", p);
        if (!read_value(attr, value, sizeof(value))) continue;
        if (strcmp(value, "Battery")) {
            snprintf(attr, sizeof(attr), "%s/online", p);
            if (read_value(attr, value, sizeof(value))) {
                long n = number(value, 0, 1);
                if (n >= 0) { online_known = 1; if (n == 1) online = 1; }
            }
        }
    }
    s->external_power = online_known ? online : -1;
    globfree(&g);
}

static void scan_backlight(struct state *s)
{
    char path[MAX_PATH], attr[MAX_PATH], value[128];
    glob_t g = {0};
    if (!path_for(path, sizeof(path), "/sys/class/backlight/*")) return;
    if (glob(path, 0, NULL, &g) == 0 && g.gl_pathc == 1) {
        const char *p = g.gl_pathv[0];
        if (strlen(p) + 24 < sizeof(attr)) {
            snprintf(attr, sizeof(attr), "%s/max_brightness", p);
            if (read_value(attr, value, sizeof(value))) s->max_brightness = number(value, 1, 1000000);
            snprintf(attr, sizeof(attr), "%s/actual_brightness", p);
            if (!read_value(attr, value, sizeof(value))) {
                snprintf(attr, sizeof(attr), "%s/brightness", p);
                if (!read_value(attr, value, sizeof(value))) value[0] = '\0';
            }
            if (s->max_brightness > 0) s->brightness = number(value, 0, s->max_brightness);
        }
    }
    globfree(&g);
}

static int has_removable_sd(void)
{
    char path[MAX_PATH], attr[MAX_PATH], value[32];
    glob_t g = {0};
    int found = 0;
    if (!path_for(path, sizeof(path), "/sys/block/mmcblk[0-9]*")) return 0;
    if (glob(path, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            const char *p = g.gl_pathv[i];
            if (strlen(p) + 16 >= sizeof(attr)) continue;
            snprintf(attr, sizeof(attr), "%s/removable", p);
            if (read_value(attr, value, sizeof(value)) && !strcmp(value, "1")) {
                snprintf(attr, sizeof(attr), "%s/size", p);
                if (read_value(attr, value, sizeof(value)) && number(value, 1, 2147483647L) > 0) found = 1;
            }
        }
    }
    globfree(&g);
    return found;
}

static struct state snapshot(void)
{
    struct state s = {.percent=-1, .brightness=-1, .max_brightness=-1,
                      .charging=-1, .external_power=-1};
    char value[128];
    scan_power(&s);
    scan_backlight(&s);
    s.audio = any_match("/dev/snd/pcmC*D*p");
    s.framebuffer = exists("/dev/fb0");
    s.input = any_match("/dev/input/event*");
    s.sd_candidate = has_removable_sd();
    s.sd_mounted = hw_sd_mounted(root);
    s.wifi = exists("/sys/class/net/wlan0");
    s.bluetooth = any_match("/sys/class/bluetooth/hci*");
    if (read_attr("/sys/power/state", value, sizeof(value))) {
        char *token = strtok(value, " ");
        while (token) { if (!strcmp(token, "mem")) s.suspend = 1; token = strtok(NULL, " "); }
    }
    return s;
}

static void format_state(const struct state *s, char *out, size_t size)
{
    snprintf(out, size, "audio_pcm_present=%d\nframebuffer_present=%d\ninput_present=%d\n"
             "battery_percent=%ld\nbattery_ambiguous=%d\ncharging=%d\nexternal_power=%d\n"
             "brightness=%ld\nmax_brightness=%ld\nremovable_sd_candidate=%d\n"
             "battery_source=%s\nsd_mounted=%d\nsd_present=%d\n"
             "wifi_interface_present=%d\nbluetooth_interface_present=%d\nsuspend_mem_advertised=%d\n",
             s->audio,s->framebuffer,s->input,s->percent,s->battery_ambiguous,s->charging,
             s->external_power,s->brightness,s->max_brightness,s->sd_candidate,
             s->battery_source?s->battery_source:"unknown",s->sd_mounted,s->sd_mounted||s->sd_candidate,
             s->wifi,s->bluetooth,s->suspend);
}

/* Recommendation only. No changes to playback, screen, radios, or suspend.
 * Caller must report active network transfers, audio, and other critical work.
 * Unknown battery/charging state deliberately blocks suspend recommendations. */
static void policy(struct state s, long idle, int playing, int busy)
{
    int dim = idle >= 60 && s.brightness > 0 && s.max_brightness > 0;
    int suspend = idle >= 300 && !playing && !busy && s.suspend &&
                  s.external_power == 0 && s.charging == 0 && s.percent > 10;
    int saver = s.percent >= 0 && s.percent <= 20 && s.charging == 0 && s.external_power == 0;
    printf("recommend_dim=%d\nrecommend_suspend=%d\nrecommend_saver=%d\nhardware_writes=disabled\n",
           dim, suspend, saver);
}

static int watch(void)
{
    static const char *dirs[] = {"/sys/class/power_supply", "/sys/class/backlight",
        "/sys/class/net", "/sys/class/bluetooth", "/sys/block", "/dev/input", "/dev/snd"};
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd >= 0) for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        char path[MAX_PATH];
        if (path_for(path, sizeof(path), dirs[i]))
            (void)inotify_add_watch(fd, path, IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO|IN_ATTRIB|IN_CLOSE_WRITE);
    }
    char old[MAX_STATE] = "", next[MAX_STATE];
    for (;;) {
        struct state s = snapshot();
        format_state(&s, next, sizeof(next));
        if (strcmp(old, next)) {
            if (fputs(next, stdout) == EOF || fflush(stdout) == EOF) break;
            memcpy(old, next, strlen(next)+1);
        }
        struct pollfd p = {.fd=fd, .events=POLLIN};
        int rc = poll(fd < 0 ? NULL : &p, fd < 0 ? 0 : 1, 60000);
        if (rc < 0 && errno != EINTR) break;
        if (rc > 0) {
            if (!(p.revents & POLLIN) || (p.revents & (POLLERR|POLLHUP|POLLNVAL))) break;
            char discard[4096];
            ssize_t bytes = read(fd, discard, sizeof(discard));
            if (bytes < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                break;
            }
            if (bytes == 0) break;
            /* Debounce hotplug bursts. */
            (void)poll(NULL, 0, 250);
        }
    }
    if (fd >= 0) close(fd);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("PocketOS R1 runtime v1"); return 0; }
    if (argc >= 3 && !strcmp(argv[1], "--root")) {
        root = argv[2];
        if (root[0] != '/' || strlen(root) >= MAX_PATH/2) return 2;
        argc -= 2; argv += 2;
    }
    if (argc == 2 && !strcmp(argv[1], "--status")) {
        char out[MAX_STATE]; struct state s = snapshot();
        format_state(&s, out, sizeof(out)); fputs(out, stdout); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--watch")) return watch();
    if (argc == 5 && !strcmp(argv[1], "--policy")) {
        long idle=number(argv[2],0,864000), playing=number(argv[3],0,1), busy=number(argv[4],0,1);
        if (idle < 0 || playing < 0 || busy < 0) return 2;
        policy(snapshot(),idle,(int)playing,(int)busy); return 0;
    }
    fputs("Usage: pocketos-r1-runtime [--root fixture] --status | --watch | --policy IDLE_SECONDS PLAYING_0_OR_1 BUSY_0_OR_1\n",stderr);
    return 2;
}
