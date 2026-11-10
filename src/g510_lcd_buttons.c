#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/input.h>

#define NUM_SCREENS 2

#ifndef G510_KEYS_DEV
#define G510_KEYS_DEV "/dev/g510-keys"   /* tests build with a fake path */
#endif
/* evdev name the udev rule matches on; "" disables the check (tests) */
#ifndef G510_KEYS_NAME
#define G510_KEYS_NAME "Logitech Gaming Keyboard Gaming Keys"
#endif

/* User-specific by nature -- built at runtime from $HOME rather than
   baked in at compile time, so this file doesn't hardcode a username. */
static const char *button_log_path(void) {
    static char path[256];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.local/share/g510lcd-buttons.log", home ? home : "/tmp");
    return path;
}

static const char *screen_state_path(void) {
    static char path[256];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    snprintf(path, sizeof(path), "%s/g510lcd_screen", runtime ? runtime : "/tmp");
    return path;
}

static const char *key_name(int code) {
    switch (code) {
        case 696: return "L1";
        case 697: return "L2";
        case 698: return "L3";
        case 699: return "L4";
        case 700: return "L5";
        default: return "?";
    }
}

static int read_screen(void) {
    FILE *f = fopen(screen_state_path(), "r");
    if (!f) return 0;
    int s = 0;
    fscanf(f, "%d", &s);
    fclose(f);
    return s;
}

static void write_screen(int s) {
    FILE *f = fopen(screen_state_path(), "w");
    if (!f) return;
    fprintf(f, "%d", s);
    fclose(f);
}

/* Same helper/reasoning as g510_lcd_stats.c's mkdir_p(): plain mkdir() can't
   create missing parent directories in one call, so on a fresh account where
   ~/.local/share doesn't exist yet, fopen(..., "a") below would fail silently
   and every physical L2-L5 press would just never get logged, forever. */
static void mkdir_p(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void log_button(const char *name) {
    const char *path = button_log_path();
    const char *slash = strrchr(path, '/');
    if (slash) {
        char dir[256];
        size_t len = (size_t)(slash - path);
        if (len >= sizeof(dir)) len = sizeof(dir) - 1;
        memcpy(dir, path, len);
        dir[len] = 0;
        mkdir_p(dir);
    }
    FILE *f = fopen(button_log_path(), "a");
    if (!f) return;
    time_t now = time(NULL);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] %s pressed\n", buf, name);
    fclose(f);
}

#define DEBOUNCE_MS 400

static long ms_since(struct timespec *last) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - last->tv_sec) * 1000 + (now.tv_nsec - last->tv_nsec) / 1000000;
}

/* Waiting for the Gaming Keys device -- at startup and after it drops
   off USB mid-run. Mid-run drops are routine: the G510s re-enumerates
   every time the headset jack is plugged/unplugged (USB ID c22e <->
   c22d). History: startup used to exit(1) when the device was missing,
   and a boot-time udev race could burn through systemd's StartLimitBurst
   and leave the unit permanently failed; device loss mid-run also
   exited, for a 3 s systemd restart (+ a g510-watch alert) on every jack
   change. Both now just wait here, in the same process.

   - Polls every 200 ms for the first 10 s (L1-L5 are back about as soon
     as udev has set the new node up), then every 3 s, so a keyboard
     unplugged for hours costs ~1 wakeup per 3 s, not 5 per second.
   - Checks the opened node really is the Gaming Keys interface: during
     re-enumeration the kernel can hand the old eventN number to another
     interface before udev has moved the g510-keys link, and reading the
     wrong device would leave L1-L5 dead with nothing to notice it.
   - Logs the reason (strerror) whenever it changes, so "permission
     denied" (not in the `input` group yet) doesn't look like an
     unplugged keyboard. */
#define POLL_FAST_MS 200
#define POLL_FAST_FOR_MS 10000
#define POLL_SLOW_MS 3000

static int is_gaming_keys(FILE *f) {
    if (!G510_KEYS_NAME[0]) return 1;
    char name[128] = "";
    if (ioctl(fileno(f), EVIOCGNAME(sizeof(name) - 1), name) < 0) return 0;
    return strcmp(name, G510_KEYS_NAME) == 0;
}

