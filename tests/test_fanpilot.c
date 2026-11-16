/* FanPilot sensors (2026-11-14): sensor=FP:<key> elements read
   /run/fanpilot/sensors.tsv, written once a second by the FanPilot fan
   service. Every test points g_fanpilot_path at a scratch file, so this
   never depends on FanPilot being installed. Run via tests/run-tests.sh. */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main

#ifndef TEST_SCRATCH
#error "build with -DTEST_SCRATCH=\"/some/dir\" (tests/run-tests.sh does)"
#endif

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { printf("PASS "); passes++; } else { printf("FAIL "); fails++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static char g_path[300];

static void write_file(const char *body) {
    FILE *f = fopen(g_path, "w");
    if (!f) { perror(g_path); exit(2); }
    fputs(body, f);
    fclose(f);
    fanpilot_load(1);           /* bypass the once-per-second cache */
}

static void write_snapshot(long long stamp, const char *rows) {
    size_t n = strlen(rows) + 64;      /* big snapshots (hundreds of rows) must not be cut short */
    char *buf = malloc(n);
    if (!buf) exit(2);
    snprintf(buf, n, "# fanpilot-sensors 1 %lld\n%s", stamp, rows);
    write_file(buf);
    free(buf);
}

static void value(const char *key, char *disp, size_t len, double *bar) {
    get_sensor_value(key, bar, disp, len);
}

#define ROWS \
    "t.k10temp/temp1\ttemp\tCPU\tCPU (Tctl)\t55.6\n" \
    "t.it8688/temp5\ttemp\tVRM\tVRM (VRM MOS)\t39.0\n" \
    "rpm.it8688/pwm2\trpm\tSYS1\tSystem fan 1 (SYS_FAN1) speed\t1180\n" \
    "pct.it8688/pwm2\tpct\tSY1%\tSystem fan 1 (SYS_FAN1) duty\t45\n" \
    "pct.it8688/pwm1\tpct\tCPF%\tCPU fan (CPU_FAN) duty\t\n"

int main(void) {
    snprintf(g_path, sizeof(g_path), "%s/sensors.tsv", TEST_SCRATCH);
    g_fanpilot_path = g_path;
    char d[32];
    double bar;
    long long now = (long long)time(NULL);

    /* --- a fresh, valid snapshot --- */
    write_snapshot(now, ROWS);
    CHECK(g_fp_count == 5, "fresh file: 5 rows parsed (got %d)", g_fp_count);
    const sensor_def_t *def = find_sensor("FP:t.k10temp/temp1");
    CHECK(def && strcmp(def->label, "CPU") == 0 && def->is_temp && !def->is_percent,
          "temp row -> label CPU, temperature kind");
    value("FP:t.k10temp/temp1", d, sizeof d, &bar);
    CHECK(strcmp(d, "56\xB0" "C") == 0 && bar > 61.0 && bar < 63.0, "55.6 C -> \"56 C\", bar 62%% (got '%s' %.1f)", d, bar);
    value("FP:rpm.it8688/pwm2", d, sizeof d, &bar);
    def = find_sensor("FP:rpm.it8688/pwm2");
    CHECK(strcmp(d, "1180") == 0 && bar < 0 && def && !def->is_temp && !def->is_percent,
          "rpm row -> \"1180\", no bar (got '%s')", d);
    value("FP:pct.it8688/pwm2", d, sizeof d, &bar);
    def = find_sensor("FP:pct.it8688/pwm2");
    CHECK(strcmp(d, "45%") == 0 && bar == 45 && def && def->is_percent, "pct row -> \"45%%\", bar 45");
    value("FP:pct.it8688/pwm1", d, sizeof d, &bar);
    CHECK(strcmp(d, "N/A") == 0 && bar < 0, "empty value (fan under BIOS control) -> N/A");
    value("FP:t.nope/temp9", d, sizeof d, &bar);
    def = find_sensor("FP:t.nope/temp9");
    CHECK(strcmp(d, "N/A") == 0 && def && strcmp(def->label, "FP") == 0, "unknown key -> label FP, N/A (still drawn)");
    CHECK(find_sensor("FP:") == NULL, "bare 'FP:' is not a sensor");
    CHECK(find_sensor("CPU_TEMP") == &SENSORS[2], "built-in sensors unaffected");

    /* --- FanPilot stopped / wrong file --- */
    write_snapshot(now - 60, ROWS);
    value("FP:t.k10temp/temp1", d, sizeof d, &bar);
    CHECK(g_fp_count == 0 && strcmp(d, "N/A") == 0, "60 s old snapshot (FanPilot stopped) -> N/A");
    write_snapshot(now + 3600, ROWS);
    CHECK(g_fp_count == 0, "timestamp far in the future -> ignored");
    write_file("# fanpilot-sensors 2 9999999999\n" ROWS);
    CHECK(g_fp_count == 0, "unknown format version -> ignored");
    write_file("hello\n" ROWS);
    CHECK(g_fp_count == 0, "no header -> ignored");
    unlink(g_path);
    fanpilot_load(1);
    value("FP:t.k10temp/temp1", d, sizeof d, &bar);
    CHECK(g_fp_count == 0 && strcmp(d, "N/A") == 0, "file missing -> N/A, no crash");

    /* --- damaged lines are skipped, good ones kept --- */
    {
        char rows[6000] = "";
        strcat(rows, "too\tfew\tfields\n");
        strcat(rows, "\ttemp\tX\tempty key\t40\n");
        strcat(rows, "t.a/temp1\tvolts\tV\tunknown kind\t1.2\n");
        strcat(rows, "t.b/temp1\ttemp\tB\tnot a number\t4x\n");
        strcat(rows, "t.long/temp1\ttemp\tLONG\t");
        for (int i = 0; i < 600; i++) strcat(rows, "x");      /* longer than the 512-byte line buffer */
        strcat(rows, "\t50\n");
        strcat(rows, "t.ok/temp1\ttemp\tOKAY\tfine\t41.0\n");
        write_snapshot(now, rows);
        value("FP:t.ok/temp1", d, sizeof d, &bar);
        CHECK(strcmp(d, "41\xB0" "C") == 0, "good row after damaged ones still read (got '%s')", d);
        value("FP:t.b/temp1", d, sizeof d, &bar);
        CHECK(strcmp(d, "N/A") == 0, "non-numeric value -> N/A");
        CHECK(fanpilot_index("a/temp1") < 0 && fanpilot_index("t.a/temp1") < 0, "unknown kind skipped");
        CHECK(fanpilot_index("t.long/temp1") < 0, "over-long line skipped whole (no half-parsed row)");
    }

    /* --- review findings (2026-11-14) --- */
    {
        /* a NUL byte in one line must not swallow the next good line */
        FILE *f = fopen(g_path, "wb");
        fprintf(f, "# fanpilot-sensors 1 %lld\n", now);
        fwrite("t.nul/temp1\ttemp\tN\tna\0me\t30\n", 1, 28, f);
        fputs("t.after/temp1\ttemp\tAFT\tafter\t31.0\n", f);
        fclose(f);
        fanpilot_load(1);
        CHECK(fanpilot_index("t.after/temp1") >= 0, "NUL byte in a line: the next line is still read");

        /* last line without '\n', CRLF endings */
        char rows[300];
        snprintf(rows, sizeof rows, "t.crlf/temp1\ttemp\tCR\tcrlf\t41.0\r\nt.last/temp1\ttemp\tLST\tlast\t42.0");
        write_snapshot(now, rows);
        value("FP:t.crlf/temp1", d, sizeof d, &bar);
        CHECK(strcmp(d, "41\xB0" "C") == 0, "CRLF line read like LF (got '%s')", d);
        value("FP:t.last/temp1", d, sizeof d, &bar);
        CHECK(strcmp(d, "42\xB0" "C") == 0, "last line without newline is read (got '%s')", d);

        /* six fields rejected (the Python side rejects them too) */
        write_snapshot(now, "t.six/temp1\ttemp\tSIX\tsix\t40\textra\n");
        CHECK(fanpilot_index("t.six/temp1") < 0, "row with 6 fields rejected");

        /* nan / inf / absurd numbers are not values */
        write_snapshot(now, "pct.n/pwm1\tpct\tN\tn\tnan\n" "pct.i/pwm1\tpct\tI\ti\tinf\n"
                            "rpm.h/pwm1\trpm\tH\th\t1e30\n" "t.ni/temp1\ttemp\tT\tt\t-inf\n");
        const char *bad[] = {"FP:pct.n/pwm1", "FP:pct.i/pwm1", "FP:rpm.h/pwm1", "FP:t.ni/temp1"};
        int all_na = 1;
        for (int i = 0; i < 4; i++) {
            value(bad[i], d, sizeof d, &bar);
            if (strcmp(d, "N/A") != 0 || bar >= 0) all_na = 0;
        }
        CHECK(all_na, "nan / inf / 1e30 -> N/A, no bar (no garbage like -2147483648)");

        /* extreme timestamps: ignored, no overflow */
        write_file("# fanpilot-sensors 1 -9223372036854775808\nt.x/temp1\ttemp\tX\tx\t30\n");
        CHECK(g_fp_count == 0, "absurd negative timestamp ignored");

        /* keys: the longest accepted key (47) fits through an element; longer ones are dropped, not truncated */
        char k47[48], k50[51], rowsk[300], elkey[64];
        memset(k47, 'k', 47); k47[47] = 0; memcpy(k47, "t.", 2);
        memset(k50, 'q', 50); k50[50] = 0; memcpy(k50, "t.", 2);
        snprintf(rowsk, sizeof rowsk, "%s\ttemp\tL47\tl\t33\n%s\ttemp\tL50\tl\t34\n", k47, k50);
        write_snapshot(now, rowsk);
        snprintf(elkey, sizeof elkey, "FP:%s", k47);
        element_t el;
        snprintf(el.sensor, sizeof el.sensor, "%s", elkey);
        value(el.sensor, d, sizeof d, &bar);
        CHECK(strcmp(d, "33\xB0" "C") == 0, "47-char key survives the element field and matches (got '%s')", d);
        CHECK(g_fp_count == 1, "over-long key row dropped instead of truncated");
    }

    /* --- more rows than fit: first FP_MAX kept, no overflow --- */
    {
        static char rows[FP_MAX * 2 * 48];
        rows[0] = 0;
        for (int i = 0; i < FP_MAX + 20; i++) {
            char line[64];
            snprintf(line, sizeof line, "t.s%d/temp1\ttemp\tS\ts\t30\n", i);
            strcat(rows, line);
        }
        write_snapshot(now, rows);
        CHECK(g_fp_count == FP_MAX, "%d rows -> capped at FP_MAX=%d", FP_MAX + 20, FP_MAX);
    }

    /* --- universal (2026-11-16): any PC FanPilot supports --- */
    {
        /* A sensor-rich PC (many drives, DIMMs, CCDs, two GPUs): FanPilot writes
           temperatures first and fans last -- the fan rows must still be read. */
        static char rows[64 * 1024];
        rows[0] = 0;
        for (int i = 0; i < 200; i++) {
            char line[64];
            snprintf(line, sizeof line, "t.drivetemp@%d:0:0:0/temp1\ttemp\tHDD\tDrive %d\t30\n", i, i);
            strcat(rows, line);
        }
        /* real id shapes from FanPilot 1.0.x: duplicate chips get @<device>, NVIDIA @<bus id> */
        strcat(rows, "rpm.nct6798@nct6775.656/pwm7\trpm\tFAN7\tPump (W_PUMP+) speed\t2400\n");
        strcat(rows, "pct.nct6798@nct6775.656/pwm7\tpct\tFN7%\tPump (W_PUMP+) duty\t60\n");
        strcat(rows, "t.nvidia@00000000:01:00.0/temp1\ttemp\tGPU\tNVIDIA GeForce RTX 4070\t47.0\n");
        strcat(rows, "rpm.amdgpu@0000:03:00.0/fan1\trpm\tGPUF\tGraphics card fan (0000:03:00.0) speed\t900\n");
        strcat(rows, "t.k10temp/temp1\ttemp\tTOOLONG\tCPU (Tctl)\t50\n");
        {   /* a user-chosen long name (~400 bytes) is still a valid line */
            char line[600];
            char name[401];
            memset(name, 'n', 400); name[400] = 0;
            snprintf(line, sizeof line, "rpm.it8688/pwm1\trpm\tCPUF\t%s\t1500\n", name);
            strcat(rows, line);
        }
        write_snapshot(now, rows);
        CHECK(g_fp_count == 206, "200 temps + 6 more rows all kept (got %d, FP_MAX=%d)", g_fp_count, FP_MAX);
        value("FP:rpm.nct6798@nct6775.656/pwm7", d, sizeof d, &bar);
        CHECK(strcmp(d, "2400") == 0, "fan row after 200 temperatures still read (got '%s')", d);
        value("FP:pct.nct6798@nct6775.656/pwm7", d, sizeof d, &bar);
        CHECK(strcmp(d, "60%") == 0 && bar == 60, "duty of a chip@device fan (got '%s')", d);
        value("FP:t.nvidia@00000000:01:00.0/temp1", d, sizeof d, &bar);
        CHECK(strcmp(d, "47\xB0" "C") == 0, "NVIDIA id with bus id (colons, dots) (got '%s')", d);
        value("FP:rpm.amdgpu@0000:03:00.0/fan1", d, sizeof d, &bar);
        CHECK(strcmp(d, "900") == 0, "second AMD card fan (got '%s')", d);
        value("FP:rpm.it8688/pwm1", d, sizeof d, &bar);
        CHECK(strcmp(d, "1500") == 0, "400-byte long label: line still read (got '%s')", d);
        const sensor_def_t *ld = find_sensor("FP:t.k10temp/temp1");
        CHECK(ld && strcmp(ld->label, "TOOL") == 0, "short label cut to 4 characters (got '%s')", ld ? ld->label : "?");

        /* review 2026-11-16: cut after 4 UTF-8 characters, never inside one */
        write_snapshot(now, "t.u/temp1\ttemp\tab\xE2\x82\xAC" "d\xE2\x82\xAC\tu\t30\n");
        ld = find_sensor("FP:t.u/temp1");
        CHECK(ld && strcmp(ld->label, "ab\xE2\x82\xAC" "d") == 0, "UTF-8 short label: 4 whole characters");

        /* CRLF: 510 content bytes + \r doesn't fit, 509 + \r does (the app agrees) */
        {
            static char crlf[2048];
            char pad[520];
            int base = (int)strlen("t.c1/temp1\ttemp\tC\t\t30");
            memset(pad, 'p', 510 - base); pad[510 - base] = 0;
            snprintf(crlf, sizeof crlf, "t.c1/temp1\ttemp\tC\t%s\t30\r\nt.c2/temp1\ttemp\tC\t%s\t30\r\n", pad, pad + 1);
            write_snapshot(now, crlf);
            CHECK(fanpilot_index("t.c1/temp1") < 0 && fanpilot_index("t.c2/temp1") >= 0,
                  "CRLF line: 510 bytes + \\r dropped, 509 + \\r kept");
        }
    }

    /* --- reads at most once per second --- */
    write_snapshot(now, ROWS);
    g_fp_loaded = time(NULL);
    write_file("# fanpilot-sensors 1 0\n");          /* would empty the table if re-read */
    g_fp_loaded = time(NULL);                         /* pretend that load happened earlier this second */
    write_snapshot(now, ROWS);                        /* restore, then check cache keeps it */
    g_fp_loaded = time(NULL);
    unlink(g_path);
    CHECK(fanpilot_index("t.k10temp/temp1") >= 0, "cached within the same second (no re-read per frame)");

    /* --- custom_screens.txt keeps the full, longer FP: key --- */
    {
        char home[300], dir[400], cs[500];
        snprintf(home, sizeof home, "%s/home", TEST_SCRATCH);
        snprintf(dir, sizeof dir, "%s/.local/share/g510-lcd", home);
        char cmd[600];
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
        if (system(cmd) != 0) { printf("FAIL mkdir\n"); return 1; }
        snprintf(cs, sizeof cs, "%s/custom_screens.txt", dir);
        FILE *f = fopen(cs, "w");
        fputs("SCREEN L2\nELEMENT sensor=FP:rpm.it8688#2/pwm3 style=number x=1 y=2 width=40\n"
              "ELEMENT sensor=FP:t.k10temp/temp1 style=bar x=1 y=12 width=50\n", f);
        fclose(f);
        setenv("HOME", home, 1);
        load_custom_screens();
        CHECK(custom_screens[0].count == 2 && strcmp(custom_screens[0].elements[0].sensor, "FP:rpm.it8688#2/pwm3") == 0
              && strcmp(custom_screens[0].elements[1].style, "bar") == 0,
              "custom_screens.txt: long FP: key kept whole (got '%s')", custom_screens[0].elements[0].sensor);
    }

    printf("fanpilot sensors: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
