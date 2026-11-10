/* Rendering/efficiency regression tests (added 2026-11-10 audit):
   1) measure_builtin_text_width() (reused scratch canvas + packed-buffer
      scan) returns exactly what the old per-call canvas + full
      getPixel() scan returned, for every built-in font and many strings;
   2) the Goertzel visualizer bin magnitude equals the old direct
      cos()/sin() correlation;
   3) sleep_or_screen_change() returns within milliseconds of the
      screen-state file being rewritten (L-button latency), and sleeps
      the full time otherwise.
   Never touches the real LCD, the live service, or real state files. */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main
#include <sys/wait.h>

#ifndef TEST_SCRATCH
#error "build with -DTEST_SCRATCH=\"/some/dir\" (tests/run-tests.sh does)"
#endif

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { printf("PASS "); passes++; } else { printf("FAIL "); fails++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

/* the pre-2026-11-10 implementation, verbatim in behaviour */
static int old_measure(const char *s, int font_size) {
    if (!s[0]) return 0;
    g15canvas scratch G15_CANVAS_AUTO = {0};
    g15r_initCanvas(&scratch);
    g15r_renderString(&scratch, (unsigned char*)s, 0, font_size, 0, 0);
    int max_x = -1;
    for (int y = 0; y < G15_LCD_HEIGHT; y++)
        for (int x = 0; x < G15_LCD_WIDTH; x++)
            if (g15r_getPixel(&scratch, x, y) && x > max_x) max_x = x;
    return max_x + 1;
}

static double old_dft(short *samples, int n, double freq_hz) {
    double omega = 2.0 * M_PI * freq_hz / VIZ_SAMPLE_RATE, real = 0, imag = 0;
    for (int i = 0; i < n; i++) {
        double x = samples[i] / 32768.0;
        real += x * cos(omega * i);
        imag -= x * sin(omega * i);
    }
    return sqrt(real * real + imag * imag) / n;
}

static long now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(void) {
    /* 1) width measurement */
    const char *strs[] = {"", "A", "i", "W", "CPU 100%", "Muneccim | YouTube Music",
                          "88:88:88", "...........", "The quick brown fox jumps over the lazy dog",
                          "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", "0.1G", "\xB0" "C", " lead", "trail "};
    int mismatches = 0, cases = 0;
    for (int fnt = G15_TEXT_SMALL; fnt <= G15_TEXT_HUGE; fnt++)
        for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
            int a = old_measure(strs[i], fnt), b = measure_builtin_text_width(strs[i], fnt);
            cases++;
            if (a != b) { mismatches++; printf("     mismatch font %d '%s': old %d new %d\n", fnt, strs[i], a, b); }
        }
    CHECK(mismatches == 0, "text width: new == old in all %d font/string cases", cases);
    char t1[64] = "The quick brown fox jumps over the lazy dog", t2[64];
    snprintf(t2, sizeof(t2), "%s", t1);
    truncate_builtin_text(t1, G15_TEXT_MED, 80);
    CHECK(measure_builtin_text_width(t1, G15_TEXT_MED) <= 80 && strlen(t1) < strlen(t2), "truncation fits 80 px ('%s')", t1);

    /* 2) Goertzel == direct DFT */
    viz_init_bar_freqs();
    short sig[VIZ_WINDOW_SAMPLES];
    for (int i = 0; i < VIZ_WINDOW_SAMPLES; i++)
        sig[i] = (short)(8000 * sin(2 * M_PI * 440 * i / VIZ_SAMPLE_RATE) + 3000 * sin(2 * M_PI * 3100 * i / VIZ_SAMPLE_RATE)
                         + (rand() % 2000 - 1000));
    double worst = 0;
    for (int b = 0; b < VIZ_NUM_BARS; b++) {
        double a = old_dft(sig, VIZ_WINDOW_SAMPLES, g_viz_bar_freqs[b]);
        double g = viz_dft_bin_magnitude(sig, VIZ_WINDOW_SAMPLES, g_viz_bar_freqs[b]);
        double rel = fabs(a - g) / (a > 1e-12 ? a : 1e-12);
        if (rel > worst) worst = rel;
    }
    CHECK(worst < 1e-9, "visualizer: Goertzel == direct DFT on all %d bars (worst relative error %.2e)", VIZ_NUM_BARS, worst);

    /* 3) screen-change wake */
    setenv("XDG_RUNTIME_DIR", TEST_SCRATCH, 1);
    screen_watch_init();
    CHECK(g_screen_ifd >= 0, "inotify watch on the runtime dir");
    long t0 = now_ms();
    sleep_or_screen_change(300);
    long slept = now_ms() - t0;
    CHECK(slept >= 290 && slept < 400, "no change: sleeps the full 300 ms (%ld ms)", slept);
    pid_t child = fork();
    if (child == 0) {
        usleep(100000);
        FILE *f = fopen(screen_state_path(), "w");
        if (f) { fputs("3", f); fclose(f); }
        _exit(0);
    }
    t0 = now_ms();
    sleep_or_screen_change(2000);
    slept = now_ms() - t0;
    waitpid(child, NULL, 0);
    CHECK(slept >= 90 && slept < 300, "screen file rewritten after 100 ms: woke after %ld ms (not 2000)", slept);
    CHECK(read_screen() == 3, "and the new screen (3) reads back");

    /* N/A formatting for missing sensors */
    char d[32]; double pct = 0;
    format_temp(-1, &pct, d, sizeof(d));
    CHECK(strcmp(d, "N/A") == 0, "missing temperature renders as N/A");

    printf("\nrender: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
