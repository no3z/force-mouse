/**
 * probe_inputs - list every /dev/input/eventN with its capabilities and show which
 * node force_cursor.c would use for the mouse, the touchscreen and key injection.
 * Run it on the Force (as root) before and after a reboot to see how the numbers move.
 */
#include "../src/input_probe.h"

#include <errno.h>
#include <stdio.h>

static void show_selection(const char* role, probe_match_fn match, const void* arg)
{
    char path[32], name[PROBE_NAME_LEN];
    if (probe_find(match, arg, path, sizeof path, name, sizeof name) == 0)
        printf("  %-12s -> %s (%s)\n", role, path, name);
    else
        printf("  %-12s -> NOT FOUND\n", role);
}

int main(void)
{
    printf("Input event nodes:\n");
    for (int i = 0; i < PROBE_MAX_EVENT_NODES; i++) {
        char path[32];
        char name[PROBE_NAME_LEN] = "";
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT)
                printf("  %-20s cannot open (errno=%d)\n", path, errno);
            continue;
        }
        if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
            strcpy(name, "?");
        printf("  %-20s %-34s rel=%d btn_left=%d abs_mt=%d key=%d\n", path, name,
            probe_has_bit(fd, EV_REL, REL_X), probe_has_bit(fd, EV_KEY, BTN_LEFT),
            probe_has_bit(fd, EV_ABS, ABS_MT_POSITION_X), probe_has_bit(fd, 0, EV_KEY));
        close(fd);
    }

    printf("\nSelection used by libforce_cursor.so:\n");
    show_selection("mouse", probe_match_mouse, NULL);
    show_selection("touchscreen", probe_match_touchscreen, NULL);
    show_selection("keyboard", probe_match_name, PROBE_NAME_KEYBOARD);
    return 0;
}
