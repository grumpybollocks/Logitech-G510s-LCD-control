#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input.h>

#define NUM_SCREENS 2

#ifndef G510_KEYS_DEV
#define G510_KEYS_DEV "/dev/g510-keys"   /* tests build with a fake path */
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

/* Real bug found by code review: the old unguarded fopen() at startup
   returned exit(1) on failure exactly like the intentional mid-run
   device-loss exit at the bottom of this file -- but with no internal
   retry, a boot-time udev race (device not enumerated yet) or the
   keyboard being briefly unavailable hits systemd's default
   RestartSec=3/StartLimitBurst=5 within ~15 seconds and the unit gets
   marked permanently failed, needing a manual `systemctl --user
   reset-failed` or reboot even after the keyboard is replugged. Same
   fix as g510_macro_daemon.py's _wait_for_device(): poll internally
   instead of crash-looping through systemd. */
/* Also used after the keyboard drops off USB mid-run (2026-11-10): the
   G510s re-enumerates every time the headset jack is plugged/unplugged
   (USB ID c22e <-> c22d), which used to mean exit(1) + a 3 s systemd
   restart + a "buttons-restarts" alert from g510-watch each time. Now
   the same process just waits here and carries on. Polls every 200 ms
   so L1-L5 are back about as soon as udev has set the new node up (the
   first open can briefly hit EACCES before the rule applies -- that's
   just another retry). */
#define DEVICE_POLL_MS 200

static FILE *wait_for_device(const char *why) {
    int printed = 0;
    while (1) {
        FILE *f = fopen(G510_KEYS_DEV, "rb");
        if (f) {
            if (printed) fprintf(stderr, "g510-lcd-buttons: %s is back\n", G510_KEYS_DEV);
            return f;
        }
        if (!printed) {
            fprintf(stderr, "g510-lcd-buttons: %s -- waiting for %s...\n", why, G510_KEYS_DEV);
            printed = 1;
        }
        usleep(DEVICE_POLL_MS * 1000);
    }
}

int main(void) {
    setvbuf(stderr, NULL, _IOLBF, 0);   /* journald gets each line as it happens */
    FILE *f = wait_for_device("keyboard not found yet");

    /* These switches bounce (confirmed: single physical presses were
       logging 2-4 times). Track the last accepted press time per key
       (indices 696-700) and ignore repeats within DEBOUNCE_MS. */
    struct timespec last_press[701] = {0};

    struct input_event ev;
    for (;;) {
        if (fread(&ev, sizeof(ev), 1, f) != 1) {
            /* The only way out of a blocking read on an evdev node is the
               device going away (unplug, jack re-enumeration, suspend
               dropping USB) -- `systemctl stop` just kills the process.
               Reopen instead of exiting (see wait_for_device()). */
            fclose(f);
            /* pause once before reopening: during re-enumeration the old
               node can still open and then fail every read straight away,
               which without this would spin the CPU until it's gone */
            usleep(DEVICE_POLL_MS * 1000);
            f = wait_for_device("keyboard disconnected");
            continue;
        }
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
    /* not reached: the loop above never exits. History: it used to
       end on a failed read and exit(1) so systemd's Restart=on-failure
       would reopen the device -- a clean exit(0) there had once left
       L1-L5 dead until a manual restart. Reconnecting in-process (above)
       replaces both; Restart=on-failure stays as a crash safety net. */
    return 1;
}
