#ifndef G510_AUDIO_VISUALIZER_H
#define G510_AUDIO_VISUALIZER_H
/* Real-time audio bar visualizer for Custom Screens. Captures whatever
   is actually playing through this machine's speakers via a persistent
   `parec` process reading the PipeWire/PulseAudio default sink's
   MONITOR -- this is what makes it work with every player uniformly
   (Spotify, Brave, VLC, anything): it hears system audio OUTPUT, not
   any one application's stream, so it never needs per-player
   integration. Direct request: "it should be compatible with all
   kinds of players, like Brave browser."

   @DEFAULT_MONITOR@ is a real PulseAudio/PipeWire-pulse special device
   alias -- verified directly (not guessed) against this machine's real
   audio devices: the actual monitor source names here are hardware-
   specific (e.g. "alsa_output.usb-Logitech_G510s_..." tied to this
   exact keyboard's USB audio device), so hardcoding one would break on
   any other machine. @DEFAULT_MONITOR@ always resolves to whatever the
   CURRENT default output device's monitor is, confirmed with a real
   parec capture against it.

   REAL BUG FOUND AND FIXED (this is the second capture design, not the
   first): the original version had `parec` write to STDOUT and read it
   via a `popen()` pipe. That looked fine in isolated tests (redirecting
   parec's stdout straight to a FILE via a plain shell command always
   produced real audio data), but reading the SAME command's output
   through a pipe/FIFO instead of a real file consistently produced
   silence -- confirmed via direct comparison: piping through an
   anonymous pipe (popen) -> all zero bytes; piping through a named FIFO
   -> also all zero bytes; redirecting to a regular file, read back with
   plain fopen/fread -> real, correct, non-zero audio data, every single
   time this was tested. This points to `parec` (or the PipeWire-pulse
   client library underneath it) negotiating a much larger write-buffer/
   flush threshold when its output is detected as a pipe-like fd versus
   a plain file -- not something this program can control from the
   read side, so the fix works around it by never using a pipe at all.

   Current design: `parec` is launched via fork()+execlp() (a real
   child PID tracked directly -- this project has a standing rule
   against fragile `pkill` pattern-matching after being bitten by it
   before, see G510_README.md) writing continuously to a real file
   under $XDG_RUNTIME_DIR (same convention as screen_state_path()
   elsewhere in this file). The file is small (a rolling ~1.3MB per 30s
   at this sample rate) and gets truncated by restarting the capture
   process periodically -- VIZ_RESTART_SEC controls both the disk-usage
   bound and how often a brief (~1-1.5s, matching the real observed
   buffer-fill delay) gap in data occurs right after each restart.

   The monitor source is SUSPENDED (produces no data) whenever nothing
   is playing -- confirmed real PipeWire behavior on this machine, not
   a bug. Handled by "not enough new data yet" being treated the same
   as during a restart gap: bars decay toward zero instead of freezing
   or showing stale data. */

#include <unistd.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <time.h>

/* Upper bound on how many frequency bins the DFT engine ever computes
   -- the actual number of bars DRAWN on screen is computed dynamically
   from the visualizer element's real width (see draw_visualizer_element
   in g510_lcd_stats.c), capped at this. Direct report after dragging
   the element wider and seeing no change: "i dragged it longer and did
   not extend. more lines, less think, match more frequencies" -- a
   fixed bar count meant resizing just made each bar WIDER, not more
   numerous. 20 covers the full 160px LCD width at a thin ~4px/bar
   without ever running short of real bins to draw. */
