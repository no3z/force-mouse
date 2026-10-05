/**
 * @file force_cursor.c
 * Decription: Mouse Cursor Hack Implementation for Akai Force.
 *
 * * Credits @no3z (Discord)
 * MockbaMid Addon Adaptation: Amit Talwar (@locrian) (Discord)
 * Date: January 2026
 *
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <alsa/asoundlib.h>
#include <dirent.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <time.h>
#include "input_probe.h"  // find input devices by capability/name, not by eventN number
#define MULTIPLIER 1.5

// Output goes to the journal of acvs; flush so no line is lost if MPC dies.
#define LOG(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while (0)

// Cursor state
#include "mouse_cursor.h"  // External cursor design (64x64 RGBA)
static uint32_t cursor_bo = 0;
static uint32_t cursor_pitch = 256;
static int cursor_initialized = 0;
static int saved_fd = -1;
static uint32_t saved_crtc = 0;
static int cursor_x = 50; // Bottom left corner
static int cursor_y = 1230; // Bottom left corner
static pthread_t input_thread;
static int input_running = 0;
static int uinput_fd = -1;        // Virtual multi-touch screen: clicks, drags and pinch gestures
static int keyboard_fd = -1;      // Virtual keyboard for button->key mappings
static volatile int gesture_in_progress = 0;
static volatile int left_button_pressed = 0;  // Track left button state for dragging
char* device = NULL;
static int cursor_rotate = 270;   // CURSOR_ROTATE: bitmap rotation in degrees (0, 90, 180, 270), clockwise in panel space
static int cursor_hot_x = 0;      // where the tip of the arrow is after the rotation
static int cursor_hot_y = 0;
static int grab_mouse = 1;        // EVIOCGRAB the mouse so MPC (libinput) does not also act on it
static int addon_started = 0;

// MIDI sequencer state
static snd_seq_t *midi_seq = NULL;
static int midi_port = -1;

// Button to key/MIDI mappings (max 16 mappings)
#define MAX_BUTTON_MAPPINGS 16
#define MAPPING_TYPE_KEY 0
#define MAPPING_TYPE_MIDI_CC 1

struct button_mapping {
    int button_code;
    int type;           // MAPPING_TYPE_KEY or MAPPING_TYPE_MIDI_CC
    int value;          // key_code for keyboard, CC number for MIDI
};
static struct button_mapping button_mappings[MAX_BUTTON_MAPPINGS];
static int num_button_mappings = 0;

static float rate = 1.0f;
static int read_params_file(const char* path, char** device, float* multiplier);

// Button name to code mapping
struct name_code_pair {
    const char* name;
    int code;
};

static const struct name_code_pair button_names[] = {
    {"BTN_LEFT", BTN_LEFT},
    {"BTN_RIGHT", BTN_RIGHT},
    {"BTN_MIDDLE", BTN_MIDDLE},
    {"BTN_SIDE", BTN_SIDE},
    {"BTN_EXTRA", BTN_EXTRA},
    {"BTN_FORWARD", BTN_FORWARD},
    {"BTN_BACK", BTN_BACK},
    {"BTN_TASK", BTN_TASK},
    {NULL, 0}
};

static const struct name_code_pair key_names[] = {
    {"KEY_ESC", KEY_ESC},
    {"KEY_SPACE", KEY_SPACE},
    {"KEY_ENTER", KEY_ENTER},
    {"KEY_TAB", KEY_TAB},
    {"KEY_BACKSPACE", KEY_BACKSPACE},
    {"KEY_LEFTSHIFT", KEY_LEFTSHIFT},
    {"KEY_RIGHTSHIFT", KEY_RIGHTSHIFT},
    {"KEY_LEFTCTRL", KEY_LEFTCTRL},
    {"KEY_RIGHTCTRL", KEY_RIGHTCTRL},
    {"KEY_LEFTALT", KEY_LEFTALT},
    {"KEY_RIGHTALT", KEY_RIGHTALT},
    {"KEY_UP", KEY_UP},
    {"KEY_DOWN", KEY_DOWN},
    {"KEY_LEFT", KEY_LEFT},
    {"KEY_RIGHT", KEY_RIGHT},
    {"KEY_PAGEUP", KEY_PAGEUP},
    {"KEY_PAGEDOWN", KEY_PAGEDOWN},
    {"KEY_HOME", KEY_HOME},
    {"KEY_END", KEY_END},
    {"KEY_DELETE", KEY_DELETE},
    {"KEY_INSERT", KEY_INSERT},
    {"KEY_F1", KEY_F1},
    {"KEY_F2", KEY_F2},
    {"KEY_F3", KEY_F3},
    {"KEY_F4", KEY_F4},
    {"KEY_F5", KEY_F5},
    {"KEY_F6", KEY_F6},
    {"KEY_F7", KEY_F7},
    {"KEY_F8", KEY_F8},
    {"KEY_F9", KEY_F9},
    {"KEY_F10", KEY_F10},
    {"KEY_F11", KEY_F11},
    {"KEY_F12", KEY_F12},
    {"KEY_MENU", KEY_MENU},
    {NULL, 0}
};

// Parse code from string (name, hex, or decimal)
static int parse_code(const char* str, const struct name_code_pair* table)
{
    // Try hex format (0xNNNN)
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        return (int)strtol(str, NULL, 16);
    }

    // Try plain decimal number
    char* endptr;
    long val = strtol(str, &endptr, 10);
    if (endptr != str && *endptr == '\0' && val >= 0 && val <= 0xFFFF) {
        return (int)val;
    }

    // Try name lookup
    for (int i = 0; table[i].name != NULL; i++) {
        if (strcasecmp(str, table[i].name) == 0) {
            return table[i].code;
        }
    }

    return -1;  // Not found
}

// Rotate the 64x64 cursor bitmap clockwise (in panel space) and follow the tip of the arrow,
// which is pixel (0,0) in mouse_cursor.h. The panel is portrait while MPC draws its landscape
// interface rotated by 90 degrees, so the arrow may need a turn to look upright.
static void rotate_cursor_bitmap(int degrees)
{
    uint32_t src[64 * 64];
    memcpy(src, cursor_data, sizeof(src));
    cursor_hot_x = 0;
    cursor_hot_y = 0;
    if (degrees != 90 && degrees != 180 && degrees != 270) {
        LOG("[BOOT] Cursor bitmap not rotated (CURSOR_ROTATE=%d)\n", degrees);
        return;
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int nx = x, ny = y;
            if (degrees == 90) {
                nx = 63 - y;
                ny = x;
            } else if (degrees == 180) {
                nx = 63 - x;
                ny = 63 - y;
            } else {
                nx = y;
                ny = 63 - x;
            }
            cursor_data[ny * 64 + nx] = src[y * 64 + x];
        }
    }
    cursor_hot_x = degrees == 90 ? 63 : degrees == 180 ? 63 : 0;
    cursor_hot_y = degrees == 90 ? 0 : 63;
    LOG("[BOOT] Cursor bitmap rotated %d degrees, hot spot (%d,%d)\n", degrees, cursor_hot_x, cursor_hot_y);
}

// Initialize bright visible cursor
static void init_cursor(int fd, uint32_t crtcId)
{
    LOG("-------MockbaMod Mouse Cursor --------\n");
    if (read_params_file("/dev/shm/.mouseCursor", &device, &rate) != 0) {
        LOG("*** MockbaMod Mouse Cursor: Failed to read device.txt file *****\n");

        return;
    } else {
        LOG("-------MockbaMod Mouse Cursor --------\n\n    Device: %s\n    Speed Multiplier:%f\n", device, rate);
    }
    if (cursor_initialized)
        return;

    // Cursor data is loaded from mouse_cursor.h (64x64 RGBA with transparency)
    // No need to generate it here - just use the pre-defined cursor_data array
    rotate_cursor_bitmap(cursor_rotate);

    // Create DRM buffer
    struct drm_mode_create_dumb create_req = { 0 };
    create_req.width = 64;
    create_req.height = 64;
    create_req.bpp = 32;

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req) == 0) {
        cursor_bo = create_req.handle;
        cursor_pitch = create_req.pitch;
        saved_fd = fd;
        saved_crtc = crtcId;

        struct drm_mode_map_dumb map_req = { 0 };
        map_req.handle = cursor_bo;

        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) == 0) {
            void* ptr = mmap(0, create_req.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_req.offset);
            if (ptr != MAP_FAILED) {
                memcpy(ptr, cursor_data, sizeof(cursor_data));
                munmap(ptr, create_req.size);
                cursor_initialized = 1;
            }
        }
    }
}

// Virtual multi-touch screen (MT protocol B), created before MPC builds its libinput context.
// MPC reads every input device through libinput, which treats a device with INPUT_PROP_DIRECT
// and ABS_MT axes as a touchscreen. Clicks, drags and pinch gestures therefore reach MPC like
// real finger input, and keep working when the physical touch controller did not load at boot.
static int init_uinput()
{
    struct uinput_user_dev uidev;
    int fd;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    // Enable event types
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);

    // Enable keys and properties
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    // Single-touch axes (mirrored like a real panel) and multi-touch axes
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);

    // Setup device info
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, PROBE_NAME_MOUSE_TOUCH);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x0000;
    uidev.id.product = 0x0000;
    uidev.id.version = 0;

    // Set axis ranges (panel coordinates, portrait)
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = 799;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = 1279;
    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = 799;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = 1279;
    uidev.absmin[ABS_MT_SLOT] = 0;
    uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmin[ABS_MT_TRACKING_ID] = 0;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;

    // Write device
    if (write(fd, &uidev, sizeof(uidev)) < 0) {
        close(fd);
        return -1;
    }

    // Create device
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// Open physical keyboard device for monitoring hardware button codes
static int open_keyboard_monitor()
{
    // Try event3 first (Amit's Input Provider - virtual keyboard that might receive hardware buttons)
    // Then try event1 (gpio-keys - physical GPIO buttons)
    const char* kbd_paths[] = {"/dev/input/event3", "/dev/input/event1", NULL};

    for (int i = 0; kbd_paths[i] != NULL; i++) {
        int fd = open(kbd_paths[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            LOG("[KBD_MONITOR] Opened %s for hardware button monitoring\n", kbd_paths[i]);
            LOG("[KBD_MONITOR] Press any hardware buttons (MENU, SHIFT, etc.) to see their key codes\n");
            fflush(stdout);
            return fd;
        }
    }

    LOG("[KBD_MONITOR] Could not open keyboard device for monitoring\n");
    return -1;
}

// Open "Amit's Input Provider" (event3, or event2 without the touchscreen) for key injection
// MPC already listens to this device, so our keys will be recognized
static int init_uinput_keyboard()
{
    // Instead of creating a new virtual keyboard (which MPC won't listen to),
    // open the existing "Amit's Input Provider" keyboard device that MPC already monitors.
    // This device is created by the midiloop addon and MPC opens it at startup.
    char path[32];
    if (probe_find(probe_match_name, PROBE_NAME_KEYBOARD, path, sizeof path, NULL, 0) != 0) {
        LOG("[KEYBOARD] Device '%s' not found (is the MidiLoop addon running?)\n", PROBE_NAME_KEYBOARD);
        fflush(stdout);
        return -1;
    }
    int fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        LOG("[KEYBOARD] Failed to open %s (Amit's Input Provider) (errno=%d)\n", path, errno);
        fflush(stdout);
        return -1;
    }

    LOG("[KEYBOARD] Opened %s (Amit's Input Provider) for keyboard injection\n", path);
    LOG("[KEYBOARD] MPC is already listening to this device, so button mappings will work!\n");
    fflush(stdout);
    return fd;
}

// Initialize ALSA sequencer for MIDI CC sending
static int init_midi_sequencer()
{
    int err;

    // Open ALSA sequencer
    err = snd_seq_open(&midi_seq, "default", SND_SEQ_OPEN_OUTPUT, 0);
    if (err < 0) {
        LOG("[MIDI] Failed to open ALSA sequencer: %s\n", snd_strerror(err));
        fflush(stdout);
        return -1;
    }

    // Set client name
    snd_seq_set_client_name(midi_seq, "MouseButtonMIDI");

    // Create output port
    midi_port = snd_seq_create_simple_port(midi_seq, "Output",
                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                            SND_SEQ_PORT_TYPE_APPLICATION);
    if (midi_port < 0) {
        LOG("[MIDI] Failed to create MIDI port\n");
        fflush(stdout);
        snd_seq_close(midi_seq);
        midi_seq = NULL;
        return -1;
    }

    LOG("[MIDI] ALSA sequencer initialized, client port: %d\n", midi_port);
    fflush(stdout);
    return 0;
}

// Send MIDI CC message to Mockba Automation In (129:0)
static void send_midi_cc(int cc_number, int value, int pressed)
{
    if (!midi_seq || midi_port < 0) {
        LOG("[MIDI] Sequencer not initialized!\n");
        fflush(stdout);
        return;
    }

    // Only send on button press (not release)
    if (!pressed) {
        return;
    }

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);

    // Set event type to controller (CC)
    snd_seq_ev_set_controller(&ev, 0, cc_number, value);

    // Set source port
    snd_seq_ev_set_source(&ev, midi_port);

    // Set destination: 129:0 (Mockba Automation In)
    snd_seq_ev_set_dest(&ev, 129, 0);

    // Send event directly
    snd_seq_ev_set_direct(&ev);

    // Send the event
    if (snd_seq_event_output(midi_seq, &ev) < 0) {
        LOG("[MIDI] Error sending CC %d\n", cc_number);
        fflush(stdout);
    } else {
        LOG("[MIDI] Sent CC %d (value=%d) to 129:0\n", cc_number, value);
        fflush(stdout);
    }

    snd_seq_drain_output(midi_seq);
}

// Send keyboard key event
static void send_key_event(int fd, int key_code, int pressed)
{
    struct input_event ev[2];
    memset(ev, 0, sizeof(ev));

    // Key press/release
    ev[0].type = EV_KEY;
    ev[0].code = key_code;
    ev[0].value = pressed;

    // Sync
    ev[1].type = EV_SYN;
    ev[1].code = SYN_REPORT;
    ev[1].value = 0;

    write(fd, ev, sizeof(ev));
}

// Check if button has a mapping (returns pointer to mapping struct, or NULL)
static struct button_mapping* get_button_mapping(int button_code)
{
    for (int i = 0; i < num_button_mappings; i++) {
        if (button_mappings[i].button_code == button_code) {
            return &button_mappings[i];
        }
    }
    return NULL;  // No mapping
}

// Send the cursor as finger 1 (slot 0) of the virtual touch screen
static void send_touch_event(int fd, int x, int y, int pressed)
{
    static int finger_down = 0;
    static int next_tracking_id = 100;
    struct input_event ev[8];
    int n = 0;

    if (!pressed && !finger_down) {
        return;
    }
    memset(ev, 0, sizeof(ev));

    ev[n].type = EV_ABS;
    ev[n].code = ABS_MT_SLOT;
    ev[n].value = 0;
    n++;

    if (!finger_down && pressed) {
        // New touch: fresh tracking id, BTN_TOUCH down
        ev[n].type = EV_ABS;
        ev[n].code = ABS_MT_TRACKING_ID;
        ev[n].value = next_tracking_id;
        n++;
        next_tracking_id = next_tracking_id >= 60000 ? 100 : next_tracking_id + 1;

        ev[n].type = EV_KEY;
        ev[n].code = BTN_TOUCH;
        ev[n].value = 1;
        n++;
        finger_down = 1;
    }

    if (pressed) {
        ev[n].type = EV_ABS;
        ev[n].code = ABS_MT_POSITION_X;
        ev[n].value = x;
        n++;
        ev[n].type = EV_ABS;
        ev[n].code = ABS_MT_POSITION_Y;
        ev[n].value = y;
        n++;
        ev[n].type = EV_ABS;
        ev[n].code = ABS_X;
        ev[n].value = x;
        n++;
        ev[n].type = EV_ABS;
        ev[n].code = ABS_Y;
        ev[n].value = y;
        n++;
    } else {
        // Release: tracking id -1, BTN_TOUCH up
        ev[n].type = EV_ABS;
        ev[n].code = ABS_MT_TRACKING_ID;
        ev[n].value = -1;
        n++;
        ev[n].type = EV_KEY;
        ev[n].code = BTN_TOUCH;
        ev[n].value = 0;
        n++;
        finger_down = 0;
    }

    ev[n].type = EV_SYN;
    ev[n].code = SYN_REPORT;
    ev[n].value = 0;
    n++;

    write(fd, ev, n * sizeof(struct input_event));
}

// Send a complete two-finger touch frame (both slots + one sync)
static void send_two_finger_frame(int fd, int x1, int y1, int tracking_id1, int x2, int y2, int tracking_id2, int send_btn_touch)
{
    struct input_event ev[16];
    memset(ev, 0, sizeof(ev));
    int idx = 0;

    // Slot 0
    ev[idx].type = EV_ABS;
    ev[idx].code = ABS_MT_SLOT;
    ev[idx].value = 0;
    idx++;

    ev[idx].type = EV_ABS;
    ev[idx].code = ABS_MT_TRACKING_ID;
    ev[idx].value = tracking_id1;
    idx++;

    if (tracking_id1 >= 0) {
        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_MT_POSITION_X;
        ev[idx].value = x1;
        idx++;

        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_MT_POSITION_Y;
        ev[idx].value = y1;
        idx++;
    }

    // Slot 1
    ev[idx].type = EV_ABS;
    ev[idx].code = ABS_MT_SLOT;
    ev[idx].value = 1;
    idx++;

    ev[idx].type = EV_ABS;
    ev[idx].code = ABS_MT_TRACKING_ID;
    ev[idx].value = tracking_id2;
    idx++;

    if (tracking_id2 >= 0) {
        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_MT_POSITION_X;
        ev[idx].value = x2;
        idx++;

        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_MT_POSITION_Y;
        ev[idx].value = y2;
        idx++;
    }

    // Add BTN_TOUCH (like real touchscreen)
    if (send_btn_touch) {
        ev[idx].type = EV_KEY;
        ev[idx].code = BTN_TOUCH;
        ev[idx].value = (tracking_id1 >= 0) ? 1 : 0;
        idx++;
    }

    // Add single-touch coordinates (first finger position, like real touchscreen)
    if (tracking_id1 >= 0) {
        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_X;
        ev[idx].value = x1;
        idx++;

        ev[idx].type = EV_ABS;
        ev[idx].code = ABS_Y;
        ev[idx].value = y1;
        idx++;
    }

    // ONE sync for both fingers
    ev[idx].type = EV_SYN;
    ev[idx].code = SYN_REPORT;
    ev[idx].value = 0;
    idx++;

    write(fd, ev, idx * sizeof(struct input_event));
}

// Animate pinch gesture (zoom in or out)
static void animate_pinch_gesture(int fd, int center_x, int center_y, int zoom_in)
{
    // Prevent overlapping gestures
    if (gesture_in_progress) {
        return;
    }
    gesture_in_progress = 1;

    // Fast, small diagonal gesture (both horizontal and vertical)
    const int frames = 5;
    const int frame_delay_ms = 16;  // ~60fps

    // Diagonal spacing (both X and Y movement)
    const int min_spacing = 30;     // Fingers close (zoom in start)
    const int max_spacing = 100;    // Fingers apart (diagonal spread)

    // First frame: send BTN_TOUCH
    int first_frame = 1;

    for (int frame = 0; frame < frames; frame++) {
        int spacing;

        // Calculate spacing based on zoom direction
        float progress = (float)frame / (float)(frames - 1);

        if (zoom_in) {
            // Zoom in: fingers start close, move apart diagonally
            spacing = min_spacing + (int)((max_spacing - min_spacing) * progress);
        } else {
            // Zoom out: fingers start far, move together diagonally
            spacing = max_spacing - (int)((max_spacing - min_spacing) * progress);
        }

        // Calculate two touch points (diagonal spread - both X and Y)
        int x1 = center_x - spacing / 2;
        int y1 = center_y - spacing / 2;  // Bottom-left
        int x2 = center_x + spacing / 2;
        int y2 = center_y + spacing / 2;  // Top-right

        // Bounds checking - keep fingers on screen
        if (x1 < 0) x1 = 0;
        if (x1 > 799) x1 = 799;
        if (x2 < 0) x2 = 0;
        if (x2 > 799) x2 = 799;
        if (y1 < 0) y1 = 0;
        if (y1 > 1279) y1 = 1279;
        if (y2 < 0) y2 = 0;
        if (y2 > 1279) y2 = 1279;

        // Use sequential tracking IDs (like real touchscreen)
        static int base_tracking_id = 10;
        int tid1 = base_tracking_id;
        int tid2 = base_tracking_id + 1;

        // Debug output for first frame
        if (first_frame) {
            LOG("[GESTURE] Frame %d: finger1=(%d,%d) finger2=(%d,%d) spacing=%dpx tid=(%d,%d)\n",
                    frame, x1, y1, x2, y2, spacing, tid1, tid2);
            fflush(stdout);
        }

        // Send BOTH fingers in ONE frame (one sync)
        // Only send BTN_TOUCH on first frame
        send_two_finger_frame(fd, x1, y1, tid1, x2, y2, tid2, first_frame);
        first_frame = 0;

        usleep(frame_delay_ms * 1000);
    }

    // Release both touches in ONE frame with BTN_TOUCH=0
    send_two_finger_frame(fd, 0, 0, -1, 0, 0, -1, 1);

    // Increment tracking IDs for next gesture
    static int base_tracking_id = 10;
    base_tracking_id += 2;

    gesture_in_progress = 0;

    // Small delay before allowing next gesture
    usleep(30000); // 30ms
}

// Pick the mouse event node. The first line of device.txt is either a path or "auto".
// A configured path that is not a mouse (the numbers shift when the touch controller
// is missing at boot, so a stale /dev/input/event2 may now be the keyboard provider)
// falls back to auto-detection.
static void resolve_mouse_device(const char* configured, char* out, size_t out_len)
{
    char name[PROBE_NAME_LEN] = "";
    snprintf(out, out_len, "%s", configured);

    if (strncmp(configured, "name:", 5) == 0) {
        // "name:<text>": the mouse whose name contains <text> (several mice, or a stable choice)
        char found[32];
        if (probe_find(probe_match_mouse_named, configured + 5, found, sizeof found, name, sizeof name) == 0) {
            snprintf(out, out_len, "%s", found);
            LOG("[INIT] Mouse matching '%s': %s ('%s')\n", configured + 5, found, name);
            return;
        }
        LOG("[INIT] WARNING: no mouse named '%s', falling back to auto-detection\n", configured + 5);
    } else if (strcasecmp(configured, "auto") != 0) {
        int fd = open(configured, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
                name[0] = '\0';
            int is_mouse = probe_is_mouse(fd, name);
            close(fd);
            if (is_mouse)
                return;
            LOG("[INIT] WARNING: %s ('%s') is not a mouse, falling back to auto-detection\n", configured, name);
        } else {
            LOG("[INIT] WARNING: cannot open %s (errno=%d), falling back to auto-detection\n", configured, errno);
        }
    }

    char found[32];
    if (probe_find(probe_match_mouse, NULL, found, sizeof found, name, sizeof name) == 0) {
        snprintf(out, out_len, "%s", found);
        LOG("[INIT] Auto-detected mouse: %s ('%s')\n", found, name);
    } else {
        LOG("[INIT] No mouse found by auto-detection\n");
    }
}

// Input monitoring thread
static void* input_monitor(void* arg)
{
    (void)arg;

    if (!device) {
        LOG("----------- ERROR no mouse device configured (missing /dev/shm/.mouseCursor?)\n");
        return NULL;
    }
    char mouse_path[64];
    resolve_mouse_device(device, mouse_path, sizeof mouse_path);
    free(device);

    LOG("--------- opening device %s\n", mouse_path);
    int fd = open(mouse_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LOG("----------- ERROR opening device %s for Mouse Events\n", mouse_path);

        return NULL;
    }

    // Take the mouse exclusively: MPC reads every input device through libinput and would
    // otherwise also move its own (invisible) pointer and click on top of our touch events.
    if (grab_mouse) {
        if (ioctl(fd, EVIOCGRAB, 1) == 0) {
            LOG("[INIT] Mouse grabbed, MPC no longer sees it directly\n");
        } else {
            LOG("[INIT] WARNING: could not grab the mouse (errno=%d), MPC may also act on it\n", errno);
        }
    }

    if (uinput_fd >= 0) {
        LOG("[INIT] Virtual touch screen ready, fd=%d\n", uinput_fd);
    } else {
        LOG("[INIT] FAILED: no virtual touch screen (/dev/uinput), clicks and zoom are disabled\n");
    }

    // Initialize devices for button mappings
    if (num_button_mappings > 0) {
        LOG("[INIT] Processing %d button mapping(s)...\n", num_button_mappings);
        fflush(stdout);

        // Check if we need keyboard device (for KEY mappings)
        int has_key_mappings = 0;
        int has_midi_mappings = 0;
        for (int i = 0; i < num_button_mappings; i++) {
            if (button_mappings[i].type == MAPPING_TYPE_KEY) {
                has_key_mappings = 1;
            } else if (button_mappings[i].type == MAPPING_TYPE_MIDI_CC) {
                has_midi_mappings = 1;
            }
        }

        // Initialize keyboard if needed
        if (has_key_mappings) {
            LOG("[INIT] Initializing keyboard device for KEY mappings...\n");
            fflush(stdout);
            keyboard_fd = init_uinput_keyboard();
            if (keyboard_fd >= 0) {
                LOG("[INIT] SUCCESS: Keyboard device ready, fd=%d\n", keyboard_fd);
                fflush(stdout);
            } else {
                LOG("[INIT] FAILED: Could not create keyboard device (errno=%d)\n", errno);
                fflush(stdout);
            }
        }

        // Initialize MIDI sequencer if needed
        if (has_midi_mappings) {
            LOG("[INIT] Initializing MIDI sequencer for MIDI_CC mappings...\n");
            fflush(stdout);
            if (init_midi_sequencer() == 0) {
                LOG("[INIT] SUCCESS: MIDI sequencer ready\n");
                fflush(stdout);
            } else {
                LOG("[INIT] FAILED: Could not initialize MIDI sequencer\n");
                fflush(stdout);
            }
        }
    } else {
        LOG("[INIT] No button mappings configured\n");
        fflush(stdout);
    }

    // Hardware button monitoring disabled (couldn't read from event1)
    // monitor_kbd_fd = open_keyboard_monitor();
    // if (monitor_kbd_fd >= 0) {
    //     LOG("[INIT] Hardware button monitoring ENABLED - press hardware buttons to see their codes\n");
    //     fflush(stdout);
    // }

    struct input_event ev;
    int (*real_drmModeMoveCursor)(int, uint32_t, int, int) = dlsym(RTLD_NEXT, "drmModeMoveCursor");

    while (input_running) {
        // Check for mouse events
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            if (ev.type == EV_REL) {
                // Swap X and Y for portrait display (800x1280), invert Y
                int position_changed = 0;
                if (ev.code == REL_X) {
                    cursor_y -= (ev.value * rate); // Mouse X -> Screen Y (inverted)
                    if (cursor_y < 0)
                        cursor_y = 0;
                    if (cursor_y > 1279)
                        cursor_y = 1279; // Portrait height
                    position_changed = 1;
                } else if (ev.code == REL_Y) {
                    cursor_x += (ev.value * rate); // Mouse Y -> Screen X
                    if (cursor_x < 0)
                        cursor_x = 0;
                    if (cursor_x > 799)
                        cursor_x = 799; // Portrait width
                    position_changed = 1;
                } else if (ev.code == REL_WHEEL) {
                    // Mouse wheel -> pinch gesture (two fingers on the virtual touch screen)
                    LOG("[WHEEL] Detected wheel event: value=%d, uinput_fd=%d, gesture_in_progress=%d\n",
                            ev.value, uinput_fd, gesture_in_progress);
                    fflush(stdout);

                    if (uinput_fd >= 0 && !gesture_in_progress) {
                        if (ev.value > 0) {
                            // Scroll up = zoom in
                            LOG("[WHEEL] Injecting ZOOM IN gesture at (%d, %d)\n", cursor_x, cursor_y);
                            fflush(stdout);
                            animate_pinch_gesture(uinput_fd, cursor_x, cursor_y, 1);
                        } else if (ev.value < 0) {
                            // Scroll down = zoom out
                            LOG("[WHEEL] Injecting ZOOM OUT gesture at (%d, %d)\n", cursor_x, cursor_y);
                            fflush(stdout);
                            animate_pinch_gesture(uinput_fd, cursor_x, cursor_y, 0);
                        }
                    }
                }

                // If left button is pressed and cursor moved, send touch move event
                if (position_changed && left_button_pressed && uinput_fd >= 0) {
                    send_touch_event(uinput_fd, cursor_x, cursor_y, 1);
                }
            } else if (ev.type == EV_KEY) {
                // Mouse button events
                LOG("[DEBUG] EV_KEY: code=%d value=%d\n", ev.code, ev.value);
                fflush(stdout);

                // Check if button has a mapping
                struct button_mapping* mapping = get_button_mapping(ev.code);
                LOG("[DEBUG] Button %d: mapping=%p\n", ev.code, (void*)mapping);
                fflush(stdout);

                if (mapping != NULL) {
                    if (mapping->type == MAPPING_TYPE_KEY && keyboard_fd >= 0) {
                        // Send keyboard event
                        LOG("[BUTTON] Button %d -> Key %d (pressed=%d)\n", ev.code, mapping->value, ev.value);
                        fflush(stdout);
                        send_key_event(keyboard_fd, mapping->value, ev.value);
                    } else if (mapping->type == MAPPING_TYPE_MIDI_CC) {
                        // Send MIDI CC event
                        LOG("[BUTTON] Button %d -> MIDI CC %d (pressed=%d)\n", ev.code, mapping->value, ev.value);
                        fflush(stdout);
                        send_midi_cc(mapping->value, 127, ev.value);
                    }
                } else if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                    // No mapping, send as touch event (default behavior)
                    LOG("[DEBUG] Sending as touch event\n");
                    fflush(stdout);
                    if (uinput_fd >= 0) {
                        send_touch_event(uinput_fd, cursor_x, cursor_y, ev.value);

                        // Track left button state for continuous drag
                        if (ev.code == BTN_LEFT) {
                            left_button_pressed = ev.value;
                            LOG("[DEBUG] Left button %s\n", ev.value ? "PRESSED" : "RELEASED");
                            fflush(stdout);
                        }
                    }
                }
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                // Move cursor after sync
                if (saved_fd >= 0 && real_drmModeMoveCursor) {
                    // The legacy move ioctl takes the position of the image's top-left corner and
                    // ignores the hot spot, unlike the atomic commits, so apply it here too. Without
                    // this the arrow jumped by the hot spot offset whenever MPC committed a frame.
                    real_drmModeMoveCursor(saved_fd, saved_crtc, cursor_x - cursor_hot_x, cursor_y - cursor_hot_y);
                }
            }
        }
        if (n != sizeof(ev)) {
            usleep(1000); // nothing pending: wait 1 ms; otherwise keep draining the queue
        }
    }

    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }
    if (keyboard_fd >= 0) {
        close(keyboard_fd);
    }
    close(fd);
    return NULL;
}

// ---- Atomic cursor -------------------------------------------------------------------------
// MPC 3.9.x drives the display with atomic commits and, on every frame, switches off every
// plane it does not use (FB_ID = 0, CRTC_ID = 0), including the hardware cursor plane. A cursor
// enabled once through the legacy ioctls is therefore wiped at the next frame. So the cursor
// plane's properties are added to MPC's own commit (drmModeAtomicCommit hook below), after its
// entries, which keeps the plane on in the same atomic update.
static uint32_t cursor_plane_id = 0;
static uint32_t cursor_fb_id = 0;
static uint32_t cursor_crtc_id = 0;
static struct {
    uint32_t fb_id, crtc_id, crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h;
} cp;
static volatile int atomic_cursor_ok = 0;
static int atomic_failures = 0;
static int (*real_atomic_add)(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t) = NULL;

// Property id (and current value) of a plane property, by name
static uint32_t plane_property(int fd, uint32_t plane, const char* name, uint64_t* value)
{
    uint32_t id = 0;
    drmModeObjectProperties* props = drmModeObjectGetProperties(fd, plane, DRM_MODE_OBJECT_PLANE);
    for (uint32_t i = 0; props && i < props->count_props && !id; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
        if (prop && strcmp(prop->name, name) == 0) {
            id = prop->prop_id;
            if (value) {
                *value = props->prop_values[i];
            }
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return id;
}

// Find the cursor-type plane usable with this CRTC and wrap our cursor buffer in a framebuffer
static void setup_atomic_cursor(int fd, uint32_t crtcId)
{
    drmModeRes* res = drmModeGetResources(fd);
    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
    int crtc_index = -1;

    for (int i = 0; res && i < res->count_crtcs; i++) {
        if (res->crtcs[i] == crtcId) {
            crtc_index = i;
        }
    }
    for (uint32_t i = 0; planes && crtc_index >= 0 && i < planes->count_planes && !cursor_plane_id; i++) {
        drmModePlane* plane = drmModeGetPlane(fd, planes->planes[i]);
        uint64_t type = 99;
        if (plane && (plane->possible_crtcs & (1u << crtc_index))) {
            int argb = 0;
            plane_property(fd, plane->plane_id, "type", &type);
            for (uint32_t k = 0; k < plane->count_formats; k++) {
                if (plane->formats[k] == DRM_FORMAT_ARGB8888) {
                    argb = 1;
                }
            }
            if (type == 2 /* DRM_PLANE_TYPE_CURSOR */ && argb) {
                cursor_plane_id = plane->plane_id;
            }
        }
        drmModeFreePlane(plane);
    }
    if (planes) {
        drmModeFreePlaneResources(planes);
    }
    if (res) {
        drmModeFreeResources(res);
    }
    if (!cursor_plane_id) {
        LOG("[BOOT] No cursor plane found, only the legacy cursor path is available\n");
        return;
    }

    cp.fb_id = plane_property(fd, cursor_plane_id, "FB_ID", NULL);
    cp.crtc_id = plane_property(fd, cursor_plane_id, "CRTC_ID", NULL);
    cp.crtc_x = plane_property(fd, cursor_plane_id, "CRTC_X", NULL);
    cp.crtc_y = plane_property(fd, cursor_plane_id, "CRTC_Y", NULL);
    cp.crtc_w = plane_property(fd, cursor_plane_id, "CRTC_W", NULL);
    cp.crtc_h = plane_property(fd, cursor_plane_id, "CRTC_H", NULL);
    cp.src_x = plane_property(fd, cursor_plane_id, "SRC_X", NULL);
    cp.src_y = plane_property(fd, cursor_plane_id, "SRC_Y", NULL);
    cp.src_w = plane_property(fd, cursor_plane_id, "SRC_W", NULL);
    cp.src_h = plane_property(fd, cursor_plane_id, "SRC_H", NULL);
    if (!cp.fb_id || !cp.crtc_id || !cp.crtc_x || !cp.crtc_y || !cp.crtc_w || !cp.crtc_h || !cp.src_x || !cp.src_y
        || !cp.src_w || !cp.src_h) {
        LOG("[BOOT] Cursor plane %u lacks a standard property, atomic cursor off\n", cursor_plane_id);
        return;
    }

    uint32_t handles[4] = { cursor_bo, 0, 0, 0 };
    uint32_t pitches[4] = { cursor_pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(fd, 64, 64, DRM_FORMAT_ARGB8888, handles, pitches, offsets, &cursor_fb_id, 0) != 0) {
        LOG("[BOOT] drmModeAddFB2 for the cursor failed (errno=%d), atomic cursor off\n", errno);
        return;
    }
    cursor_crtc_id = crtcId;
    atomic_cursor_ok = 1;
    LOG("[BOOT] Atomic cursor ready: plane %u, fb %u, pitch %u\n", cursor_plane_id, cursor_fb_id, cursor_pitch);
}

