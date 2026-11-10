/* Sensor detection tests (2026-11-02, rewritten 2026-11-10 for the
   portable multi-driver lookup). Two halves:
   1) the REAL machine: whatever was picked must be a known driver and
      read the same value as a direct read of that file (SKIP, not FAIL,
      for hardware this machine doesn't have);
   2) FAKE sysfs trees built in a scratch dir, one per kind of PC this
      can't be run on here (Intel desktop, AMD APU + dGPU, Intel Arc,
      nouveau, sleeping laptop dGPU, thermal-zone-only CPU...), by
      pointing g_sysfs at them. Run via tests/run-tests.sh. */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main
#include <errno.h>

#ifndef TEST_SCRATCH
#error "build with -DTEST_SCRATCH=\"/some/dir\" (tests/run-tests.sh does)"
#endif

static int fails = 0, passes = 0, skips = 0;
#define CHECK(cond, ...) do { if (cond) { printf("PASS "); passes++; } else { printf("FAIL "); fails++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)
#define SKIP(...) do { printf("SKIP "); skips++; printf(__VA_ARGS__); printf("\n"); } while (0)

static char g_root[200];

static void mkdirs(const char *path) {
    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

/* write <root>/<rel> = content (creating parent dirs) */
static void put(const char *rel, const char *content) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", g_root, rel);
    char dir[700];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdirs(dir); }
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "%s\n", content);
    fclose(f);
}

/* <root>/class/drm/<card>/device/driver -> .../drivers/<drv> (dangling is fine: only readlink'd) */
static void card(const char *cardn, const char *drv) {
    char dir[700], link[720], target[256];
    snprintf(dir, sizeof(dir), "%s/class/drm/%s/device", g_root, cardn);
    mkdirs(dir);
    snprintf(link, sizeof(link), "%s/driver", dir);
    snprintf(target, sizeof(target), "../../../bus/pci/drivers/%s", drv);
    if (symlink(target, link) != 0 && errno != EEXIST) { perror(link); exit(2); }
}

static void use_tree(const char *name) {
    snprintf(g_root, sizeof(g_root), "%s/%s", TEST_SCRATCH, name);
    mkdirs(g_root);
    g_sysfs = g_root;
    sensors_reset_cache();
    printf("-- fake: %s\n", name);
}

static void real_machine(void) {
    printf("-- real machine (/sys)\n");
    g_sysfs = "/sys";
    sensors_reset_cache();
    if (!hwmon_dir(&g_hwmon_cpu) && !thermal_cpu_path()) SKIP("no CPU temperature sensor on this machine");
    else if (hwmon_dir(&g_hwmon_cpu)) {
        int known = 0;
        for (int i = 0; CPU_HWMON[i]; i++) known |= sensor_name_matches(g_hwmon_cpu.name, CPU_HWMON[i]);
        CHECK(known, "CPU sensor '%s' at %s is a known CPU driver", g_hwmon_cpu.name, g_hwmon_cpu.dir);
        char p[400]; snprintf(p, sizeof(p), "%s/temp%d_input", g_hwmon_cpu.dir, g_hwmon_cpu.chan);
        int direct = read_hwmon_temp_c(p), via = get_cpu_temp_c();
        CHECK(direct > 0 && abs(via - direct) <= 2, "CPU temp via lookup %d C == direct read %d C", via, direct);
    }
    const gpu_lookup_t *g = gpu_get();
    if (!g) SKIP("no supported GPU on this machine");
    else {
        CHECK(gpu_driver_rank(g->driver) >= 0, "GPU driver '%s' (%s) is a known one", g->driver, g->dev);
        unsigned long long u, t; get_vram_bytes(&u, &t);
        if (gpu_is(g, "amdgpu") || gpu_is(g, "nvidia")) CHECK(t > 0 && u <= t, "VRAM used %llu <= total %llu", u, t);
        int e = gpu_temp_c(0);
        if (g->hwmon[0]) CHECK(e > 0, "GPU main temp %d C read from %s", e, g->hwmon);
        if (gpu_is(g, "amdgpu")) { double b = get_gpu_percent(); CHECK(b >= 0 && b <= 100, "GPU busy %.0f%% in range", b); }
    }
    if (!hwmon_dir(&g_hwmon_board)) SKIP("no motherboard sensor driver on this machine");
    else CHECK(hwmon_temp_c(&g_hwmon_board, 1) > 0 || hwmon_temp_c(&g_hwmon_board, 2) > 0,
               "board sensor '%s' gives at least one reading", g_hwmon_board.name);
    /* no NVIDIA driver here -> NVML must never even be dlopen'ed */
    if (access("/proc/driver/nvidia/version", R_OK) != 0)
        CHECK(!nvml_ready() && g_nvml.lib == NULL, "no NVIDIA driver -> NVML not loaded");
    else SKIP("NVIDIA driver present: NVML path exercised by the real-machine checks above");
}

