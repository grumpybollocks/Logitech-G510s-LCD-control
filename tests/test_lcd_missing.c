/* "LCD device missing" logging test (added 2026-11-02): with the keyboard
   unplugged, send_frame() must log once when the device goes away and once
   when it comes back -- not once per frame (20/s on a visualizer screen).
   The device path is redirected (build-time G510_LCD_DEV) into a scratch
   directory whose parent is removed to make it unopenable, the same way
   /dev/g510-lcd is unopenable for a normal user when the symlink is gone.
   Note: fopen("wb") would simply CREATE a missing file in a writable dir,
   which is why the parent directory itself is removed. */
#ifndef TEST_SCRATCH
#error "build with -DTEST_SCRATCH=\"/some/tmp/dir\" (tests/run-tests.sh does this)"
#endif
#define DEVDIR TEST_SCRATCH "/fakedev"
#define G510_LCD_DEV DEVDIR "/g510-lcd"
#define G510_LCD_NO_ID_CHECK  /* the fake device is a FIFO, not a hidraw node */
#define main lcd_main
#include "../src/g510_lcd_stats.c"
#undef main

static int count_lines(const char *p) {
    FILE *f = fopen(p, "r"); if (!f) return -1;
    int n = 0, c; while ((c = fgetc(f)) != EOF) if (c == '\n') n++;
    fclose(f); return n;
}

int main(void) {
    const char *log = TEST_SCRATCH "/stderr.log";
    unlink(G510_LCD_DEV); rmdir(DEVDIR);
    if (!freopen(log, "w", stderr)) return 2;
    g15canvas canvas G15_CANVAS_AUTO = {0};
    g15r_initCanvas(&canvas);
    for (int i = 0; i < 50; i++) send_frame(&canvas);                         /* missing */
    fflush(stderr); int a = count_lines(log);
    /* appears: a FIFO with a reader. send_frame() keeps its fd open across
       frames (2026-11-10), so "gone again" must make WRITES fail the way a
       real hidraw node does after an unplug (ENODEV) -- a regular file would
       keep accepting writes on the open fd after being deleted. Closing the
       reader makes the next write fail (EPIPE; SIGPIPE ignored here). */
    signal(SIGPIPE, SIG_IGN);
    mkdir(DEVDIR, 0755); mkfifo(G510_LCD_DEV, 0600);
    int reader = open(G510_LCD_DEV, O_RDONLY | O_NONBLOCK);
    for (int i = 0; i < 50; i++) send_frame(&canvas);                         /* 50 x 992 B fits the pipe */
    fflush(stderr); int b = count_lines(log);
    close(reader); unlink(G510_LCD_DEV); rmdir(DEVDIR);                         /* gone again */
    for (int i = 0; i < 50; i++) send_frame(&canvas);
    fflush(stderr); int c = count_lines(log);
    int fails = 0;
    printf("%s 50 frames, device missing -> %d log line(s) (want 1)\n", a == 1 ? "PASS" : (fails++, "FAIL"), a);
    printf("%s 50 frames, device back    -> %d new line(s) (want 1)\n", b - a == 1 ? "PASS" : (fails++, "FAIL"), b - a);
    printf("%s 50 frames, missing again  -> %d new line(s) (want 1)\n", c - b == 1 ? "PASS" : (fails++, "FAIL"), c - b);
    printf("\nLCD-missing logging: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