static void atomic_add(drmModeAtomicReqPtr req, uint32_t prop, uint64_t value)
{
    if (!real_atomic_add) {
        real_atomic_add = dlsym(RTLD_NEXT, "drmModeAtomicAddProperty");
    }
    if (real_atomic_add) {
        real_atomic_add(req, cursor_plane_id, prop, value);
    }
}

// Show our cursor on the CRTC through the real libdrm. The cursor ioctls need a DRM master
// fd, which is why this runs on the fd MPC itself opened.
static int show_cursor(int fd, uint32_t crtcId)
{
    static int (*real_drmModeSetCursor2)(int, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t) = NULL;
    int (*real_drmModeMoveCursor)(int, uint32_t, int, int);

    if (cursor_bo == 0) {
        return -1;
    }
    if (!real_drmModeSetCursor2) {
        real_drmModeSetCursor2 = dlsym(RTLD_NEXT, "drmModeSetCursor2");
    }
    if (!real_drmModeSetCursor2) {
        return -1;
    }

    int ret = real_drmModeSetCursor2(fd, crtcId, cursor_bo, 64, 64, cursor_hot_x, cursor_hot_y);

    // Initially position the cursor
    real_drmModeMoveCursor = dlsym(RTLD_NEXT, "drmModeMoveCursor");
    if (real_drmModeMoveCursor) {
        real_drmModeMoveCursor(fd, crtcId, cursor_x - cursor_hot_x, cursor_y - cursor_hot_y);
    }
    return ret;
}

