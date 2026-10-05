/**
 * @file input_probe.h
 * Find evdev nodes by what they are instead of by their /dev/input/eventN number.
 *
 * The event numbers follow probe order, so they shift whenever a device is missing
 * at boot (typically the touch controller). With the touchscreen present the Force
 * has: event0 touch, event1 gpio-keys, event2 mouse, event3 "Amit's Input Provider".
 * Without it: event0 gpio-keys, event1 mouse, event2 "Amit's Input Provider".
 */
#ifndef FORCE_INPUT_PROBE_H
#define FORCE_INPUT_PROBE_H

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PROBE_MAX_EVENT_NODES 32
#define PROBE_NAME_LEN 128
#define PROBE_NAME_MOUSE_TOUCH "Virtual Mouse Touch" // created by force_cursor.c
#define PROBE_NAME_KEYBOARD "Amit's Input Provider" // created by the MidiLoop addon

#define PROBE_BIT(arr, bit) (((arr)[(bit) / 8] >> ((bit) % 8)) & 1)

// ev_type 0 asks for the supported event types, otherwise for the codes of ev_type
static inline int probe_has_bit(int fd, int ev_type, int code)
{
    unsigned char bits[KEY_MAX / 8 + 1];
    memset(bits, 0, sizeof bits);
    if (ioctl(fd, EVIOCGBIT(ev_type, sizeof bits), bits) < 0)
        return 0;
    return PROBE_BIT(bits, code);
}

// Relative pointer with a left button. Ignores the virtual devices on the Force.
static inline int probe_is_mouse(int fd, const char* name)
{
    if (!strcmp(name, PROBE_NAME_KEYBOARD) || !strcmp(name, PROBE_NAME_MOUSE_TOUCH))
        return 0;
    return probe_has_bit(fd, 0, EV_REL) && probe_has_bit(fd, EV_REL, REL_X)
        && probe_has_bit(fd, EV_REL, REL_Y) && probe_has_bit(fd, EV_KEY, BTN_LEFT);
}

// Multi-touch screen (slot protocol). Our own single-touch uinput device does not qualify.
static inline int probe_is_touchscreen(int fd, const char* name)
{
    if (!strcmp(name, PROBE_NAME_MOUSE_TOUCH))
        return 0;
    return probe_has_bit(fd, EV_ABS, ABS_MT_POSITION_X) && probe_has_bit(fd, EV_ABS, ABS_MT_POSITION_Y);
}

typedef int (*probe_match_fn)(int fd, const char* name, const void* arg);

static inline int probe_match_mouse(int fd, const char* name, const void* arg)
{
    (void)arg;
    return probe_is_mouse(fd, name);
}

static inline int probe_match_touchscreen(int fd, const char* name, const void* arg)
{
    (void)arg;
    return probe_is_touchscreen(fd, name);
}

static inline int probe_match_name(int fd, const char* name, const void* arg)
{
    (void)fd;
    return strcmp(name, (const char*)arg) == 0;
}

// First /dev/input/eventN accepted by match(). Returns 0 and fills path/name, or -1.
static inline int probe_find(probe_match_fn match, const void* arg, char* path, size_t path_len,
    char* name_out, size_t name_len)
{
    for (int i = 0; i < PROBE_MAX_EVENT_NODES; i++) {
        char candidate[32];
        char name[PROBE_NAME_LEN] = "";
        snprintf(candidate, sizeof candidate, "/dev/input/event%d", i);
        int fd = open(candidate, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
            name[0] = '\0';
        int ok = match(fd, name, arg);
        close(fd);
        if (ok) {
            snprintf(path, path_len, "%s", candidate);
            if (name_out)
                snprintf(name_out, name_len, "%s", name);
            return 0;
        }
    }
    return -1;
}

#endif /* FORCE_INPUT_PROBE_H */