/* disconnected = 1 after a mid-run device loss: say so, and say when it's
   back (0 while flapping, so a flapping device isn't logged every cycle) */
static FILE *wait_for_device(int disconnected) {
    int reported = disconnected;
    int last_err = 0;
    long waited_ms = 0;
    if (disconnected) fprintf(stderr, "g510-lcd-buttons: keyboard disconnected -- waiting for %s\n", G510_KEYS_DEV);
    for (;;) {
        FILE *f = fopen(G510_KEYS_DEV, "rb");
        int err = f ? 0 : errno;
        if (f && is_gaming_keys(f)) {
            if (reported) fprintf(stderr, "g510-lcd-buttons: %s is back\n", G510_KEYS_DEV);
            return f;
        }
        if (f) { fclose(f); err = -1; }            /* -1: opened, but not the Gaming Keys */
        if (err != last_err) {
            fprintf(stderr, "g510-lcd-buttons: waiting for %s: %s\n", G510_KEYS_DEV,
                    err == -1 ? "it points at a different input device (udev still updating?)" : strerror(err));
            if (err == EACCES)
                fprintf(stderr, "g510-lcd-buttons: hint: your user must be in the 'input' group "
                                 "(log out and back in after being added)\n");
            last_err = err;
            reported = 1;
        }
        int ms = waited_ms < POLL_FAST_FOR_MS ? POLL_FAST_MS : POLL_SLOW_MS;
        usleep((useconds_t)ms * 1000);
        waited_ms += ms;
    }
}

int main(void) {
    setvbuf(stderr, NULL, _IOLBF, 0);   /* journald gets each line as it happens */
    FILE *f = wait_for_device(0);

    /* These switches bounce (confirmed: single physical presses were
       logging 2-4 times). Track the last accepted press time per key
       (indices 696-700) and ignore repeats within DEBOUNCE_MS. */
    struct timespec last_press[701] = {0};

    /* Flap guard: a node that reopens fine and then fails at once, over
       and over, used to loop every 200 ms forever logging 2 lines each
       time (systemd's StartLimitBurst used to stop that; nothing did any
       more). A failure within FLAP_MS of reconnecting doubles the pause
       (up to POLL_SLOW_MS) and is logged once, not every time. */
    #define FLAP_MS 2000
    struct timespec reconnected = {0, 0};
    int flaps = 0;

    struct input_event ev;
    for (;;) {
        if (fread(&ev, sizeof(ev), 1, f) != 1) {
            /* A blocking evdev read only fails when the device goes away
               (unplug, jack re-enumeration, suspend dropping USB);
               `systemctl stop` just kills the process. Pause once first:
               during re-enumeration the old node can still open and then
               fail every read at once, which would otherwise spin. */
            fclose(f);
            if (reconnected.tv_sec && ms_since(&reconnected) < FLAP_MS) {
                if (flaps++ == 0)
                    fprintf(stderr, "g510-lcd-buttons: %s keeps failing right after reconnecting -- backing off\n", G510_KEYS_DEV);
            } else {
                flaps = 0;
            }
            long pause = (long)POLL_FAST_MS << (flaps < 4 ? flaps : 4);
            usleep((useconds_t)(pause < POLL_SLOW_MS ? pause : POLL_SLOW_MS) * 1000);
            f = wait_for_device(flaps == 0);
            clock_gettime(CLOCK_MONOTONIC, &reconnected);
            continue;
        }
        flaps = 0;
        if (ev.type != EV_KEY || ev.value != 1) continue; /* only key-down */
        if (ev.code < 696 || ev.code > 700) continue;

        if (last_press[ev.code].tv_sec != 0 && ms_since(&last_press[ev.code]) < DEBOUNCE_MS) {
            continue; /* bounce, ignore */
        }
        clock_gettime(CLOCK_MONOTONIC, &last_press[ev.code]);

        if (ev.code == 696) { /* L1: cycle stats/clock, or return to stats from a test screen */
            int cur = read_screen();
            write_screen(cur >= 2 ? 0 : (cur + 1) % NUM_SCREENS);
        } else { /* L2-L5: show a test screen confirming the press, and log it */
            write_screen(ev.code - 695); /* 697->2, 698->3, 699->4, 700->5 */
            log_button(key_name(ev.code));
        }
    }
}