// Start the cursor and the input thread, once, from whichever entry point comes first.
static void start_addon(int fd, uint32_t crtcId)
{
    if (!__sync_bool_compare_and_swap(&addon_started, 0, 1)) {
        return;
    }

    init_cursor(fd, crtcId);
    if (cursor_bo != 0) {
        setup_atomic_cursor(fd, crtcId);
    }

    // Start input monitor thread
    if (!input_running) {
        input_running = 1;
        pthread_create(&input_thread, NULL, input_monitor, NULL);
    }

    if (show_cursor(fd, crtcId) == 0) {
        LOG("[BOOT] Cursor shown on crtc %u (drm fd %d)\n", crtcId, fd);
    } else {
        LOG("[BOOT] Could not show the cursor on crtc %u (drm fd %d, errno=%d)\n", crtcId, fd, errno);
    }
}

// Firmware 3.9.x no longer calls drmModeSetCursor*, so the addon cannot wait for MPC to ask
// for the cursor to be hidden. Instead find the DRM device MPC already opened: the fd that
// belongs to a /dev/dri/card* with an active CRTC.
static int find_drm_fd(uint32_t* crtc_out)
{
    DIR* dir = opendir("/proc/self/fd");
    struct dirent* de;
    int found = -1;

    if (!dir) {
        return -1;
    }
    while (found < 0 && (de = readdir(dir)) != NULL) {
        char link[300], target[128];
        int fd = atoi(de->d_name);
        snprintf(link, sizeof link, "/proc/self/fd/%s", de->d_name);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (fd <= 2 || n <= 0) {
            continue;
        }
        target[n] = '\0';
        if (strncmp(target, "/dev/dri/card", 13) != 0) {
            continue;
        }
        drmModeRes* res = drmModeGetResources(fd);
        if (!res) {
            continue;
        }
        for (int i = 0; i < res->count_crtcs && found < 0; i++) {
            drmModeCrtc* crtc = drmModeGetCrtc(fd, res->crtcs[i]);
            if (crtc && crtc->mode_valid) {
                *crtc_out = res->crtcs[i];
                found = fd;
            }
            drmModeFreeCrtc(crtc);
        }
        drmModeFreeResources(res);
    }
    closedir(dir);
    return found;
}

