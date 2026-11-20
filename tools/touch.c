/*
 * Inject touches and key presses on a Sailfish device or emulator, for UI tests.
 * Runs ON the device as root. The emulators have no python, so this is plain C.
 *
 * Build for the emulator (static, no SDK needed):  gcc -m32 -static -O2 -o touch-i486 tools/touch.c
 * For the phone, inside the SDK (build-init a scratch dir first):
 *   sfdk -c target=SailfishOS-5.1.0.11-aarch64 build-shell gcc -O2 -o touch-aarch64 touch.c
 * Copy and run (with the device locked by sfdev):
 *   sfdev scp emu1 touch-i486 :/tmp/touch
 *   sfdev ssh --root emu1 -- /tmp/touch tap 360 800
 *
 * Coordinates are portrait screen pixels (Jolla C2 and its emulator: 720 x 1600).
 *   tap X Y                  tap
 *   hold X Y [MS]            long press (default 900 ms)
 *   drag X1 Y1 X2 Y2 [MS]    press, move, release (default 300 ms)
 *   pinch X Y D1 D2 [MS]     two fingers around (X, Y), horizontal distance D1 -> D2
 *   type TEXT                type ASCII text on a virtual USB keyboard (US layout; \n = Enter)
 *   typefile PATH            type the contents of a file (no quoting trouble over ssh)
 *   key NAME...              keys and combos: enter backspace tab esc up down left right
 *                            home end pgup pgdn delete space, ctrl+c, alt+x, shift+tab
 */
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define SCREEN_W 720
#define SCREEN_H 1600

static int touch_fd = -1, tracking = 100;


static void emit(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    gettimeofday(&ev.time, NULL);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != sizeof ev)
        perror("write");
}