int main(void) {
    real_machine();

    /* Intel desktop: coretemp with the package reading NOT on temp1,
       Intel iGPU (no VRAM/busy/temps), Nuvoton Super-I/O board chip
       with junk unconnected inputs, plus a decoy acpitz. */
    use_tree("intel_desktop");
    put("class/hwmon/hwmon0/name", "acpitz");  put("class/hwmon/hwmon0/temp1_input", "27800");
    put("class/hwmon/hwmon1/name", "coretemp");
    put("class/hwmon/hwmon1/temp1_label", "Core 0");       put("class/hwmon/hwmon1/temp1_input", "41000");
    put("class/hwmon/hwmon1/temp3_label", "Package id 0"); put("class/hwmon/hwmon1/temp3_input", "56000");
    put("class/hwmon/hwmon2/name", "nct6798");
    put("class/hwmon/hwmon2/temp1_input", "31000");
    put("class/hwmon/hwmon2/temp2_input", "127000");       /* unconnected */
    put("class/hwmon/hwmon2/temp3_input", "-128000");      /* unconnected */
    card("card0", "i915");
    CHECK(get_cpu_temp_c() == 56 && g_hwmon_cpu.chan == 3, "coretemp: 'Package id 0' (temp3) picked over temp1 -> %d C", get_cpu_temp_c());
    CHECK(hwmon_dir(&g_hwmon_board) && strcmp(g_hwmon_board.name, "nct6798") == 0, "board: nct6798 matched by the 'nct6*' pattern");
    CHECK(hwmon_temp_c(&g_hwmon_board, 1) == 31, "board: MB_TEMP1 = 31 C");
    CHECK(hwmon_temp_c(&g_hwmon_board, 2) == -1 && hwmon_temp_c(&g_hwmon_board, 3) == -1, "board: junk 127 / -128 readings -> N/A");
    { const gpu_lookup_t *g = gpu_get(); unsigned long long u, t; get_vram_bytes(&u, &t);
      CHECK(g && gpu_is(g, "i915"), "GPU: i915 found");
      CHECK(t == 0 && get_gpu_percent() < 0 && gpu_temp_c(0) == -1, "GPU: iGPU without sensors -> VRAM/busy/temp N/A"); }

    /* AMD APU + discrete card: both amdgpu, the 16 GB card must win
       and its OWN hwmon must be used (not the APU's). Two board
       drivers present: vendor WMI beats the raw Super-I/O chip. */
    use_tree("amd_apu_plus_dgpu");
    put("class/hwmon/hwmon0/name", "k10temp");
    put("class/hwmon/hwmon0/temp1_label", "Tctl"); put("class/hwmon/hwmon0/temp1_input", "62000");
    put("class/hwmon/hwmon1/name", "it8688");      put("class/hwmon/hwmon1/temp1_input", "33000");
    put("class/hwmon/hwmon2/name", "gigabyte_wmi"); put("class/hwmon/hwmon2/temp1_input", "29000");
    card("card0", "amdgpu");
    put("class/drm/card0/device/mem_info_vram_total", "536870912");
    put("class/drm/card0/device/mem_info_vram_used", "100000000");
    put("class/drm/card0/device/hwmon/hwmon5/temp1_label", "edge");
    put("class/drm/card0/device/hwmon/hwmon5/temp1_input", "45000");
    card("card1", "amdgpu");
    put("class/drm/card1/device/mem_info_vram_total", "17179869184");
    put("class/drm/card1/device/mem_info_vram_used", "4294967296");
    put("class/drm/card1/device/gpu_busy_percent", "33");
    put("class/drm/card1/device/hwmon/hwmon6/temp1_label", "edge");     put("class/drm/card1/device/hwmon/hwmon6/temp1_input", "60000");
    put("class/drm/card1/device/hwmon/hwmon6/temp2_label", "junction"); put("class/drm/card1/device/hwmon/hwmon6/temp2_input", "70000");
    put("class/drm/card1/device/hwmon/hwmon6/temp3_label", "mem");      put("class/drm/card1/device/hwmon/hwmon6/temp3_input", "80000");
    put("class/drm/card1-DP-1/status", "connected");                    /* connector: must be ignored */
    CHECK(get_cpu_temp_c() == 62, "k10temp Tctl -> 62 C");
    { const gpu_lookup_t *g = gpu_get(); unsigned long long u, t; get_vram_bytes(&u, &t);
      CHECK(g && strstr(g->dev, "/card1/"), "GPU: the 16 GB card1 wins over the 512 MB APU (%s)", g ? g->dev : "none");
      CHECK(t == 17179869184ULL && u == 4294967296ULL, "GPU: VRAM 4 / 16 GB from card1");
      CHECK(get_gpu_percent() == 33, "GPU: busy 33%%");
      CHECK(gpu_temp_c(0) == 60 && gpu_temp_c(1) == 70 && gpu_temp_c(2) == 80, "GPU: edge/hotspot/vram 60/70/80 from card1's own hwmon"); }
    CHECK(hwmon_dir(&g_hwmon_board) && strcmp(g_hwmon_board.name, "gigabyte_wmi") == 0, "board: gigabyte_wmi preferred over it8688");

    /* Intel Arc (xe): numbered temps, no labels in the ABI */
    use_tree("intel_arc_xe");
    card("card0", "xe");
    put("class/drm/card0/device/hwmon/hwmon3/temp2_input", "50000");
    put("class/drm/card0/device/hwmon/hwmon3/temp3_input", "61000");
    CHECK(gpu_temp_c(0) == 50 && gpu_temp_c(1) == -1 && gpu_temp_c(2) == 61, "xe: temp2 = package 50 C, temp3 = VRAM 61 C, no hotspot");
    CHECK(get_cpu_temp_c() == -1, "no CPU sensor at all -> N/A (-1), not a bogus number");

    /* NVIDIA with the open-source nouveau driver: one temp, no VRAM info */
    use_tree("nouveau");
    card("card0", "nouveau");
    put("class/drm/card0/device/hwmon/hwmon2/temp1_input", "47000");
    { unsigned long long u, t; get_vram_bytes(&u, &t);
      CHECK(gpu_temp_c(0) == 47 && t == 0, "nouveau: temp 47 C, VRAM N/A"); }

    /* hybrid laptop: dGPU runtime-suspended -> nothing read */
    use_tree("suspended_dgpu");
    card("card0", "amdgpu");
    put("class/drm/card0/device/mem_info_vram_total", "8589934592");
    put("class/drm/card0/device/mem_info_vram_used", "1");
    put("class/drm/card0/device/gpu_busy_percent", "5");
    put("class/drm/card0/device/power/runtime_status", "suspended");
    put("class/drm/card0/device/hwmon/hwmon1/temp1_label", "edge");
    put("class/drm/card0/device/hwmon/hwmon1/temp1_input", "40000");
    { unsigned long long u, t; get_vram_bytes(&u, &t);
      CHECK(t == 0 && get_gpu_percent() < 0 && gpu_temp_c(0) == -1, "suspended GPU: not read (N/A), so the LCD can't keep it awake"); }

    /* no CPU hwmon driver loaded, but the thermal zone exists */
    use_tree("thermal_only");
    put("class/thermal/thermal_zone0/type", "acpitz");       put("class/thermal/thermal_zone0/temp", "27800");
    put("class/thermal/thermal_zone1/type", "x86_pkg_temp"); put("class/thermal/thermal_zone1/temp", "48000");
    CHECK(get_cpu_temp_c() == 48, "thermal-zone fallback: x86_pkg_temp 48 C (acpitz decoy ignored)");

    /* two hwmon entries with the same driver: lowest number wins, every time */
    use_tree("two_coretemp");
    put("class/hwmon/hwmon7/name", "coretemp"); put("class/hwmon/hwmon7/temp1_input", "70000");
    put("class/hwmon/hwmon2/name", "coretemp"); put("class/hwmon/hwmon2/temp1_input", "52000");
    CHECK(get_cpu_temp_c() == 52 && strstr(g_hwmon_cpu.dir, "/hwmon2"), "duplicate driver: hwmon2 chosen over hwmon7");

    /* caching: a stale cached path is replaced on rescan; misses rescan at most every 30 s */
    snprintf(g_hwmon_cpu.dir, sizeof(g_hwmon_cpu.dir), "%s/class/hwmon/hwmon7", g_root);
    g_hwmon_cpu.checked = 0;
    CHECK(hwmon_dir(&g_hwmon_cpu) && strstr(g_hwmon_cpu.dir, "/hwmon2"), "stale cached path re-resolved on rescan");
    use_tree("empty");
    hwmon_lookup_t missing = {CPU_HWMON, CPU_LABELS, "", "", 1, 0};
    CHECK(hwmon_temp_c(&missing, 0) == -1, "missing sensor -> -1");
    time_t c1 = time(NULL) - 5;
    missing.checked = c1;
    CHECK(hwmon_temp_c(&missing, 0) == -1 && missing.checked == c1, "missing sensor: call 5 s later does NOT rescan");
    missing.checked = time(NULL) - 31;
    hwmon_temp_c(&missing, 0);
    CHECK(missing.checked >= time(NULL) - 1, "missing sensor: call 31 s later DOES rescan");
    CHECK(gpu_get() == NULL, "no GPU -> NULL");
    c1 = time(NULL) - 5; g_gpu.checked = c1;
    CHECK(gpu_get() == NULL && g_gpu.checked == c1, "missing GPU: call 5 s later does NOT rescan");

    printf("\nsensor detection: %d passed, %d failed, %d skipped\n", passes, fails, skips);
    return fails ? 1 : 0;
}