// Crash-loop guard. The library lives inside MPC: if MPC dies shortly after we started, a
// restart with the library would likely die again, and after a few rapid restarts systemd gives
// up (no display, and no touch to recover with). The marker file is created at every start and
// removed after a clean exit or once MPC has run for GUARD_SETTLE_SECONDS. A start that finds a
// marker younger than GUARD_WINDOW_SECONDS leaves the addon off for that run.
#define GUARD_PATH "/dev/shm/.mouseCursor.guard"
#define GUARD_WINDOW_SECONDS 45
#define GUARD_SETTLE_SECONDS 30
static int guard_owner = 0;

static int is_mpc_process(void)
{
    char path[256];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) {
        return 0;
    }
    path[n] = '\0';
    const char* base = strrchr(path, '/');
    return strcmp(base ? base + 1 : path, "MPC") == 0;
}

static int guard_tripped(void)
{
    struct stat st;
    return stat(GUARD_PATH, &st) == 0 && time(NULL) - st.st_mtime < GUARD_WINDOW_SECONDS;
}

static void guard_release(void)
{
    if (guard_owner) {
        unlink(GUARD_PATH);
        guard_owner = 0;
    }
}

static void* bootstrap_thread(void* arg)
{
    (void)arg;
    uint32_t crtc = 0;

    for (int i = 0; i < 600 && !addon_started; i++) { // up to 60 s for MPC to bring the display up
        int fd = find_drm_fd(&crtc);
        if (fd >= 0) {
            start_addon(fd, crtc);
            break;
        }
        usleep(100000);
    }
    if (!addon_started) {
        LOG("[BOOT] No active DRM device found in this process, cursor not started\n");
    }

    sleep(GUARD_SETTLE_SECONDS);
    guard_release();
    return NULL;
}