static void syn(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

/* A virtual multitouch screen through /dev/uinput, the same size as the display. The
 * emulators have no touchscreen of their own, and this way pinch works there too.
 * Uses the legacy uinput_user_dev setup, which the older SDK kernel headers have. */
static void open_touchscreen(void)
{
    struct uinput_user_dev dev;
    touch_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (touch_fd < 0) {
        perror("/dev/uinput");
        exit(1);
    }
    ioctl(touch_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(touch_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(touch_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(touch_fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
    ioctl(touch_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    memset(&dev, 0, sizeof dev);
    strcpy(dev.name, "pocketssh-test-touchscreen");
    dev.id.bustype = BUS_VIRTUAL;
    dev.id.vendor = 0x1234;
    dev.id.product = 0x5679;
    const struct { int code, max; } axes[] = {
        { ABS_X, SCREEN_W - 1 }, { ABS_Y, SCREEN_H - 1 }, { ABS_MT_SLOT, 9 },
        { ABS_MT_TRACKING_ID, 65535 }, { ABS_MT_POSITION_X, SCREEN_W - 1 },
        { ABS_MT_POSITION_Y, SCREEN_H - 1 }, { ABS_MT_TOUCH_MAJOR, 255 },
    };
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; i++) {
        ioctl(touch_fd, UI_SET_ABSBIT, axes[i].code);
        dev.absmax[axes[i].code] = axes[i].max;
    }
    if (write(touch_fd, &dev, sizeof dev) != sizeof dev)
        perror("uinput setup");
    if (ioctl(touch_fd, UI_DEV_CREATE) < 0) {
        perror("uinput create");
        exit(1);
    }
    usleep(1800000); /* let the compositor pick up the new touchscreen */
}

static void close_touchscreen(void)
{
    usleep(300000);
    ioctl(touch_fd, UI_DEV_DESTROY);
    close(touch_fd);
}

static void finger(int slot, double x, double y, int is_new)
{
    emit(touch_fd, EV_ABS, ABS_MT_SLOT, slot);
    if (is_new)
        emit(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, ++tracking);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_X, (int)x);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_Y, (int)y);
    emit(touch_fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 10);
    if (slot == 0) {
        emit(touch_fd, EV_ABS, ABS_X, (int)x);
        emit(touch_fd, EV_ABS, ABS_Y, (int)y);
    }
}

static void lift(int slot)
{
    emit(touch_fd, EV_ABS, ABS_MT_SLOT, slot);
    emit(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
}

static void buttons(int down)
{
    emit(touch_fd, EV_KEY, BTN_TOUCH, down);
    emit(touch_fd, EV_KEY, BTN_TOOL_FINGER, down);
}

static void drag(double x1, double y1, double x2, double y2, int ms)
{
    int steps = ms / 12 < 2 ? 2 : ms / 12;
    finger(0, x1, y1, 1);
    buttons(1);
    syn(touch_fd);
    for (int i = 1; i <= steps; i++) {
        double f = (double)i / steps;
        usleep(ms * 1000 / steps);
        finger(0, x1 + (x2 - x1) * f, y1 + (y2 - y1) * f, 0);
        syn(touch_fd);
    }
    lift(0);
    buttons(0);
    syn(touch_fd);
}

static void hold(double x, double y, int ms)
{
    finger(0, x, y, 1);
    buttons(1);
    syn(touch_fd);
    usleep(ms * 1000);
    lift(0);
    buttons(0);
    syn(touch_fd);
}

static void pinch(double x, double y, double d1, double d2, int ms)
{
    int steps = ms / 12 < 2 ? 2 : ms / 12;
    finger(0, x - d1 / 2, y, 1);
    finger(1, x + d1 / 2, y, 1);
    buttons(1);
    syn(touch_fd);
    for (int i = 1; i <= steps; i++) {
        double d = d1 + (d2 - d1) * i / steps;
        usleep(ms * 1000 / steps);
        finger(0, x - d / 2, y, 0);
        finger(1, x + d / 2, y, 0);
        syn(touch_fd);
    }
    lift(0);
    lift(1);
    buttons(0);
    syn(touch_fd);
}

/* --- virtual keyboard ------------------------------------------------------------- */

static int kb_fd = -1;

static void kb_open(void)
{
    struct uinput_user_dev dev;
    kb_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (kb_fd < 0) {
        perror("/dev/uinput");
        exit(1);
    }
    ioctl(kb_fd, UI_SET_EVBIT, EV_KEY);
    for (int code = 1; code < 128; code++)
        ioctl(kb_fd, UI_SET_KEYBIT, code);
    memset(&dev, 0, sizeof dev);
    strcpy(dev.name, "pocketssh-test-keyboard");
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x1234;
    dev.id.product = 0x5678;
    dev.id.version = 1;
    if (write(kb_fd, &dev, sizeof dev) != sizeof dev)
        perror("uinput setup");
    ioctl(kb_fd, UI_DEV_CREATE);
    sleep(1); /* let the compositor pick up the new keyboard */
}

static void kb_close(void)
{
    usleep(300000);
    ioctl(kb_fd, UI_DEV_DESTROY);
    close(kb_fd);
}

static void press(int code, const int *mods, int nmods)
{
    for (int i = 0; i < nmods; i++)
        emit(kb_fd, EV_KEY, mods[i], 1);
    emit(kb_fd, EV_KEY, code, 1);
    syn(kb_fd);
    emit(kb_fd, EV_KEY, code, 0);
    for (int i = 0; i < nmods; i++)
        emit(kb_fd, EV_KEY, mods[i], 0);
    syn(kb_fd);
    usleep(30000);
}

static const struct { const char *name; int code; } named[] = {
    { "esc", KEY_ESC }, { "backspace", KEY_BACKSPACE }, { "tab", KEY_TAB }, { "enter", KEY_ENTER },
    { "ctrl", KEY_LEFTCTRL }, { "shift", KEY_LEFTSHIFT }, { "alt", KEY_LEFTALT }, { "space", KEY_SPACE },
    { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT },
    { "home", KEY_HOME }, { "end", KEY_END }, { "pgup", KEY_PAGEUP }, { "pgdn", KEY_PAGEDOWN },
    { "delete", KEY_DELETE },
};

/* US layout: code for each ASCII character, and whether it needs shift. */
static int char_code(char c, int *shift)
{
    static const char *row_num = "1234567890";
    static const char *shifted_num = "!@#$%^&*()";
    static const struct { char plain, shifted; int code; } sym[] = {
        { '-', '_', KEY_MINUS }, { '=', '+', KEY_EQUAL }, { '[', '{', KEY_LEFTBRACE },
        { ']', '}', KEY_RIGHTBRACE }, { ';', ':', KEY_SEMICOLON }, { '\'', '"', KEY_APOSTROPHE },
        { '`', '~', KEY_GRAVE }, { '\\', '|', KEY_BACKSLASH }, { ',', '<', KEY_COMMA },
        { '.', '>', KEY_DOT }, { '/', '?', KEY_SLASH },
    };
    static const int letters[26] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
        KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    };
    *shift = 0;
    if (c >= 'a' && c <= 'z')
        return letters[c - 'a'];
    if (c >= 'A' && c <= 'Z') {
        *shift = 1;
        return letters[c - 'A'];
    }
    for (int i = 0; i < 10; i++) {
        if (c == row_num[i])
            return i == 9 ? KEY_0 : KEY_1 + i;
        if (c == shifted_num[i]) {
            *shift = 1;
            return i == 9 ? KEY_0 : KEY_1 + i;
        }
    }
    for (size_t i = 0; i < sizeof sym / sizeof sym[0]; i++) {
        if (c == sym[i].plain)
            return sym[i].code;
        if (c == sym[i].shifted) {
            *shift = 1;
            return sym[i].code;
        }
    }
    if (c == ' ')
        return KEY_SPACE;
    if (c == '\n')
        return KEY_ENTER;
    if (c == '\t')
        return KEY_TAB;
    return -1;
}

static int named_code(const char *name)
{
    int shift;
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
        if (strcmp(name, named[i].name) == 0)
            return named[i].code;
    if (strlen(name) == 1)
        return char_code(name[0], &shift);
    return -1;
}

static void type_text(const char *text)
{
    static const int shift_mod[1] = { KEY_LEFTSHIFT };
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '\\' && p[1] == 'n') {
            c = '\n';
            p++;
        }
        int shift, code = char_code(c, &shift);
        if (code >= 0)
            press(code, shift_mod, shift);
    }
}

static void combo(const char *spec)
{
    char buf[64];
    int mods[4], nmods = 0;
    strncpy(buf, spec, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *last = buf, *plus;
    while ((plus = strchr(last, '+')) && plus[1]) {
        *plus = 0;
        if (nmods < 4)
            mods[nmods++] = named_code(last);
        last = plus + 1;
    }
    int code = named_code(last);
    if (code < 0) {
        fprintf(stderr, "unknown key %s\n", last);
        return;
    }
    press(code, mods, nmods);
}

static double arg(int argc, char **argv, int i, double fallback)
{
    return i < argc ? atof(argv[i]) : fallback;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: touch tap|hold|drag|pinch|type|key ...\n");
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "typefile") && argc > 2) {
        char buf[8192];
        FILE *f = fopen(argv[2], "r");
        if (!f) {
            perror(argv[2]);
            return 1;
        }
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        kb_open();
        for (size_t i = 0; i < n; i++) {
            int shift, code = char_code(buf[i], &shift);
            static const int shift_mod[1] = { KEY_LEFTSHIFT };
            if (code >= 0)
                press(code, shift_mod, shift);
        }
        kb_close();
        return 0;
    }
    if (!strcmp(cmd, "type") || !strcmp(cmd, "key")) {
        kb_open();
        for (int i = 2; i < argc; i++) {
            if (!strcmp(cmd, "type")) {
                if (i > 2)
                    type_text(" ");
                type_text(argv[i]);
            } else {
                combo(argv[i]);
            }
        }
        kb_close();
        return 0;
    }
    open_touchscreen();
    if (!strcmp(cmd, "tap"))
        drag(arg(argc, argv, 2, 0), arg(argc, argv, 3, 0), arg(argc, argv, 2, 0), arg(argc, argv, 3, 0), 60);
    else if (!strcmp(cmd, "hold"))
        hold(arg(argc, argv, 2, 0), arg(argc, argv, 3, 0), (int)arg(argc, argv, 4, 900));
    else if (!strcmp(cmd, "drag"))
        drag(arg(argc, argv, 2, 0), arg(argc, argv, 3, 0), arg(argc, argv, 4, 0), arg(argc, argv, 5, 0),
             (int)arg(argc, argv, 6, 300));
    else if (!strcmp(cmd, "pinch"))
        pinch(arg(argc, argv, 2, 0), arg(argc, argv, 3, 0), arg(argc, argv, 4, 0), arg(argc, argv, 5, 0),
              (int)arg(argc, argv, 6, 400));
    else
        fprintf(stderr, "unknown command %s\n", cmd);
    close_touchscreen();
    return 0;
}