#define VIZ_NUM_BARS 32
#define VIZ_WINDOW_SAMPLES 512
#define VIZ_SAMPLE_RATE 44100
/* Calibrated against real playing audio via a standalone harness
   (called update_visualizer() in a loop, printed g_viz_bars[] every
   300ms while real music played through this machine's speakers) --
   NOT guessed, three separate rounds so far, each against genuinely
   different real content: a 440Hz test tone (peaked ~0.11); one song
   (typical ~0.02-0.04, peaks ~0.07-0.09); a second, quieter song
   (typical ~0.01-0.03, peaks only ~0.04-0.05 -- confirmed directly:
   "barely see the lines rising on this song"). Real songs vary in
   loudness/mastering more than a single calibration constant can
   perfectly cover -- 25.0 is picked to make the quieter song's
   typical content reach a clearly-visible mid height without
   over-saturating a louder song's peaks. If a future song still looks
   too flat or too pinned-at-max, that's this same real tradeoff, not
   a new bug -- an adaptive/auto-gain scale (tracking a rolling
   loudness average instead of one fixed constant) would fix it
   properly but is a bigger change than this pass covers.

   Direct follow-up after the treble-boost pass above: "add a tiny
   bit of reduction on them so they dont max out so fast" -- measured
   directly against real, currently-playing music: several bins were
   routinely hitting 2-4x the clamp threshold during normal (not even
   especially loud) passages, confirmed via a standalone harness
   printing g_viz_bars[]*VIZ_SCALE*100 every ~300ms. Pulled back
   25.0 -> 21.0 (~16%, a real "tiny bit" against that measured
   overshoot, not a full rebalance) -- verified via --preview against
   the same live audio that it still looks lively, just pins at max
   less constantly. */
#define VIZ_SCALE 21.0
/* How often the capture process is killed and restarted with a fresh,
   truncated file. Bounds disk usage (44100 * 2 bytes/sec * this many
   seconds) and, as an unavoidable side effect, causes a brief real
   gap in data right after each restart while the new process's write
   buffer fills back up -- verified this delay is ~1-1.5s in practice,
   short enough that bars simply decay briefly rather than looking
   broken. Longer intervals make that gap rarer at the cost of more
   disk (30s is ~2.6MB, trivial either way). */
#define VIZ_RESTART_SEC 30

static pid_t g_viz_pid = -1;
static time_t g_viz_started = 0;
static long g_viz_read_offset = 0;
static short g_viz_ring[VIZ_WINDOW_SAMPLES];
static double g_viz_bars[VIZ_NUM_BARS] = {0};
/* Log-spaced bar center frequencies, 100Hz-8kHz -- more resolution in
   the bass/mid range where most music's perceptible energy lives,
   matching how real hardware/software equalizers are laid out. Not a
   precision spectrum analyzer -- tuned for a visually lively effect,
   verified to correctly distinguish "tone present" from "silence" and
   to respond to the right general region of the spectrum, not exact
   scientific bin placement. */
static double g_viz_bar_freqs[VIZ_NUM_BARS];

/* Direct request, and confirmed NOT a bug first: "most of the left
   bars are up while the rest on the right are not. is this
   intended?" -- verified with a real 3kHz test tone played through
   the speakers that the high-frequency bins genuinely respond
   correctly (that bin spiked 4-5x above baseline); real music just
   naturally carries far less energy up there than in the bass/mids,
   same reason any real hardware equalizer looks more alive on the
   left. Direct follow-up: "do make the right side a bit more
   sensitive so it shows up a bit more. while proportions are kept
   the same" -- a smooth per-bin GAIN (not a normalize/flatten) so
   each bin's own frame-to-frame ups and downs stay proportional to
   what they really are, just uniformly amplified more the higher the
   bin's frequency is.

   Then, even after VIZ_SCALE itself was pulled back (see its own
   comment): "still kinda maxes out too fast" -- honest tradeoff
   flagged directly before this change: a single fixed VIZ_SCALE
   can't make both "loud bass-heavy songs don't max out" AND "quiet
   songs still show life" fully true at once (that needs real
   adaptive/auto-gain, a bigger change). What a fixed-curve change
   CAN do cheaply: also trim the BASS end down a little (not just
   boost treble up), same linear-in-t gain curve extended to run from
   VIZ_BASS_TRIM (<1x) at the lowest bin through 1.0x at low-mid up to
   VIZ_TREBLE_BOOST_MAX (>1x) at the highest bin, instead of starting
   flat at 1.0x. Proportions within each bin are still preserved
   (still a fixed multiplier per bin, not a normalize), just the
   overall left/right balance is shifted a bit further. */
#define VIZ_BASS_TRIM 0.8
#define VIZ_TREBLE_BOOST_MAX 2.5
static double g_viz_bin_gain[VIZ_NUM_BARS];

static void viz_init_bar_freqs(void) {
    double lo = 100.0, hi = 8000.0;
    for (int i = 0; i < VIZ_NUM_BARS; i++) {
        double t = (double)i / (VIZ_NUM_BARS - 1);
        g_viz_bar_freqs[i] = lo * pow(hi / lo, t);
        g_viz_bin_gain[i] = VIZ_BASS_TRIM + (VIZ_TREBLE_BOOST_MAX - VIZ_BASS_TRIM) * t;
    }
}