__attribute__((constructor)) static void force_mouse_init(void)
{
    if (!is_mpc_process()) {
        return; // LD_PRELOAD also reaches every child of MPC
    }
    if (guard_tripped()) {
        LOG("[GUARD] MPC restarted within %d s of the previous start, mouse addon off for this run (remove %s to override)\n",
            GUARD_WINDOW_SECONDS, GUARD_PATH);
        return;
    }
    int g = open(GUARD_PATH, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
    if (g >= 0) {
        close(g);
        guard_owner = 1;
    }

    // The virtual touch screen has to exist before MPC builds its libinput context
    uinput_fd = init_uinput();
    if (uinput_fd >= 0) {
        usleep(300000); // let udev tag it as a touchscreen
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, bootstrap_thread, NULL) == 0) {
        pthread_detach(thread);
    }
}

__attribute__((destructor)) static void force_mouse_exit(void)
{
    guard_release(); // clean exit: the next start is not a crash restart
}

// Hook drmModeSetCursor2 (older firmware: MPC calls it with bo_handle 0 to hide the cursor)
int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle,
    uint32_t width, uint32_t height,
    int32_t hot_x, int32_t hot_y)
{
    static int (*real_drmModeSetCursor2)(int, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t) = NULL;

    if (!real_drmModeSetCursor2) {
        real_drmModeSetCursor2 = dlsym(RTLD_NEXT, "drmModeSetCursor2");
    }

    if (bo_handle == 0) {
        // Show our cursor instead of hiding it
        start_addon(fd, crtcId);
        if (cursor_bo != 0) {
            return show_cursor(fd, crtcId);
        }
        return 0;
    }

    // Pass through other cursor sets
    if (real_drmModeSetCursor2) {
        return real_drmModeSetCursor2(fd, crtcId, bo_handle, width, height, hot_x, hot_y);
    }
    return 0;
}

