/* Sensor lookup-by-name tests (added 2026-11-02). Machine-independent:
   instead of expecting a particular hwmonN, every resolved path is checked
   against its own sysfs "name"/driver, and readings are compared with a
   direct read of that same path. Sensors this machine doesn't have are
   reported as SKIP, not FAIL. Run via tests/run-tests.sh. */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main

static int fails = 0, passes = 0, skips = 0;
#define CHECK(cond, ...) do { if (cond) { printf("PASS "); passes++; } else { printf("FAIL "); fails++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)
#define SKIP(...) do { printf("SKIP "); skips++; printf(__VA_ARGS__); printf("\n"); } while (0)

static void check_hwmon(sysfs_lookup_t *c) {
    const char *dir = hwmon_dir(c);
    if (!dir) { SKIP("no hwmon named '%s' on this machine", c->want); return; }
    CHECK(hwmon_matches(dir, c->want), "'%s' resolved to %s, whose name file matches", c->want, dir);
    char p[352]; snprintf(p, sizeof(p), "%s/temp1_input", dir);
    int direct = read_hwmon_temp_c(p), via = hwmon_temp_c(c, 1);
    if (direct < 0) SKIP("'%s' has no temp1_input", c->want);
    else CHECK(abs(via - direct) <= 2, "'%s' temp1 via lookup %d C == direct read %d C", c->want, via, direct);
}

int main(void) {
    /* 1) resolution by name gives the right device and identical readings */
    check_hwmon(&g_hwmon_cpu);
    check_hwmon(&g_hwmon_gpu);
    check_hwmon(&g_hwmon_board);
    const char *drm = drm_device_dir(&g_drm_gpu);
    if (!drm) SKIP("no amdgpu DRM device on this machine");
    else {
        CHECK(drm_matches(drm, "amdgpu"), "amdgpu DRM device resolved to %s (driver + VRAM file verified)", drm);
        unsigned long long u, t; get_vram_bytes(&u, &t);
        CHECK(t > 0 && u <= t, "VRAM used %llu <= total %llu", u, t);
        double g = get_gpu_percent();
        CHECK(g >= 0 && g <= 100, "GPU busy %.0f%% in range", g);
    }

    /* 2) a cached path that now belongs to a different device is re-resolved */
    if (hwmon_dir(&g_hwmon_cpu)) {
        char right[320]; snprintf(right, sizeof(right), "%s", g_hwmon_cpu.dir);
        /* point the cache at some OTHER hwmon entry, as after a replug reusing numbers */
        DIR *d = opendir("/sys/class/hwmon"); struct dirent *e; char other[320] = "";
        while (d && (e = readdir(d))) {
            if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
            snprintf(other, sizeof(other), "/sys/class/hwmon/%s", e->d_name);
            if (strcmp(other, right) != 0) break;
            other[0] = 0;
        }
        if (d) closedir(d);
        if (!other[0]) SKIP("only one hwmon device; can't simulate a swapped number");
        else {
            snprintf(g_hwmon_cpu.dir, sizeof(g_hwmon_cpu.dir), "%s", other);
            g_hwmon_cpu.checked = 0;
            const char *r = hwmon_dir(&g_hwmon_cpu);
            CHECK(r && strcmp(r, right) == 0, "stale cache pointing at %s re-resolved to %s", other, r ? r : "NULL");
        }
    }

    /* 3) missing sensors: "no reading" like a missing file always gave, and rescans rate-limited */
    sysfs_lookup_t missing = {"no_such_sensor", "", 0};
    CHECK(hwmon_temp_c(&missing, 1) == -1, "missing sensor gives -1 (same as a missing file before)");
    time_t c1 = time(NULL) - 5;           /* planted: a rescan would overwrite it with "now" */
    missing.checked = c1;
    CHECK(hwmon_temp_c(&missing, 1) == -1 && missing.checked == c1, "missing sensor: call 5 s later does NOT rescan");
    missing.checked = time(NULL) - 31;
    hwmon_temp_c(&missing, 1);
    CHECK(missing.checked >= time(NULL) - 1, "missing sensor: call 31 s later DOES rescan");
    sysfs_lookup_t miss_drm = {"no_such_driver", "", 0};
    drm_device_dir(&miss_drm);
    c1 = time(NULL) - 5; miss_drm.checked = c1;
    CHECK(drm_device_dir(&miss_drm) == NULL && miss_drm.checked == c1, "missing DRM device: call 5 s later does NOT rescan");

    printf("\nsensor lookup: %d passed, %d failed, %d skipped\n", passes, fails, skips);
    return fails ? 1 : 0;
}