static const char *viz_capture_path(void) {
    static char path[256];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    snprintf(path, sizeof(path), "%s/g510lcd_viz_capture.raw", runtime ? runtime : "/tmp");
    return path;
}

static void viz_start_capture(void) {
    viz_init_bar_freqs();
    unlink(viz_capture_path());
    g_viz_read_offset = 0;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        /* ORPHAN FIX: the SIGTERM/SIGINT handler below can't run if this
           process dies by SIGKILL (a systemd MemoryMax kill, `kill -9`, a
           debugger's `kill`) or a crash -- and then this parec kept
           recording forever into a DELETED file on the RAM-backed
           /run/user tmpfs (~170MB/hour each, invisible to `ls`). Found
           when test runs ended by SIGKILL left 3 of them behind. Ask the
           kernel to SIGTERM us whenever the parent dies, however it dies;
           the getppid() check closes the fork()->prctl() race. */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(1);
        /* Child: redirect stderr to /dev/null (parec logs connection
           messages there on every start/stop that would otherwise spam
           this service's journal every VIZ_RESTART_SEC). */
        FILE *devnull = fopen("/dev/null", "w");
        if (devnull) dup2(fileno(devnull), 2);
        /* --latency-msec is a real, directly measured fix, not a
           guess: without it, this capture file was observed growing
           in ~65KB (~0.7s of audio) bursts roughly every 0.6-0.7s --
           confirmed by polling its size every 50ms -- meaning every
           bar the LCD drew was reading audio up to ~0.7s stale, far
           more lag than the 100ms draw loop itself. With
           --latency-msec=50 the same test showed near-continuous
           small writes (a few KB) almost every poll cycle instead.
           Direct request: "i need the visualizer respons faster". */
        execlp("parec", "parec", "-d", "@DEFAULT_MONITOR@", "--format=s16le",
               "--rate=44100", "--channels=1", "--file-format=raw",
               "--latency-msec=50", viz_capture_path(), (char*)NULL);
        _exit(127); /* only reached if execlp itself failed */
    }
    g_viz_pid = pid; /* pid == -1 on a failed fork() -- update_visualizer()'s pid<=0 check handles that */
    g_viz_started = time(NULL);
}

static void viz_stop_capture(void) {
    if (g_viz_pid > 0) {
        kill(g_viz_pid, SIGTERM);
        waitpid(g_viz_pid, NULL, 0);
        g_viz_pid = -1;
    }
    unlink(viz_capture_path());
}

/* Real resource-leak bug found while testing the periodic-restart
   logic: without this, killing the daemon (systemctl restart, a
   crash, anything sending SIGTERM/SIGINT) left the forked parec child
   running forever as an orphan -- confirmed directly, multiple
   restarts during testing accumulated multiple leaked parec
   processes. main() must call signal(SIGTERM/SIGINT,
   viz_signal_cleanup) once at startup. */