// Hook drmModeAtomicAddProperty: only to log what MPC writes on the cursor plane, a few times
int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value)
{
    static int logged = 0;

    if (!real_atomic_add) {
        real_atomic_add = dlsym(RTLD_NEXT, "drmModeAtomicAddProperty");
    }
    if (cursor_plane_id && object_id == cursor_plane_id && logged < 24) {
        logged++;
        LOG("[ATOMIC] MPC sets cursor plane %u: property %u = %llu\n", object_id, property_id, (unsigned long long)value);
    }
    return real_atomic_add ? real_atomic_add(req, object_id, property_id, value) : -1;
}

// Hook drmModeAtomicCommit: add the cursor plane to every real commit MPC makes. If the kernel
// rejects the commit but accepts MPC's own request, our plane was the problem: log it and stop
// injecting after a few of those, so a bad cursor can never break MPC's display.
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void* user_data)
{
    static int (*real_drmModeAtomicCommit)(int, drmModeAtomicReqPtr, uint32_t, void*) = NULL;

    if (!real_drmModeAtomicCommit) {
        real_drmModeAtomicCommit = dlsym(RTLD_NEXT, "drmModeAtomicCommit");
    }
    if (!real_drmModeAtomicCommit) {
        return -1;
    }
    if (!atomic_cursor_ok || fd != saved_fd || (flags & DRM_MODE_ATOMIC_TEST_ONLY)) {
        return real_drmModeAtomicCommit(fd, req, flags, user_data);
    }

    int saved = drmModeAtomicGetCursor(req);
    atomic_add(req, cp.fb_id, cursor_fb_id);
    atomic_add(req, cp.crtc_id, cursor_crtc_id);
    atomic_add(req, cp.crtc_x, (uint64_t)(int64_t)(cursor_x - cursor_hot_x));
    atomic_add(req, cp.crtc_y, (uint64_t)(int64_t)(cursor_y - cursor_hot_y));
    atomic_add(req, cp.crtc_w, 64);
    atomic_add(req, cp.crtc_h, 64);
    atomic_add(req, cp.src_x, 0);
    atomic_add(req, cp.src_y, 0);
    atomic_add(req, cp.src_w, 64 << 16);
    atomic_add(req, cp.src_h, 64 << 16);
    int ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
    int err = errno;
    drmModeAtomicSetCursor(req, saved); // leave MPC's request as it was

    if (ret != 0) {
        int retry = real_drmModeAtomicCommit(fd, req, flags, user_data);
        if (retry == 0) {
            LOG("[ATOMIC] Commit with the cursor failed (ret=%d errno=%d) but MPC's own succeeded (%d/3)\n", ret, err,
                atomic_failures + 1);
            if (++atomic_failures >= 3) {
                atomic_cursor_ok = 0;
                LOG("[ATOMIC] Atomic cursor disabled\n");
            }
        }
        return retry;
    }
    return ret;
}

