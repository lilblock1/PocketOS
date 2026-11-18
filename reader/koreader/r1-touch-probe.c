/* Read-only evdev discovery for the KOReader R1 port.
 * Never writes to a device node, flashes partitions or grabs an event stream.
 * Exit 1 on missing/ambiguous devices rather than guessing event indexes.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static int plausible(int fd) {
    struct input_absinfo x = {0}, y = {0};
    unsigned char keys[(KEY_MAX + 8) / 8] = {0};
    int mt = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x) == 0 &&
             ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y) == 0;
    if (!mt) {
        if (ioctl(fd, EVIOCGABS(ABS_X), &x) != 0 ||
            ioctl(fd, EVIOCGABS(ABS_Y), &y) != 0) return 0;
        /* X/Y joystick input alone must never be accepted as a touchscreen. */
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return 0;
        if (!(keys[BTN_TOUCH / 8] & (1U << (BTN_TOUCH % 8)))) return 0;
    }
    return x.maximum > x.minimum && y.maximum > y.minimum;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        puts("PocketOS KOReader R1 touch probe v1");
        return 0;
    }
    if (argc != 1) return 2;
    char result[48] = "";
    int count = 0;
    for (int i = 0; i < 32; ++i) {
        char node[48];
        if (snprintf(node, sizeof(node), "/dev/input/event%d", i) >=
            (int)sizeof(node)) return 3;
        struct stat st;
        if (lstat(node, &st) < 0 || !S_ISCHR(st.st_mode)) continue;
        int fd = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        if (plausible(fd)) {
            ++count;
            if (count == 1) strcpy(result, node);
        }
        close(fd);
    }
    if (count != 1) {
        fprintf(stderr, "R1: found %d compatible touch devices; refusing to guess\n", count);
        return 1;
    }
    puts(result);
    return 0;
}