static void viz_signal_cleanup(int sig) {
    viz_stop_capture();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Single-bin DFT magnitude via direct correlation against one target
   frequency -- cheap since only VIZ_NUM_BARS specific bins are ever
   needed (~512*20 = 10240 multiply-adds per update, negligible even
   at the ~10fps this now runs at for visualizer-showing screens), not
   the O(N log N) full spectrum a real FFT would compute for bins
   nothing ever reads. */
static double viz_dft_bin_magnitude(short *samples, int n, double freq_hz) {
    double omega = 2.0 * M_PI * freq_hz / VIZ_SAMPLE_RATE;
    double real = 0, imag = 0;
    for (int i = 0; i < n; i++) {
        double s = samples[i] / 32768.0;
        real += s * cos(omega * i);
        imag -= s * sin(omega * i);
    }
    return sqrt(real * real + imag * imag) / n;
}

/* Called once per draw cycle (same pattern as update_net_speed()).
   Restarts the capture process on its own schedule (see
   VIZ_RESTART_SEC), then reads whatever new bytes have accumulated in
   the capture file since the last call and re-runs the DFT on the
   latest window. If there's no new data yet (right after a restart,
   before parec's own buffer has filled, or if the monitor source is
   SUSPENDED because nothing is playing -- confirmed real PipeWire
   behavior, not an error), bars decay toward zero instead of freezing
   or showing stale data forever. */
static void update_visualizer(void) {
    if (g_viz_pid <= 0) {
        viz_start_capture();
        return;
    }
    if (difftime(time(NULL), g_viz_started) >= VIZ_RESTART_SEC) {
        viz_stop_capture();
        viz_start_capture();
        return;
    }

    FILE *f = fopen(viz_capture_path(), "rb");
    if (!f) {
        for (int i = 0; i < VIZ_NUM_BARS; i++) g_viz_bars[i] *= 0.85;
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long available = size - g_viz_read_offset;
    if (available < (long)sizeof(g_viz_ring)) {
        /* Not enough new data yet -- normal right after a restart, or
           while the monitor source is SUSPENDED (silence). Decay
           instead of freezing. */
        fclose(f);
        for (int i = 0; i < VIZ_NUM_BARS; i++) g_viz_bars[i] *= 0.85;
        return;
    }
    /* Always read the MOST RECENT window, not strictly the next
       unread bytes -- if the reader ever falls behind (shouldn't
       normally happen at this poll rate, but avoids an unbounded
       backlog if it does), this naturally catches back up to "now"
       rather than playing catch-up through stale audio. */
    fseek(f, size - (long)sizeof(g_viz_ring), SEEK_SET);
    size_t got = fread(g_viz_ring, 1, sizeof(g_viz_ring), f);
    fclose(f);
    g_viz_read_offset = size;
    if (got < sizeof(g_viz_ring)) return; /* short read -- try again next cycle */

    for (int i = 0; i < VIZ_NUM_BARS; i++) {
        double mag = viz_dft_bin_magnitude(g_viz_ring, VIZ_WINDOW_SAMPLES, g_viz_bar_freqs[i]) * g_viz_bin_gain[i];
        /* Light smoothing (70% new / 30% old) -- responsive but not
           flickery frame to frame. */
        /* Direct request: "i wished more bard would be rising, Winamp
           style" -- less smoothing (was 0.3 old/0.7 new) makes each
           bar react more visibly frame to frame instead of blending
           toward an average. Honest caveat, unlike the other fixes in
           this pass: nothing was playing at the time this was tuned
           (couldn't verify against real live audio), so this is a
           reasoned best guess based on how smoothing works, not
           independently confirmed livelier -- worth another real
           listen. */
        g_viz_bars[i] = g_viz_bars[i] * 0.15 + mag * 0.85;
    }
}

/* --preview is a one-shot render (the GUI editor's live-preview
   mechanism). Direct request: "id love to have the visualiser LIVE on
   the software so i can see it myself remotely how and if it works" --
   since real capture now happens in a persistent file-backed process
   (started by the live systemd service, if it's running), a one-shot
   --preview invocation can just READ that same shared capture file
   directly for a real, live snapshot, without itself needing to spawn
   or manage any capture process. Only falls back to a fixed placeholder
   pattern if that file doesn't exist yet or doesn't have a full
   window's worth of data (e.g. the live service was just started and
   parec's buffer hasn't filled, or the service isn't running at all --
   never crashes either way, just shows something reasonable). */
static void viz_set_preview_placeholder(void) {
    viz_init_bar_freqs();
    FILE *f = fopen(viz_capture_path(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        if (size >= (long)sizeof(g_viz_ring)) {
            fseek(f, size - (long)sizeof(g_viz_ring), SEEK_SET);
            size_t got = fread(g_viz_ring, 1, sizeof(g_viz_ring), f);
            fclose(f);
            if (got == sizeof(g_viz_ring)) {
                for (int i = 0; i < VIZ_NUM_BARS; i++) {
                    g_viz_bars[i] = viz_dft_bin_magnitude(g_viz_ring, VIZ_WINDOW_SAMPLES, g_viz_bar_freqs[i]) * g_viz_bin_gain[i];
                }
                return;
            }
        } else {
            fclose(f);
        }
    }
    /* Fixed, varied fallback pattern -- purely so the editor shows the
       bars' real position/size even with no live data available yet. */
    double placeholder[8] = {0.3, 0.6, 0.9, 0.5, 0.2, 0.7, 1.0, 0.4};
    for (int i = 0; i < VIZ_NUM_BARS; i++) g_viz_bars[i] = placeholder[i % 8] / VIZ_SCALE;
}

#endif
