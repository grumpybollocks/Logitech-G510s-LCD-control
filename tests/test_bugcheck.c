/* Regression tests for the 2026-11-14 bug-check of g510_lcd_stats.c.
   Nothing here touches the keyboard, the live services or your settings:
   HOME, XDG_RUNTIME_DIR, PATH and g_sysfs all point into TEST_SCRATCH.
   Run via tests/run-tests.sh. */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main

#ifndef TEST_SCRATCH
#error "build with -DTEST_SCRATCH=\"/some/dir\" (tests/run-tests.sh does)"
#endif

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { printf("PASS "); passes++; } else { printf("FAIL "); fails++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void put(const char *path, const char *content) {
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname '%s')\"", path);
    if (system(cmd) != 0) exit(2);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(content, f);
    fclose(f);
}

int main(void) {
    char run[300], home[300], sys[300], bin[300];
    snprintf(run, sizeof run, "%s/run", TEST_SCRATCH);
    snprintf(home, sizeof home, "%s/home", TEST_SCRATCH);
    snprintf(sys, sizeof sys, "%s/sys", TEST_SCRATCH);
    snprintf(bin, sizeof bin, "%s/bin", TEST_SCRATCH);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s' '%s' '%s' '%s'", run, home, sys, bin);
    if (system(cmd) != 0) return 2;
    setenv("XDG_RUNTIME_DIR", run, 1);
    setenv("HOME", home, 1);

    /* 1. renaming the screen-state file must not make the loop spin */
    screen_watch_init();
    CHECK(g_screen_ifd >= 0 && g_screen_wd >= 0, "inotify watch set up");
    char moved[400];
    snprintf(moved, sizeof moved, "%s.moved", screen_state_path());
    rename(screen_state_path(), moved);
    long t0 = now_ms();
    int calls = 0;
    while (now_ms() - t0 < 1500 && calls < 10000) { sleep_or_screen_change(500); calls++; }
    CHECK(calls <= 6, "after a rename, sleep_or_screen_change(500) ran %d times in 1.5 s (was ~75000 before the fix)", calls);
    unlink(screen_state_path());
    t0 = now_ms(); calls = 0;
    while (now_ms() - t0 < 1500 && calls < 10000) { sleep_or_screen_change(500); calls++; }
    CHECK(calls <= 6, "after a delete it still sleeps normally (%d calls)", calls);

    /* 2. a half-written screen-state file keeps the last screen */
    put(screen_state_path(), "3");
    CHECK(read_screen() == 3, "screen 3 read");
    put(screen_state_path(), "");
    CHECK(read_screen() == 3, "empty file (mid-rewrite) keeps screen 3");
    unlink(screen_state_path());
    CHECK(read_screen() == 0, "missing file is still screen 0");

    /* 3. absurd coordinates are clamped, not passed to libg15render */
    CHECK(parse_coord("-2147483648") == COORD_MIN && parse_coord("99999999999999999999") == COORD_MAX
          && parse_coord("42") == 42 && parse_coord("-5") == -5, "parse_coord clamps to [%d, %d]", COORD_MIN, COORD_MAX);
    {
        char cs[400];
        snprintf(cs, sizeof cs, "%s/.local/share/g510-lcd/custom_screens.txt", home);
        put(cs, "SCREEN L2\nELEMENT sensor=CPU_PCT style=number x=-2147483648 y=2147483647 width=40\n"
                "TEXT content=hi x=-99999999999 y=5\nIMAGE path=nope.bin x=2147483647 y=0 width=8 height=8\n"
                "VISUALIZER x=-2147483648 y=-2147483648 width=40 height=10\n");
        load_custom_screens();
        custom_screen_t *s = &custom_screens[0];
        CHECK(s->count == 1 && s->elements[0].x == COORD_MIN && s->elements[0].y == COORD_MAX
              && s->texts[0].x == COORD_MIN && s->visualizers[0].y == COORD_MIN,
              "extreme coordinates in custom_screens.txt are clamped on load");
        g15canvas canvas;
        memset(&canvas, 0, sizeof canvas);
        g15r_initCanvas(&canvas);
        t0 = now_ms();
        draw_element(&canvas, &s->elements[0]);
        CHECK(now_ms() - t0 < 1000, "drawing a clamped element returns (took %ld ms; used to hang forever)", now_ms() - t0);
    }

    /* 4. AMD: Tdie is preferred over Tctl (Ryzen 1000/2000X offset) */
    {
        char p[500];
        snprintf(p, sizeof p, "%s/class/hwmon/hwmon0/name", sys);              put(p, "k10temp\n");
        snprintf(p, sizeof p, "%s/class/hwmon/hwmon0/temp1_label", sys);       put(p, "Tctl\n");
        snprintf(p, sizeof p, "%s/class/hwmon/hwmon0/temp1_input", sys);       put(p, "80000\n");
        snprintf(p, sizeof p, "%s/class/hwmon/hwmon0/temp2_label", sys);       put(p, "Tdie\n");
        snprintf(p, sizeof p, "%s/class/hwmon/hwmon0/temp2_input", sys);       put(p, "60000\n");
        g_sysfs = sys;
        g_hwmon_cpu.checked = 0;
        g_hwmon_cpu.dir[0] = 0;
        CHECK(get_cpu_temp_c() == 60, "Tctl 80 / Tdie 60 -> CPU temp 60 (got %d)", get_cpu_temp_c());
    }

    /* 5. CPU GHz from cpufreq (works where /proc/cpuinfo has no "cpu MHz") */
    {
        char p[500];
        snprintf(p, sizeof p, "%s/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", sys);
        put(p, "3600000\n");
        double ghz = get_cpu_ghz();
        CHECK(ghz > 3.59 && ghz < 3.61, "cpufreq 3600000 kHz -> %.2f GHz", ghz);
        g_sysfs = "/sys";
    }

    /* 6. intervals use a clock that a wall-clock step back can't freeze */
    {
        time_t a = mono_now();
        struct timespec bt;
        clock_gettime(CLOCK_BOOTTIME, &bt);
        CHECK(a > 0 && a == bt.tv_sec + 1, "mono_now() follows CLOCK_BOOTTIME, never 0");
    }

    /* 7. a hung media player can't block the render loop for long */
    {
        char fake[400];
        snprintf(fake, sizeof fake, "%s/playerctl", bin);
        put(fake, "#!/bin/sh\nsleep 8\n");
        chmod(fake, 0755);
        char path[800];
        snprintf(path, sizeof path, "%s:%s", bin, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
        setenv("PATH", path, 1);
        t0 = now_ms();
        update_media_info();
        long took = now_ms() - t0;
        CHECK(took < 3500, "media query with a hung player returns in %ld ms (was 8000 here, 25 s per player in real life)", took);
        t0 = now_ms();
        update_media_info();
        CHECK(now_ms() - t0 < 200, "next query is skipped for 30 s instead of stalling again (%ld ms)", now_ms() - t0);
    }

    /* 8. the secondary disk is a setting, not a compiled-in drive name */
    {
        char cfg[500];
        snprintf(cfg, sizeof cfg, "%s/secondary_disk", data_dir());
        unlink(cfg);
        double bar; char d[32];
        get_sensor_value("DISK_SECONDARY_PCT", &bar, d, sizeof d);
        CHECK(strcmp(disk_secondary_path(), "") == 0 && strcmp(d, "N/A") == 0, "no setting -> N/A (got '%s')", d);
        char line[400];
        snprintf(line, sizeof line, "%s\n", TEST_SCRATCH);
        put(cfg, line);
        get_sensor_value("DISK_SECONDARY_PCT", &bar, d, sizeof d);
        CHECK(strcmp(disk_secondary_path(), TEST_SCRATCH) == 0 && strcmp(d, "N/A") == 0,
              "a plain folder (drive not mounted there) -> N/A, not the root fs usage (got '%s')", d);
        put(cfg, "/\n");          /* "/" is always a mountpoint */
        get_sensor_value("DISK_SECONDARY_PCT", &bar, d, sizeof d);
        CHECK(strchr(d, '%') && bar >= 0, "setting pointing at a mounted filesystem -> its usage (got '%s')", d);
    }

    /* ---- 2026-11-15 bug-check ---- */
    /* 9. the hung-player back-off reaches a fresh process (the GUI's --preview):
          section 7 left the hung fake playerctl first on PATH */
    {
        struct stat st;
        CHECK(stat(media_skip_path(), &st) == 0, "the back-off is shared in %s", media_skip_path());
        g_media_skip_until = 0;           /* = a brand-new --preview process */
        t0 = now_ms();
        update_media_info();
        CHECK(now_ms() - t0 < 200, "a fresh process honours it instead of waiting ~2 s (%ld ms)", now_ms() - t0);
        g_media_skip_until = 0;
        put(media_skip_path(), "99999999999\n");   /* nonsense / from an earlier boot */
        media_skip_load();
        CHECK(g_media_skip_until == 0, "a back-off further out than 30 s is ignored");
        unlink(media_skip_path());
    }

    /* 10. an imported image with a long (multi-byte) name is still loaded */
    {
        char cs[400], body[600], name[200];
        memset(name, 'a', 150);
        name[150] = 0;
        snprintf(cs, sizeof cs, "%s/.local/share/g510-lcd/custom_screens.txt", home);
        snprintf(body, sizeof body, "SCREEN L2\nIMAGE path=custom_screen_images/%s.bin x=0 y=0 width=8 height=8\n", name);
        put(cs, body);
        load_custom_screens();
        custom_screen_t *s = &custom_screens[0];
        CHECK(s->image_count == 1 && strlen(s->images[0].path) == strlen("custom_screen_images/.bin") + 150,
              "a %zu-byte image path is kept whole (was cut at 127 -> file never found)",
              strlen("custom_screen_images/.bin") + 150);
    }

    /* 11. an unknown sensor keeps its bounds slot, so later drag boxes don't shift */
    {
        char cs[400], out[400], meta[420];
        snprintf(cs, sizeof cs, "%s/.local/share/g510-lcd/custom_screens.txt", home);
        put(cs, "SCREEN L2\nELEMENT sensor=NO_SUCH_SENSOR style=number x=0 y=0\n"
                "ELEMENT sensor=CPU_PCT style=number x=0 y=20\n");
        g15canvas canvas;
        memset(&canvas, 0, sizeof canvas);
        g15r_initCanvas(&canvas);
        draw_custom_screen(&canvas, 2);
        snprintf(out, sizeof out, "%s/preview.ppm", run);
        write_bounds_meta(out);
        snprintf(meta, sizeof meta, "%s.meta", out);
        FILE *f = fopen(meta, "r");
        char l0[200] = "", l1[200] = "";
        if (f) {
            if (!fgets(l0, sizeof l0, f)) l0[0] = 0;
            if (!fgets(l1, sizeof l1, f)) l1[0] = 0;
            fclose(f);
        }
        CHECK(strcmp(l0, "0\n") == 0 && strncmp(l1, "1 label_x1=0 label_y1=20 ", 25) == 0,
              "meta: empty line 0 for the unknown sensor, CPU_PCT's bounds on line 1 (got '%.40s' / '%.40s')", l0, l1);
    }

    printf("bugcheck: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
