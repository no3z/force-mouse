/**
 * evdump - print the events of an input device (evtest is not installed on the Force).
 *
 *   evdump /dev/input/eventN [seconds]      default 5 seconds
 *
 * Useful to see what the addon's "Virtual Mouse Touch" device emits: it only reads, any number
 * of readers may watch the same node.
 */
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char* abs_name(int code)
{
    switch (code) {
    case ABS_X: return "ABS_X";
    case ABS_Y: return "ABS_Y";
    case ABS_MT_SLOT: return "ABS_MT_SLOT";
    case ABS_MT_TRACKING_ID: return "ABS_MT_TRACKING_ID";
    case ABS_MT_POSITION_X: return "ABS_MT_POSITION_X";
    case ABS_MT_POSITION_Y: return "ABS_MT_POSITION_Y";
    default: return NULL;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: evdump /dev/input/eventN [seconds]\n");
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 5;
    int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }

    char name[128] = "?";
    ioctl(fd, EVIOCGNAME(sizeof name), name);
    printf("%s: %s, %d s\n", argv[1], name, seconds);
    fflush(stdout);

    time_t end = time(NULL) + seconds;
    int frames = 0;
    while (time(NULL) < end) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 200) <= 0)
            continue;
        struct input_event ev;
        while (read(fd, &ev, sizeof ev) == sizeof ev) {
            if (ev.type == EV_SYN) {
                printf("  -- sync %d\n", ++frames);
            } else if (ev.type == EV_ABS) {
                const char* n = abs_name(ev.code);
                if (n)
                    printf("  %s %d\n", n, ev.value);
                else
                    printf("  EV_ABS code=%d %d\n", ev.code, ev.value);
            } else if (ev.type == EV_KEY) {
                printf("  EV_KEY code=%d %d%s\n", ev.code, ev.value, ev.code == BTN_TOUCH ? " (BTN_TOUCH)" : "");
            } else if (ev.type == EV_REL) {
                printf("  EV_REL code=%d %d\n", ev.code, ev.value);
            }
        }
        fflush(stdout);
    }
    printf("%d frame(s)\n", frames);
    return 0;
}
