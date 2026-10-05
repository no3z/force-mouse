/**
 * fake_mouse - a virtual relative mouse for testing the addon without touching hardware.
 * It stays alive and obeys one-line commands read from a FIFO:
 *
 *   move DX DY      relative motion
 *   click           left button down and up
 *   down | up       left button
 *   rclick          right button down and up
 *   wheel N         wheel ticks (positive = up)
 *   quit
 *
 * Usage: fake_mouse [fifo]            (default /tmp/fake_mouse.cmd)
 * Then, for example:  echo "move 100 0" > /tmp/fake_mouse.cmd
 * Point device.txt at it with the first line  name:Fake Test Mouse
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int out;

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(out, &ev, sizeof ev);
}

static void sync_report(void) { emit(EV_SYN, SYN_REPORT, 0); }

static void button(int code, int value)
{
    emit(EV_KEY, code, value);
    sync_report();
}

int main(int argc, char** argv)
{
    const char* fifo = argc > 1 ? argv[1] : "/tmp/fake_mouse.cmd";

    out = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (out < 0) {
        perror("/dev/uinput");
        return 1;
    }
    ioctl(out, UI_SET_EVBIT, EV_KEY);
    ioctl(out, UI_SET_EVBIT, EV_REL);
    ioctl(out, UI_SET_EVBIT, EV_SYN);
    ioctl(out, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(out, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(out, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(out, UI_SET_RELBIT, REL_X);
    ioctl(out, UI_SET_RELBIT, REL_Y);
    ioctl(out, UI_SET_RELBIT, REL_WHEEL);

    struct uinput_user_dev dev;
    memset(&dev, 0, sizeof dev);
    snprintf(dev.name, UINPUT_MAX_NAME_SIZE, "Fake Test Mouse");
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x1234;
    dev.id.product = 0x5678;
    if (write(out, &dev, sizeof dev) < 0 || ioctl(out, UI_DEV_CREATE) < 0) {
        perror("create");
        return 1;
    }

    unlink(fifo);
    if (mkfifo(fifo, 0600) < 0) {
        perror(fifo);
        return 1;
    }
    int in = open(fifo, O_RDWR); // O_RDWR keeps the FIFO open between writers
    FILE* f = fdopen(in, "r");
    printf("fake mouse ready, commands via %s\n", fifo);
    fflush(stdout);

    char line[128];
    while (fgets(line, sizeof line, f)) {
        int a = 0, b = 0;
        if (sscanf(line, "move %d %d", &a, &b) == 2) {
            if (a)
                emit(EV_REL, REL_X, a);
            if (b)
                emit(EV_REL, REL_Y, b);
            sync_report();
        } else if (!strncmp(line, "click", 5)) {
            button(BTN_LEFT, 1);
            usleep(60000);
            button(BTN_LEFT, 0);
        } else if (!strncmp(line, "rclick", 6)) {
            button(BTN_RIGHT, 1);
            usleep(60000);
            button(BTN_RIGHT, 0);
        } else if (!strncmp(line, "down", 4)) {
            button(BTN_LEFT, 1);
        } else if (!strncmp(line, "up", 2)) {
            button(BTN_LEFT, 0);
        } else if (sscanf(line, "wheel %d", &a) == 1) {
            emit(EV_REL, REL_WHEEL, a);
            sync_report();
        } else if (!strncmp(line, "quit", 4)) {
            break;
        }
    }
    ioctl(out, UI_DEV_DESTROY);
    unlink(fifo);
    return 0;
}