// Hook drmModeSetCursor
int drmModeSetCursor(int fd, uint32_t crtcId, uint32_t bo_handle,
    uint32_t width, uint32_t height)
{
    return drmModeSetCursor2(fd, crtcId, bo_handle, width, height, 0, 0);
}

// Hook drmModeMoveCursor
int drmModeMoveCursor(int fd, uint32_t crtcId, int x, int y)
{
    static int (*real_drmModeMoveCursor)(int, uint32_t, int, int) = NULL;
    static int count = 0;

    if (!real_drmModeMoveCursor) {
        real_drmModeMoveCursor = dlsym(RTLD_NEXT, "drmModeMoveCursor");
    }

    if (count < 3) {
        fprintf(stderr, "[CURSOR_PATCH] MPC moved cursor to %d,%d\n", x, y);
        count++;
    }

    if (real_drmModeMoveCursor) {
        return real_drmModeMoveCursor(fd, crtcId, x, y);
    }
    return 0;
}

static int read_params_file(const char* path, char** out_str, float* out_val)
{
    FILE* fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[256];
    /* ----- first line (string) ----- */
    if (!fgets(line, sizeof line, fp)) { /* no first line? */
        fclose(fp);
        return -1;
    }
    size_t len = strcspn(line, "\r\n"); /* strip newline */
    *out_str = malloc(len + 1);
    if (!*out_str) {
        fclose(fp);
        return -1;
    }
    memcpy(*out_str, line, len);
    (*out_str)[len] = '\0';

    /* ----- second line (float) ----- */
    if (!fgets(line, sizeof line, fp)) {
        *out_val = 1.0f; /* default if missing */
    } else {
        char* endptr;
        float val = strtof(line, &endptr);
        if (endptr == line || val < 0.1f || val > 5.0f)
            val = 1.0f; /* default on parse error or out of range */
        *out_val = val;
    }

    /* ----- additional lines (button mappings) ----- */
    num_button_mappings = 0;
    LOG("[CONFIG] Starting to parse button mappings...\n");
    fflush(stdout);
    while (fgets(line, sizeof line, fp) && num_button_mappings < MAX_BUTTON_MAPPINGS) {
        // Strip newline
        len = strcspn(line, "\r\n");
        line[len] = '\0';

        LOG("[CONFIG] Read line: '%s'\n", line);
        fflush(stdout);

        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') {
            LOG("[CONFIG] Skipping (empty or comment)\n");
            fflush(stdout);
            continue;
        }

        // Parse BUTTON=KEY or BUTTON=MIDI_CC_XXX format
        char* eq = strchr(line, '=');
        if (!eq) {
            continue;  // Invalid format
        }

        *eq = '\0';  // Split at '='
        char* button_str = line;
        char* value_str = eq + 1;

        // Trim whitespace
        while (*button_str == ' ' || *button_str == '\t') button_str++;
        while (*value_str == ' ' || *value_str == '\t') value_str++;

        // Options (not button mappings)
        if (strcasecmp(button_str, "GRAB") == 0) {
            grab_mouse = atoi(value_str) != 0;
            LOG("[CONFIG] GRAB=%d\n", grab_mouse);
            continue;
        }
        if (strcasecmp(button_str, "CURSOR_ROTATE") == 0) {
            cursor_rotate = atoi(value_str);
            LOG("[CONFIG] CURSOR_ROTATE=%d\n", cursor_rotate);
            continue;
        }

        // Parse button code
        int button_code = parse_code(button_str, button_names);
        if (button_code < 0) {
            LOG("[CONFIG] Warning: Invalid button '%s' (skipped)\n", button_str);
            fflush(stdout);
            continue;
        }

        // Check if value is MIDI_CC_XXX format
        if (strncasecmp(value_str, "MIDI_CC_", 8) == 0) {
            // Parse MIDI CC number
            int cc_number = atoi(value_str + 8);
            if (cc_number >= 0 && cc_number <= 127) {
                button_mappings[num_button_mappings].button_code = button_code;
                button_mappings[num_button_mappings].type = MAPPING_TYPE_MIDI_CC;
                button_mappings[num_button_mappings].value = cc_number;
                LOG("[CONFIG] Button mapping: %s (%d) -> MIDI CC %d\n",
                        button_str, button_code, cc_number);
                fflush(stdout);
                num_button_mappings++;
            } else {
                LOG("[CONFIG] Warning: Invalid MIDI CC number '%s' (must be 0-127)\n",
                        value_str);
                fflush(stdout);
            }
        } else {
            // Parse as keyboard key
            int key_code = parse_code(value_str, key_names);
            if (key_code >= 0) {
                button_mappings[num_button_mappings].button_code = button_code;
                button_mappings[num_button_mappings].type = MAPPING_TYPE_KEY;
                button_mappings[num_button_mappings].value = key_code;
                LOG("[CONFIG] Button mapping: %s (%d) -> KEY %s (%d)\n",
                        button_str, button_code, value_str, key_code);
                fflush(stdout);
                num_button_mappings++;
            } else {
                LOG("[CONFIG] Warning: Invalid key '%s' (skipped)\n", value_str);
                fflush(stdout);
            }
        }
    }

    fclose(fp);
    return 0;
}
