/* libg15render.so on this system was built with FreeType/TTF support
   compiled in, which adds extra fields to g15canvas (see the #ifdef
   TTF_SUPPORT block in the header). Without defining this here too, our
   compilation sees a SMALLER g15canvas struct than the library actually
   writes to -- g15r_initCanvas() then writes past the end of our
   stack-allocated struct. This was a latent bug in the original code too
   (it just happened to land on harmless stack padding); confirmed via
   AddressSanitizer + a stack-protector trip while adding v1.1 features. */
#define TTF_SUPPORT
#include <ft2build.h>
#include FT_FREETYPE_H
#include <libg15render.h>
#include "font_sanity.h"
#include "audio_visualizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <dirent.h>

/* Exact port of libg15's dumpPixmapIntoLCDFormat(): converts libg15render's
   row-major MSB-first bitmap into the LCD's vertical "page" wire format. */
static void dump_to_lcd_format(unsigned char *lcd_buffer, unsigned char const *data) {
    unsigned int output_offset = 32;
    unsigned int base_offset = 0;
    unsigned int curr_row, curr_col;

    for (curr_row = 0; curr_row < 6; ++curr_row) {
        for (curr_col = 0; curr_col < 160; ++curr_col) {
            unsigned int bit = curr_col % 8;
            lcd_buffer[output_offset] =
                (((data[base_offset]        << bit) & 0x80) >> 7) |
                (((data[base_offset + 20]   << bit) & 0x80) >> 6) |
                (((data[base_offset + 40]   << bit) & 0x80) >> 5) |
                (((data[base_offset + 60]   << bit) & 0x80) >> 4) |
                (((data[base_offset + 80]   << bit) & 0x80) >> 3) |
                (((data[base_offset + 100]  << bit) & 0x80) >> 2) |
                (((data[base_offset + 120]  << bit) & 0x80) >> 1) |
                (((data[base_offset + 140]  << bit) & 0x80) >> 0);
            ++output_offset;
            if (bit == 7) base_offset++;
        }
        base_offset += 160 - 20;
    }
}

static void send_frame(g15canvas *canvas) {
    unsigned char report[992];
    memset(report, 0, 32);
    report[0] = 0x03;
    dump_to_lcd_format(report, canvas->buffer);

    /* Stable symlink from the udev rule (99-g510-lcd.rules) -- the raw
       hidraw number can shift across reboots/replugs, this doesn't. */
    FILE *f = fopen("/dev/g510-lcd", "wb");
    if (!f) { perror("open /dev/g510-lcd"); return; }
    fwrite(report, 1, 992, f);
    fclose(f);
}

/* Renders the canvas to a plain PPM image instead of the real hardware --
   used by --preview so the GUI's editor pane shows pixel-for-pixel exactly
   what the real LCD would show, using the identical drawing code path.
   Colors are tinted to evoke the real G510's green-on-black panel. */
static void write_ppm(g15canvas *c, const char *path) {
    /* Written to path+".tmp" then rename()'d into place atomically --
       a real screenshot caught the reader side (render_preview() in
       g510_app.py) showing two different frames' content visibly
       overlapping in one image, exactly what a torn read of a
       direct in-place fopen(path,"wb") can produce if a reader opens
       the file mid-write. rename() on POSIX is atomic when source and
       destination share a filesystem (always true here, both under
       the same /tmp path a caller passed in), so any concurrent
       reader sees either the complete old file or the complete new
       one, never a mix -- true regardless of exactly what triggers
       the overlap, which wasn't fully pinned down. */
    char tmp_path[300];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *f = fopen(tmp_path, "wb");
    if (!f) { perror("open preview output"); return; }
    fprintf(f, "P6\n%d %d\n255\n", G15_LCD_WIDTH, G15_LCD_HEIGHT);
    for (int y = 0; y < G15_LCD_HEIGHT; y++) {
        for (int x = 0; x < G15_LCD_WIDTH; x++) {
            int lit = g15r_getPixel(c, x, y) == G15_COLOR_BLACK;
            unsigned char px[3];
            if (lit) { px[0] = 20; px[1] = 40; px[2] = 15; }
            else     { px[0] = 190; px[1] = 214; px[2] = 145; }
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    rename(tmp_path, path);
}

/* --- stat readers --- */

static void read_cpu_totals(unsigned long long *idle, unsigned long long *total) {
    FILE *f = fopen("/proc/stat", "r");
    /* Real bug found by full-file audit: every OTHER stat reader in
       this file (get_cpu_temp_c, get_vram_bytes, get_swap_percent,
       get_uptime_hours, etc.) checks fopen() for NULL before using it;
       this one and get_ram_kb below didn't -- confirmed via a direct
       isolated repro (fscanf/fgets on a NULL FILE*) that this is a
       real SIGSEGV, not just a theoretical gap, and this is on the
       DEFAULT stats screen's hot path (draw_stats_screen calls both
       every redraw), not an obscure corner case. */
    if (!f) { *idle = 0; *total = 0; return; }
    unsigned long long user = 0, nice = 0, sys = 0, idl = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    int fields = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &user, &nice, &sys, &idl, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (fields != 8) { *idle = 0; *total = 0; return; }
    *idle = idl + iowait;
    *total = user + nice + sys + idl + iowait + irq + softirq + steal;
}

static double get_cpu_percent(void) {
    static unsigned long long prev_idle = 0, prev_total = 0;
    unsigned long long idle, total;
    read_cpu_totals(&idle, &total);
    if (total < prev_total || idle < prev_idle) {
        prev_idle = idle;
        prev_total = total;
        return 0.0;
    }
    unsigned long long dt = total - prev_total;
    unsigned long long di = idle - prev_idle;
    double pct = (dt > 0 && di <= dt)
        ? (100.0 * (double)(dt - di) / (double)dt) : 0.0;
    prev_idle = idle; prev_total = total;
    return pct;
}

/* returns used/total RAM in KB via MemTotal/MemAvailable */
static void get_ram_kb(long *used_kb, long *total_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { *used_kb = 0; *total_kb = 0; return; } /* same class of bug as read_cpu_totals above -- see its comment */
    char label[64];
    long value;
    long mem_total = 0, mem_avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s %ld", label, &value) == 2) {
            if (strcmp(label, "MemTotal:") == 0) mem_total = value;
            else if (strcmp(label, "MemAvailable:") == 0) mem_avail = value;
        }
    }
    fclose(f);
    *total_kb = mem_total;
    *used_kb = mem_total - mem_avail;
}

static int g_nvidia_gpu_temp_c = -1;
static double g_nvidia_gpu_percent = -1;
static unsigned long long g_nvidia_vram_used = 0;
static unsigned long long g_nvidia_vram_total = 0;

static int read_hwmon_temp_c(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int millideg;
    int ok = fscanf(f, "%d", &millideg) == 1;
    fclose(f);
    return ok ? millideg / 1000 : -1;
}

static int contains_case_insensitive(const char *text, const char *needle) {
    if (!text || !needle || !*needle) return 0;
    for (; *text; text++) {
        const char *a = text, *b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int matches_any(const char *text, const char *const *needles, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (contains_case_insensitive(text, needles[i])) return 1;
    return 0;
}

static int read_hwmon_driver_temp(const char *const *drivers, size_t driver_count,
                                  const char *const *labels, size_t label_count,
                                  int allow_first_channel) {
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;
    int fallback = -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hwmon", 5) != 0) continue;
        char path[512], name[128];
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
        FILE *name_file = fopen(path, "r");
        if (!name_file) continue;
        if (!fgets(name, sizeof(name), name_file)) {
            fclose(name_file);
            continue;
        }
        fclose(name_file);
        name[strcspn(name, "\r\n")] = 0;
        if (!matches_any(name, drivers, driver_count)) continue;

        for (int channel = 1; channel <= 32; channel++) {
            char input_path[512], label_path[512], label[128] = "";
            snprintf(input_path, sizeof(input_path),
                     "/sys/class/hwmon/%s/temp%d_input", entry->d_name, channel);
            int temp = read_hwmon_temp_c(input_path);
            if (temp < 0) continue;
            snprintf(label_path, sizeof(label_path),
                     "/sys/class/hwmon/%s/temp%d_label", entry->d_name, channel);
            FILE *label_file = fopen(label_path, "r");
            if (label_file) {
                if (fgets(label, sizeof(label), label_file))
                    label[strcspn(label, "\r\n")] = 0;
                fclose(label_file);
            }
            if (label_count && matches_any(label, labels, label_count)) {
                closedir(dir);
                return temp;
            }
            if (allow_first_channel && fallback < 0) fallback = temp;
        }
    }
    closedir(dir);
    return fallback;
}

static int compare_hwmon_names(const void *left, const void *right) {
    const char *a = left;
    const char *b = right;
    return atoi(a + 5) - atoi(b + 5);
}

static int get_motherboard_temp_c(int index) {
    static const char *const drivers[] = {
        "pch_", "nct", "it87", "w836", "asusec", "asus", "dell_smm", "thinkpad"
    };
    if (index < 0) return -1;
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;
    char hwmon_names[64][256];
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < 64) {
        if (strncmp(entry->d_name, "hwmon", 5) != 0 ||
            !isdigit((unsigned char)entry->d_name[5])) continue;
        size_t name_length = strlen(entry->d_name);
        if (name_length >= sizeof(hwmon_names[count])) continue;
        memcpy(hwmon_names[count], entry->d_name, name_length + 1);
        count++;
    }
    closedir(dir);
    qsort(hwmon_names, count, sizeof(hwmon_names[0]), compare_hwmon_names);

    for (size_t i = 0; i < count; i++) {
        char path[512], name[128];
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", hwmon_names[i]);
        FILE *name_file = fopen(path, "r");
        if (!name_file) continue;
        if (!fgets(name, sizeof(name), name_file)) {
            fclose(name_file);
            continue;
        }
        fclose(name_file);
        name[strcspn(name, "\r\n")] = 0;
        if (!matches_any(name, drivers, sizeof(drivers) / sizeof(drivers[0]))) continue;

        for (int channel = 1; channel <= 32; channel++) {
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_input",
                     hwmon_names[i], channel);
            int temp = read_hwmon_temp_c(path);
            if (temp < 0) continue;
            if (index-- == 0) return temp;
        }
    }
    return -1;
}

static int get_cpu_temp_c(void) {
    static const char *const drivers[] = {"coretemp", "k10temp", "zenpower", "cpu_thermal"};
    static const char *const labels[] = {"package", "tctl", "tdie"};
    return read_hwmon_driver_temp(drivers, sizeof(drivers) / sizeof(drivers[0]),
                                  labels, sizeof(labels) / sizeof(labels[0]), 1);
}

static int get_gpu_temp_c(int sensor) {
    static const char *const drivers[] = {"amdgpu", "nouveau", "nvidia"};
    static const char *const edge_labels[] = {"edge", "gpu core", "gpu"};
    static const char *const hotspot_labels[] = {"junction", "hotspot"};
    static const char *const memory_labels[] = {"mem", "vram"};
    const char *const *labels = edge_labels;
    size_t label_count = sizeof(edge_labels) / sizeof(edge_labels[0]);
    if (sensor == 1) {
        labels = hotspot_labels;
        label_count = sizeof(hotspot_labels) / sizeof(hotspot_labels[0]);
    } else if (sensor == 2) {
        labels = memory_labels;
        label_count = sizeof(memory_labels) / sizeof(memory_labels[0]);
    }
    int temp = read_hwmon_driver_temp(drivers, sizeof(drivers) / sizeof(drivers[0]),
                                      labels, label_count, sensor == 0);
    if (temp >= 0) return temp;
    if (sensor == 0) return g_nvidia_gpu_temp_c;
    return -1;
}

static void get_vram_bytes(unsigned long long *used, unsigned long long *total) {
    *used = 0;
    *total = 0;
    DIR *dir = opendir("/sys/class/drm");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "card", 4) != 0 ||
                !isdigit((unsigned char)entry->d_name[4])) continue;
            char used_path[512], total_path[512];
            snprintf(used_path, sizeof(used_path),
                     "/sys/class/drm/%s/device/mem_info_vram_used", entry->d_name);
            snprintf(total_path, sizeof(total_path),
                     "/sys/class/drm/%s/device/mem_info_vram_total", entry->d_name);
            FILE *f = fopen(used_path, "r");
            if (!f) continue;
            int used_ok = fscanf(f, "%llu", used) == 1;
            fclose(f);
            f = fopen(total_path, "r");
            if (!f) continue;
            int total_ok = fscanf(f, "%llu", total) == 1;
            fclose(f);
            if (used_ok && total_ok && *total > 0) {
                closedir(dir);
                return;
            }
            *used = 0;
            *total = 0;
        }
        closedir(dir);
    }
    if (g_nvidia_vram_total > 0) {
        *used = g_nvidia_vram_used;
        *total = g_nvidia_vram_total;
    }
}

static void format_gb(unsigned long long bytes, char *out, size_t outlen) {
    snprintf(out, outlen, "%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
}

static double get_cpu_ghz(void) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0.0;
    char line[256];
    double mhz = 0.0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu MHz", 7) == 0) {
            sscanf(strchr(line, ':') + 1, "%lf", &mhz);
            break;
        }
    }
    fclose(f);
    return mhz / 1000.0;
}

/* --- Phase 2 sensors (v1.1) --- */

static double get_gpu_percent(void) {
    DIR *dir = opendir("/sys/class/drm");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "card", 4) != 0 ||
                !isdigit((unsigned char)entry->d_name[4])) continue;
            char path[512];
            snprintf(path, sizeof(path), "/sys/class/drm/%s/device/gpu_busy_percent",
                     entry->d_name);
            FILE *f = fopen(path, "r");
            if (!f) continue;
            int value;
            int ok = fscanf(f, "%d", &value) == 1;
            fclose(f);
            if (ok && value >= 0 && value <= 100) {
                closedir(dir);
                return (double)value;
            }
        }
        closedir(dir);
    }
    return g_nvidia_gpu_percent;
}

static void update_nvidia_stats(void) {
    static const char *const binaries[] = {"/usr/bin/nvidia-smi", "/usr/local/bin/nvidia-smi"};
    const char *binary = NULL;
    g_nvidia_gpu_temp_c = -1;
    g_nvidia_gpu_percent = -1;
    g_nvidia_vram_used = 0;
    g_nvidia_vram_total = 0;
    for (size_t i = 0; i < sizeof(binaries) / sizeof(binaries[0]); i++) {
        if (access(binaries[i], X_OK) == 0) {
            binary = binaries[i];
            break;
        }
    }
    if (!binary) return;

    char command[256];
    snprintf(command, sizeof(command),
             "%s --query-gpu=temperature.gpu,utilization.gpu,memory.used,memory.total "
             "--format=csv,noheader,nounits 2>/dev/null", binary);
    FILE *pipe = popen(command, "r");
    if (!pipe) return;
    char line[256];
    if (fgets(line, sizeof(line), pipe)) {
        char *field = strtok(line, ",");
        char *end;
        long temperature = field ? strtol(field, &end, 10) : -1;
        int temperature_ok = field && end != field;
        field = strtok(NULL, ",");
        double utilization = field ? strtod(field, &end) : -1;
        int utilization_ok = field && end != field;
        field = strtok(NULL, ",");
        double used_mb = field ? strtod(field, &end) : -1;
        int used_ok = field && end != field;
        field = strtok(NULL, ",");
        double total_mb = field ? strtod(field, &end) : -1;
        int total_ok = field && end != field;
        if (temperature_ok && temperature >= 0 && temperature <= 200)
            g_nvidia_gpu_temp_c = (int)temperature;
        if (utilization_ok && utilization >= 0 && utilization <= 100)
            g_nvidia_gpu_percent = utilization;
        if (used_ok && total_ok && used_mb >= 0 && total_mb > 0) {
            g_nvidia_vram_used = (unsigned long long)(used_mb * 1024.0 * 1024.0);
            g_nvidia_vram_total = (unsigned long long)(total_mb * 1024.0 * 1024.0);
        }
    }
    pclose(pipe);
}

static double get_swap_percent(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char label[64]; long value;
    long swap_total = 0, swap_free = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s %ld", label, &value) == 2) {
            if (strcmp(label, "SwapTotal:") == 0) swap_total = value;
            else if (strcmp(label, "SwapFree:") == 0) swap_free = value;
        }
    }
    fclose(f);
    if (swap_total <= 0) return 0.0; /* no swap configured -- not an error */
    return 100.0 * (swap_total - swap_free) / swap_total;
}

/* Returns -1 if the path isn't mounted right now (e.g. a removable
   secondary drive unplugged) instead of guessing or crashing. */
static double get_disk_percent(const char *path) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) return -1;
    if (st.f_blocks == 0) return -1;
    return 100.0 * (st.f_blocks - st.f_bfree) / st.f_blocks;
}

static double get_uptime_hours(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0.0;
    double up = 0.0;
    fscanf(f, "%lf", &up);
    fclose(f);
    return up / 3600.0;
}

/* Network throughput is delta-based, so it's computed exactly once per
   frame (regardless of how many elements reference it) into these cached
   globals -- calling the delta logic per-element would corrupt the deltas. */
static double g_net_down_kbps = 0, g_net_up_kbps = 0;
static char g_net_iface[64] = "";

static int interface_is_up(const char *iface) {
    char path[256], state[32];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(state, sizeof(state), f) != NULL;
    fclose(f);
    if (!ok) return 0;
    state[strcspn(state, "\r\n")] = 0;
    return strcmp(state, "up") == 0 || strcmp(state, "unknown") == 0;
}

static int choose_network_interface(char *iface, size_t iface_size) {
    char best[64] = "";
    unsigned int best_metric = ~0U;
    FILE *routes = fopen("/proc/net/route", "r");
    if (routes) {
        char line[256];
        fgets(line, sizeof(line), routes);
        while (fgets(line, sizeof(line), routes)) {
            char candidate[64], destination[16], gateway[16];
            unsigned int flags, metric;
            if (sscanf(line, "%63s %15s %15s %x %*d %*d %u",
                       candidate, destination, gateway, &flags, &metric) == 5 &&
                strcmp(destination, "00000000") == 0 && (flags & 1) &&
                metric < best_metric && interface_is_up(candidate)) {
                snprintf(best, sizeof(best), "%s", candidate);
                best_metric = metric;
            }
        }
        fclose(routes);
    }
    if (!best[0]) {
        DIR *dir = opendir("/sys/class/net");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.' || strcmp(entry->d_name, "lo") == 0 ||
                    !interface_is_up(entry->d_name)) continue;
                size_t name_length = strlen(entry->d_name);
                if (name_length >= sizeof(best)) continue;
                memcpy(best, entry->d_name, name_length + 1);
                break;
            }
            closedir(dir);
        }
    }
    if (!best[0]) return 0;
    snprintf(iface, iface_size, "%s", best);
    return 1;
}

static int read_network_counters(const char *iface,
                                 unsigned long long *rx, unsigned long long *tx) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char *name = line;
        while (isspace((unsigned char)*name)) name++;
        if (strcmp(name, iface) != 0) continue;
        char *value_text = colon + 1;
        found = 1;
        for (int field = 0; field <= 8; field++) {
            while (isspace((unsigned char)*value_text)) value_text++;
            if (!isdigit((unsigned char)*value_text)) {
                found = 0;
                break;
            }
            char *end;
            unsigned long long value = strtoull(value_text, &end, 10);
            if (end == value_text) {
                found = 0;
                break;
            }
            if (field == 0) *rx = value;
            if (field == 8) *tx = value;
            value_text = end;
        }
        break;
    }
    fclose(f);
    return found;
}

static void update_net_speed(void) {
    static unsigned long long prev_rx = 0, prev_tx = 0;
    static time_t prev_time = 0;
    char iface[64] = "";
    unsigned long long rx = 0, tx = 0;
    int have_interface = choose_network_interface(iface, sizeof(iface));
    int have_counters = have_interface && read_network_counters(iface, &rx, &tx);
    time_t now = time(NULL);
    if (!have_counters || strcmp(iface, g_net_iface) != 0 ||
        rx < prev_rx || tx < prev_tx) {
        g_net_down_kbps = 0;
        g_net_up_kbps = 0;
        prev_rx = rx;
        prev_tx = tx;
        prev_time = now;
        snprintf(g_net_iface, sizeof(g_net_iface), "%s",
                 have_counters ? iface : "");
        return;
    }
    double dt = prev_time > 0 ? difftime(now, prev_time) : 0;
    if (dt > 0) {
        g_net_down_kbps = (rx - prev_rx) / 1024.0 / dt;
        g_net_up_kbps = (tx - prev_tx) / 1024.0 / dt;
        prev_rx = rx;
        prev_tx = tx;
        prev_time = now;
    }
}

/* Media info (song/artist/elapsed) via playerctl -- deliberately
   player-agnostic (works with Brave, Spotify, VLC, anything exposing
   MPRIS over D-Bus, confirmed for real against a real Brave YouTube
   Music tab: "brave.instanceNNNN" showed up in `playerctl -l` and
   metadata queries worked). One combined --format call per poll cycle
   (same once-per-frame pattern as update_net_speed() above) instead of
   3 separate playerctl invocations -- cheaper, and avoids the fields
   being read from 3 different, possibly-inconsistent snapshots in time.

   Delimiter is the ASCII Unit Separator (0x1F, "\x1f" in this C string
   literal -- a real byte, not bash escape syntax), not something
   printable like "|" -- verified necessary with a REAL title
   containing a literal "|" character ("Müneccim | YouTube Music"),
   which would have silently corrupted naive pipe-delimited parsing.

   playerctl's {{ position }}/{{ mpris:length }} format fields are in
   MICROSECONDS -- confirmed by direct testing, NOT the same unit as
   the separate `playerctl position` subcommand (which returns
   seconds) -- an easy, real mistake to make by assuming consistency
   instead of testing both. */
static char g_media_title[64] = "";
static char g_media_artist[48] = "";
static char g_media_elapsed[32] = ""; /* wide enough for even an absurdly long podcast (e.g. 9999:59/9999:59) -- -Wformat-truncation caught the original 16-byte buffer as theoretically too small, real warning, not ignored */

static void update_media_info(void) {
    g_media_title[0] = 0;
    g_media_artist[0] = 0;
    g_media_elapsed[0] = 0;
    /* Real bug found and fixed (title/artist source): querying with no
       -p flag lets playerctl pick "the first available player" by its
       own priority order, which on this real KDE desktop picked
       Brave's own raw MPRIS export over KDE's plasma-browser-
       integration -- for the exact same YouTube Music tab, Brave's
       own export reported the generic page title ("YouTube Music", no
       song name) and an empty artist, while plasma-browser-integration
       reported the real song title, real artist, AND real album,
       confirmed side by side with `playerctl -p <name> metadata`.

       Second real bug found and fixed (position/length source): direct
       report -- "the timer never resets when a new song plays. it
       continues where it left off, total time also gets just added
       time." Confirmed by polling both players side by side while a
       YouTube Music playlist played: plasma-browser-integration's own
       `position`/`mpris:length` climbed PAST a single track's real
       duration (a "song" reporting position=9:46 against a
       length=11:05 that kept growing every poll) -- it doesn't cleanly
       reset those two fields between tracks in an autoplay
       playlist/mix. Meanwhile Brave's own raw MPRIS export (tied
       directly to the real underlying <video> element's currentTime/
       duration, not a derived value from the browser-integration
       bridge) stayed correctly bounded to the real per-track duration
       the whole time, confirmed over the same window.

       Fix: one single `-a` (all players) query instead of two, so this
       isn't 2x the subprocess/D-Bus cost. Title/artist still come from
       whichever line is plasma-browser-integration (best metadata,
       same reasoning as above) or the first line if that player isn't
       running. Position/length come from whichever line is NOT
       plasma-browser-integration (the raw player, tied to the actual
       media element) when one exists, else fall back to the same line
       used for title/artist -- correct for the common non-browser case
       (Spotify, VLC) where there's only one real source to begin with,
       already covered by bugcheck_media_sensors.py's real end-to-end
       test. */
    FILE *p = popen("playerctl -a metadata --format "
                     "'{{ playerName }}\x1f{{ title }}\x1f{{ artist }}\x1f{{ position }}\x1f{{ mpris:length }}' 2>/dev/null", "r");
    if (!p) return;

    char title_buf[64] = "", artist_buf[48] = "";
    long pos_us = -1, len_us = -1;
    int have_title = 0, have_time = 0;
    char line[256];
    while (fgets(line, sizeof(line), p)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *player = line;
        char *title = strchr(player, '\x1f');
        if (!title) continue;
        *title = 0; title++;
        char *artist = strchr(title, '\x1f');
        if (!artist) continue;
        *artist = 0; artist++;
        char *pos_str = strchr(artist, '\x1f');
        if (!pos_str) continue;
        *pos_str = 0; pos_str++;
        char *len_str = strchr(pos_str, '\x1f');
        if (!len_str) continue;
        *len_str = 0; len_str++;

        int is_plasma = (strcmp(player, "plasma-browser-integration") == 0);

        if (!have_title || is_plasma) {
            strncpy(title_buf, title, sizeof(title_buf) - 1);
            title_buf[sizeof(title_buf) - 1] = 0;
            strncpy(artist_buf, artist, sizeof(artist_buf) - 1);
            artist_buf[sizeof(artist_buf) - 1] = 0;
            have_title = 1;
        }
        if (!have_time || !is_plasma) {
            pos_us = atol(pos_str);
            len_us = atol(len_str);
            have_time = 1;
            if (!is_plasma) have_time = 2; /* a real non-bridge source found -- stop letting plasma's line override it */
        }
    }
    pclose(p);

    strncpy(g_media_title, title_buf, sizeof(g_media_title) - 1);
    strncpy(g_media_artist, artist_buf, sizeof(g_media_artist) - 1);
    if (have_time && pos_us >= 0 && len_us >= 0) {
        int pos_s = (int)(pos_us / 1000000);
        int len_s = (int)(len_us / 1000000);
        snprintf(g_media_elapsed, sizeof(g_media_elapsed), "%d:%02d/%d:%02d",
                 pos_s / 60, pos_s % 60, len_s / 60, len_s % 60);
    }
}

static void format_kbps(double kbps, char *out, size_t outlen) {
    if (kbps > 1024.0) snprintf(out, outlen, "%.1fM", kbps / 1024.0);
    else snprintf(out, outlen, "%.0fK", kbps);
}

/* --- layout --- */

#define ROW_CPU  3
#define ROW_RAM  33
#define ROW_VRAM 23
#define ROW_TEMP 13
#define BAR_H    4

#define LABEL_X  6
#define PCT_X    35
#define BAR_X1   62
#define BAR_X2   122
#define AMT_X    125

/* Slim, borderless bar: a 1px baseline marks full scale, a filled block
   on top shows the current value. No boxed outline (elegant, not "fat"). */
static void draw_slim_bar(g15canvas *c, int x1, int x2, int y, int h, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g15r_drawLine(c, x1, y + h + 1, x2, y + h + 1, G15_COLOR_BLACK);
    int fill_x2 = x1 + (int)((x2 - x1) * (pct / 100.0));
    if (fill_x2 > x1) {
        g15r_pixelBox(c, x1, y, fill_x2, y + h, G15_COLOR_BLACK, 1, G15_PIXEL_FILL);
    }
}

static void draw_vertical_slim_bar(g15canvas *c, int x, int y, int w, int h, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (w < 2 || h < 2) return;
    g15r_drawLine(c, x, y, x, y + h - 1, G15_COLOR_BLACK);
    g15r_drawLine(c, x + w - 1, y, x + w - 1, y + h - 1, G15_COLOR_BLACK);
    g15r_drawLine(c, x, y + h - 1, x + w - 1, y + h - 1, G15_COLOR_BLACK);
    int inner_h = h - 2;
    int fill_h = (int)(inner_h * (pct / 100.0) + 0.5);
    if (fill_h > 0) {
        g15r_pixelBox(c, x + 1, y + inner_h - fill_h + 1,
                      x + w - 2, y + h - 2, G15_COLOR_BLACK, 1, G15_PIXEL_FILL);
    }
}

/* Labels: Eurostile Bold (custom-converted, matches original G510 look).
   Numbers: the library's own built-in bitmap font -- it was purpose-built
   for this exact tiny resolution, so it stays legible where converted
   TTFs keep garbling at 6-8px. */
static g15font *label_font = NULL;

#define LABEL_Y_OFFSET 3 /* Eurostile's metrics sit higher than the number font's */

/* PROJECT_DIR is passed at compile time by install.sh/rebuild.sh
   (-DPROJECT_DIR='"'$DIR'"') so this file doesn't hardcode a username
   or a specific checkout location. Both scripts always pass it, so
   there's no real case where a fallback value is needed -- and a
   fallback would have to be either someone's actual real path (which
   defeats the whole point) or a made-up one that fails in a confusing
   way at runtime. Failing loudly at COMPILE time instead: hand-compile
   this file directly and you get a clear error telling you what to do,
   not a silently-wrong path or a random "file not found" later. */
#ifndef PROJECT_DIR
#error "PROJECT_DIR not defined -- compile via install.sh or scripts/rebuild.sh, or pass -DPROJECT_DIR='\"/your/checkout/path\"' yourself"
#endif
/* Checks ~/.local/share/g510-lcd/fonts/ first (always user-writable,
   works identically whether this binary is a dev checkout or a real
   package under root-owned /usr/lib/g510-lcd) before falling back to
   the PROJECT_DIR-relative path (keeps working for anyone who already
   has their converted font sitting in a dev checkout, no migration
   needed -- the fallback is permanent, not a one-time transition).
   Can't ship the font itself either way (commercial license), so this
   doesn't remove the manual conversion step -- it just means that
   step no longer needs sudo for a packaged install. */
#define FONT_PATH_FALLBACK PROJECT_DIR "/fonts/lcd-label-8.fnt"
/* font_path() itself is defined further down, right after data_dir()
   -- it calls data_dir(), which needs to exist first. */

/* Your own custom-screens config and imported images live under
   ~/.local/share/g510-lcd, independent of where the program itself is
   installed from (a dev checkout via install.sh, or a real package
   under a fixed /usr/lib/g510-lcd) -- so a package upgrade (root-owned,
   read-only /usr/lib) never touches what you've actually configured.
   Mirrors button_log_path() in g510_lcd_buttons.c. On first run after
   upgrading from a version that stored these directly under
   PROJECT_DIR, migrate the old files over once rather than silently
   showing "not configured" on a screen that was actually already set
   up -- migrate_file_if_needed()/migrate_dir_if_needed() are no-ops
   whenever there's nothing old to migrate (a real package install,
   or a dev checkout that's already been migrated once). */
/* Plain mkdir() only creates one level -- ~/.local/share doesn't
   necessarily exist yet on every system (confirmed the hard way: an
   isolated test with a fresh, empty $HOME silently failed to create
   ~/.local/share/g510-lcd because mkdir() can't create the missing
   ~/.local and ~/.local/share parents in one call). Walks the path
   one "/"-separated component at a time, creating each as needed. */
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

static void migrate_file_if_needed(const char *old_path, const char *new_path) {
    FILE *already = fopen(new_path, "r");
    if (already) { fclose(already); return; }
    FILE *src = fopen(old_path, "r");
    if (!src) return;
    FILE *dst = fopen(new_path, "w");
    if (!dst) { fclose(src); return; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
    fclose(src);
    fclose(dst);
}

static void migrate_dir_if_needed(const char *old_dir, const char *new_dir) {
    DIR *d = opendir(old_dir);
    if (!d) return;
    mkdir_p(new_dir);
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char old_path[512], new_path[512];
        snprintf(old_path, sizeof(old_path), "%s/%s", old_dir, entry->d_name);
        snprintf(new_path, sizeof(new_path), "%s/%s", new_dir, entry->d_name);
        migrate_file_if_needed(old_path, new_path);
    }
    closedir(d);
}

static const char *data_dir(void) {
    static char dir[200];
    static int ready = 0;
    if (!ready) {
        const char *home = getenv("HOME");
        snprintf(dir, sizeof(dir), "%s/.local/share/g510-lcd", home ? home : "/tmp");
        mkdir_p(dir);

        char old_screens[256], new_screens[256];
        snprintf(old_screens, sizeof(old_screens), "%s/custom_screens.txt", PROJECT_DIR);
        snprintf(new_screens, sizeof(new_screens), "%s/custom_screens.txt", dir);
        migrate_file_if_needed(old_screens, new_screens);

        char old_images[256], new_images[256];
        snprintf(old_images, sizeof(old_images), "%s/custom_screen_images", PROJECT_DIR);
        snprintf(new_images, sizeof(new_images), "%s/custom_screen_images", dir);
        migrate_dir_if_needed(old_images, new_images);

        ready = 1;
    }
    return dir;
}

static const char *font_path(void) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/fonts/lcd-label-8.fnt", data_dir());
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return path; }
    return FONT_PATH_FALLBACK;
}

/* Second-tier fallback: a genuinely clean, SIL-OFL-licensed font
   (fonts/source-ttf/FALLBACK.ttf, converted to lcd-label-8-fallback.fnt
   by install.sh/scripts/rebuild.sh) used only if the primary font at
   font_path() is missing, unparseable, or fails label_font_is_sane()
   (see font_sanity.h). Same data_dir()-then-PROJECT_DIR resolution
   order as font_path() itself. */
#define FALLBACK_FONT_PATH_FALLBACK PROJECT_DIR "/fonts/lcd-label-8-fallback.fnt"

static const char *fallback_font_path(void) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/fonts/lcd-label-8-fallback.fnt", data_dir());
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return path; }
    return FALLBACK_FONT_PATH_FALLBACK;
}

static const char *custom_screens_path(void) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/custom_screens.txt", data_dir());
    return path;
}

/* An optional removable secondary drive, auto-mounted by udisks2 at
   the standard /run/media/$USER/<label> convention -- <label> is
   whatever the drive's own real filesystem volume label is, which
   this code has no control over (it's a fact about the physical
   disk, not a choice made here). Built at runtime from $USER rather
   than baked in at compile time (same reasoning as button_log_path()
   in g510_lcd_buttons.c), so this file doesn't hardcode a username.
   On a machine without this exact drive, get_disk_percent() already
   handles the path not existing by showing "N/A" rather than
   crashing. EDIT THE LABEL BELOW to match your own drive's real
   volume label if you want the DISK_SECONDARY_PCT sensor to point at
   it, or just don't use that sensor. */
#define SECONDARY_DISK_LABEL "frigider"

static const char *disk_secondary_path(void) {
    static char path[256];
    const char *user = getenv("USER");
    snprintf(path, sizeof(path), "/run/media/%s/" SECONDARY_DISK_LABEL, user ? user : "nobody");
    return path;
}

/* The built-in statistics screen retains its custom G15 label font.
   Editable dashboard labels use the selected built-in text size instead,
   matching each element's value and keeping its layout measurable. */

static void draw_label(g15canvas *c, int x, int y, const char *label) {
    if (label_font) {
        g15r_G15FontRenderString(c, label_font, (char*)label, 0, x, y + LABEL_Y_OFFSET, G15_COLOR_BLACK, 0);
    } else {
        g15r_renderString(c, (unsigned char*)label, 0, G15_TEXT_SMALL, x, y);
    }
}

/* Real bugs found by direct user testing: (1) long value text (a real
   song title) ran straight off the visible screen edge with no
   fallback -- "theres no fallback plan for when the text is too long.
   it just exits screen"; (2) the GUI's drag hit-box for that same text
   was a fixed +30px guess (see the old bounds-sidecar code below),
   nowhere near covering a real long title -- "the blue bars... dont
   contain the whole variable element(name)".

   Root cause of BOTH: g15r_testG15FontWidth() (the library's only
   width-query function) only works on a LOADED CUSTOM g15font like
   label_font -- confirmed by re-checking /usr/include/libg15render.h,
   not assumed -- it has no equivalent for the BUILT-IN G15_TEXT_SMALL/
   MED/LARGE/HUGE fonts that sensor values and freeform text actually
   render with. This measures those by rendering to a throwaway
   scratch canvas and scanning for the rightmost lit pixel -- the same
   render-and-scan technique already used elsewhere in this project
   (the clock face's roman-numeral positions), not a new invented
   hack. */
static int measure_builtin_text_width(const char *s, int font_size) {
    if (!s[0]) return 0;
    g15canvas scratch;
    g15r_initCanvas(&scratch);
    g15r_clearScreen(&scratch, G15_COLOR_WHITE);
    g15r_renderString(&scratch, (unsigned char*)s, 0, font_size, 0, 0);
    int max_x = -1;
    for (int y = 0; y < G15_LCD_HEIGHT; y++)
        for (int x = 0; x < G15_LCD_WIDTH; x++)
            if (g15r_getPixel(&scratch, x, y) && x > max_x) max_x = x;
    return max_x + 1; /* -1 (nothing lit) + 1 = 0, correct for an all-blank string */
}

static void draw_builtin_label(g15canvas *canvas, const char *text, int x, int y,
                               int font_size) {
    char copy[64];
    snprintf(copy, sizeof(copy), "%s", text);
    g15r_G15FPrint(canvas, copy, x, y, font_size,
                   G15_JUSTIFY_LEFT, G15_COLOR_BLACK, 0);
}

static int measure_builtin_label(const char *text, int font_size, int measure_height) {
    if (!text[0]) return 0;
    g15canvas scratch;
    g15r_initCanvas(&scratch);
    g15r_clearScreen(&scratch, G15_COLOR_WHITE);
    draw_builtin_label(&scratch, text, 0, 0, font_size);
    int minimum = measure_height ? G15_LCD_HEIGHT : G15_LCD_WIDTH;
    int maximum = -1;
    for (int y = 0; y < G15_LCD_HEIGHT; y++) {
        for (int x = 0; x < G15_LCD_WIDTH; x++) {
            if (g15r_getPixel(&scratch, x, y) != G15_COLOR_BLACK) continue;
            int coordinate = measure_height ? y : x;
            if (coordinate < minimum) minimum = coordinate;
            if (coordinate > maximum) maximum = coordinate;
        }
    }
    return maximum >= minimum ? maximum - minimum + 1 : 0;
}

static int measure_builtin_label_width(const char *text, int font_size) {
    return measure_builtin_label(text, font_size, 0);
}

static int measure_builtin_label_height(const char *text, int font_size) {
    return measure_builtin_label(text, font_size, 1);
}

static void truncate_builtin_label(char *text, int font_size, int max_width) {
    if (max_width < 0) max_width = 0;
    if (measure_builtin_label_width(text, font_size) <= max_width) return;
    int len = (int)strlen(text);
    while (len > 0) {
        len--;
        text[len] = '.';
        text[len + 1] = 0;
        if (measure_builtin_label_width(text, font_size) <= max_width) return;
    }
    text[0] = 0;
}

/* Truncates `s` in place (a single trailing '.' marks a real cut, not
   a rendering glitch) until it measures at or under `max_w` pixels at
   `font_size`. No-op if it already fits. Cheap: measuring shrinks by
   one character each try, and these strings are at most a few dozen
   characters (SENSOR value buffers are 32 bytes, freeform TEXT content
   is 48). */
/* Truncation math is exact (verified by direct stress-testing across
   every font size and a wide range of avail/unicode inputs -- never
   measured a single pixel past max_w), but exact-to-the-edge still
   reads as "touching/going off the screen" on a real physical LCD.
   A few px of breathing room makes a truncated value look deliberately
   cut off, not broken. */
#define TEXT_EDGE_MARGIN 3

static void truncate_builtin_text(char *s, int font_size, int max_w) {
    if (max_w < 0) max_w = 0;
    if (measure_builtin_text_width(s, font_size) <= max_w) return;
    int len = (int)strlen(s);
    while (len > 0) {
        len--;
        s[len] = '.';
        s[len + 1] = 0;
        if (measure_builtin_text_width(s, font_size) <= max_w) return;
    }
    s[0] = 0; /* not even one character + '.' fits -- genuinely no room, show nothing rather than garbage */
}

static void draw_row(g15canvas *c, int y, const char *label, int pct,
                      const char *pct_str, const char *amount, int pct_y_nudge) {
    draw_label(c, LABEL_X, y, label);
    g15r_renderString(c, (unsigned char*)pct_str, 0, G15_TEXT_SMALL, PCT_X, y + pct_y_nudge);
    draw_slim_bar(c, BAR_X1, BAR_X2, y, BAR_H, pct);
    if (amount) {
        g15r_renderString(c, (unsigned char*)amount, 0, G15_TEXT_SMALL, AMT_X, y);
    }
}

static const char *screen_state_path(void) {
    static char path[256];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    snprintf(path, sizeof(path), "%s/g510lcd_screen", runtime ? runtime : "/tmp");
    return path;
}

static int read_screen(void) {
    FILE *f = fopen(screen_state_path(), "r");
    if (!f) return 0;
    int s = 0;
    fscanf(f, "%d", &s);
    fclose(f);
    return s;
}

/* Screen 0: the original built-in stats screen. Pulled into its own
   function (was inline in main()) so --preview can render it through the
   exact same code path as the live loop. */
static int max_temp_seen = 0; /* highest temp observed since this program started */

static void draw_stats_screen(g15canvas *canvas) {
    double cpu_pct = get_cpu_percent();
    char cpu_str[16], cpu_ghz_str[16];
    snprintf(cpu_str, sizeof(cpu_str), "%3d%%", (int)(cpu_pct + 0.5));
    snprintf(cpu_ghz_str, sizeof(cpu_ghz_str), "%.1fGHz", get_cpu_ghz());
    draw_row(canvas, ROW_CPU, "CPU", (int)cpu_pct, cpu_str, cpu_ghz_str, 0);

    long ram_used_kb, ram_total_kb;
    get_ram_kb(&ram_used_kb, &ram_total_kb);
    int ram_pct = ram_total_kb > 0 ? (int)(100.0 * ram_used_kb / ram_total_kb) : 0;
    char ram_pct_str[16], ram_amt_str[16];
    snprintf(ram_pct_str, sizeof(ram_pct_str), "%3d%%", ram_pct);
    snprintf(ram_amt_str, sizeof(ram_amt_str), "%.1fG", ram_used_kb / (1024.0 * 1024.0));
    draw_row(canvas, ROW_RAM, "RAM", ram_pct, ram_pct_str, ram_amt_str, 0);

    unsigned long long vram_used, vram_total;
    get_vram_bytes(&vram_used, &vram_total);
    int vram_pct = vram_total > 0 ? (int)(100.0 * vram_used / vram_total) : 0;
    char vram_pct_str[16], vram_amt_str[16];
    if (vram_total > 0) {
        snprintf(vram_pct_str, sizeof(vram_pct_str), "%3d%%", vram_pct);
        format_gb(vram_used, vram_amt_str, sizeof(vram_amt_str));
    } else {
        snprintf(vram_pct_str, sizeof(vram_pct_str), "N/A");
        snprintf(vram_amt_str, sizeof(vram_amt_str), "N/A");
    }
    draw_row(canvas, ROW_VRAM, "VRAM", vram_pct, vram_pct_str, vram_amt_str, 0);

    int temp_c = get_cpu_temp_c();
    char temp_str[16], temp_max_str[16];
    int temp_pct = 0;
    if (temp_c >= 0) {
        if (temp_c > max_temp_seen) max_temp_seen = temp_c;
        snprintf(temp_str, sizeof(temp_str), "%d" "\xB0" "C", temp_c);
        snprintf(temp_max_str, sizeof(temp_max_str), "MAX %d" "\xB0" "C", max_temp_seen);
        temp_pct = temp_c * 100 / 90;
    } else {
        snprintf(temp_str, sizeof(temp_str), "N/A");
        snprintf(temp_max_str, sizeof(temp_max_str), "MAX N/A");
    }
    draw_row(canvas, ROW_TEMP, "TEMP", temp_pct, temp_str, temp_max_str, 1);
}

/* Screen 1: a simple large clock. New screens go here -- L1 cycles
   through however many screens NUM_SCREENS (in g510_lcd_buttons.c)
   currently accounts for. */
/* Square-ish frame with rounded corners (g15r_drawRoundBox, not a
   plain pixelBox) -- direct refinement request: "the clock is too
   rough, i want rounded corners a bit". Position/size still grounded
   in the real measured empty space (rendered --preview, scanned pixel
   data: text ends at x=92, so x1=107 leaves a real 15px gap, not
   guessed) but grown from the first pass's 36x36 to 40x40 -- fitting
   3-character roman numerals (XII/III measured at 11px wide) AND
   clearly-separated hands needed more room than the original size
   had, confirmed by the numeral-position math below actually working
   out with real margins, not by eyeballing it. */
#define CLOCK_FACE_X1 107
#define CLOCK_FACE_Y1 1
#define CLOCK_FACE_X2 147
#define CLOCK_FACE_Y2 41
#define CLOCK_FACE_CX ((CLOCK_FACE_X1 + CLOCK_FACE_X2) / 2)
#define CLOCK_FACE_CY ((CLOCK_FACE_Y1 + CLOCK_FACE_Y2) / 2)

/* Hand-drawn I/V/X strokes -- direct refinement request: "could we do
   the roman numerals JUST A BIT SMALLER?". G15_TEXT_SMALL (used for
   the first pass) is already the smallest built-in bitmap font this
   library ships (confirmed: only SMALL/MED/LARGE/HUGE exist, checked
   the header) -- there's no smaller size to ask it for. Roman
   numerals only ever need three shapes (I/V/X), each a trivial
   straight-line composition, so drawing them directly with
   g15r_drawLine at a chosen size is both smaller AND crisper on a
   1-bit display than shrinking a bitmap or antialiased TTF glyph
   would be (no half-lit pixels to go muddy at tiny sizes). */
#define ROMAN_GLYPH_H 4

static int roman_glyph_width(char ch) {
    return (ch == 'I') ? 1 : 3;
}

static void draw_roman_glyph(g15canvas *c, char ch, int x, int y) {
    switch (ch) {
        case 'I':
            g15r_drawLine(c, x, y, x, y + ROMAN_GLYPH_H - 1, G15_COLOR_BLACK);
            break;
        case 'V':
            g15r_drawLine(c, x, y, x + 1, y + ROMAN_GLYPH_H - 1, G15_COLOR_BLACK);
            g15r_drawLine(c, x + 2, y, x + 1, y + ROMAN_GLYPH_H - 1, G15_COLOR_BLACK);
            break;
        case 'X':
            g15r_drawLine(c, x, y, x + 2, y + ROMAN_GLYPH_H - 1, G15_COLOR_BLACK);
            g15r_drawLine(c, x + 2, y, x, y + ROMAN_GLYPH_H - 1, G15_COLOR_BLACK);
            break;
    }
}

/* cx/cy = the numeral's own center point (same 12px-radius circle
   used before) -- computes the real composed width from the actual
   glyphs being drawn (1px gap between characters) so it's centered
   exactly, not approximated. */
static void draw_roman_numeral(g15canvas *c, const char *s, int cx, int cy) {
    int len = (int)strlen(s);
    int w = 0;
    for (int i = 0; i < len; i++) {
        w += roman_glyph_width(s[i]);
        if (i < len - 1) w += 1;
    }
    int x = cx - w / 2;
    int y = cy - ROMAN_GLYPH_H / 2;
    for (int i = 0; i < len; i++) {
        draw_roman_glyph(c, s[i], x, y);
        x += roman_glyph_width(s[i]) + 1;
    }
}

static void draw_analog_clock(g15canvas *c, struct tm *t) {
    g15r_drawRoundBox(c, CLOCK_FACE_X1, CLOCK_FACE_Y1, CLOCK_FACE_X2, CLOCK_FACE_Y2, 0, G15_COLOR_BLACK);

    /* Roman numerals at 12/3/6/9 -- direct request: "some roman
       numerals at 12 3 6 9 oclock", later refined ("JUST A BIT
       SMALLER") to these hand-drawn I/V/X strokes -- see
       draw_roman_numeral above. Each numeral is centered on its own
       point on the same 12px-radius circle used since the first pass. */
    draw_roman_numeral(c, "XII", CLOCK_FACE_CX, CLOCK_FACE_CY - 12);
    draw_roman_numeral(c, "III", CLOCK_FACE_CX + 12, CLOCK_FACE_CY);
    draw_roman_numeral(c, "VI",  CLOCK_FACE_CX, CLOCK_FACE_CY + 12);
    draw_roman_numeral(c, "IX",  CLOCK_FACE_CX - 12, CLOCK_FACE_CY);

    /* Small tick marks at the other 8 hours -- direct request: "with
       small lines in between". Same angle convention as the hands
       (0 = 12 o'clock, clockwise), radius 15-18 -- inside the rounded
       frame (half-side 20) but clear of the numeral zone (numeral
       centers sit at radius 12, half-height ~2.5, so nothing there
       extends past radius ~15). Skips hours 12/3/6/9 -- already
       labeled with numerals, a tick there would just clutter them. */
    for (int hour = 1; hour <= 12; hour++) {
        if (hour == 12 || hour == 3 || hour == 6 || hour == 9) continue;
        double angle = (hour / 12.0) * 2 * M_PI;
        int x1 = CLOCK_FACE_CX + (int)round(sin(angle) * 15);
        int y1 = CLOCK_FACE_CY - (int)round(cos(angle) * 15);
        int x2 = CLOCK_FACE_CX + (int)round(sin(angle) * 18);
        int y2 = CLOCK_FACE_CY - (int)round(cos(angle) * 18);
        g15r_drawLine(c, x1, y1, x2, y2, G15_COLOR_BLACK);
    }

    /* Hands -- lengths kept clearly under the numeral radius (12) so
       neither hand ever visually overlaps a numeral, including at
       :15/:45 (minute hand pointing exactly at III/IX) or 3:00/9:00
       (hour hand pointing exactly at III/IX). No second hand --
       deliberately simple on a square this small. */
    double minute_angle = (t->tm_min / 60.0) * 2 * M_PI;
    double hour_angle = ((t->tm_hour % 12) + t->tm_min / 60.0) / 12.0 * 2 * M_PI;

    int minute_len = 9, hour_len = 6;
    int mx = CLOCK_FACE_CX + (int)round(sin(minute_angle) * minute_len);
    int my = CLOCK_FACE_CY - (int)round(cos(minute_angle) * minute_len);
    int hx = CLOCK_FACE_CX + (int)round(sin(hour_angle) * hour_len);
    int hy = CLOCK_FACE_CY - (int)round(cos(hour_angle) * hour_len);

    g15r_drawLine(c, CLOCK_FACE_CX, CLOCK_FACE_CY, mx, my, G15_COLOR_BLACK);
    g15r_drawLine(c, CLOCK_FACE_CX, CLOCK_FACE_CY, hx, hy, G15_COLOR_BLACK);
    g15r_drawCircle(c, CLOCK_FACE_CX, CLOCK_FACE_CY, 1, 1, G15_COLOR_BLACK);
}

static void draw_clock_screen(g15canvas *c) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[16], date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);
    strftime(date_str, sizeof(date_str), "%A, %d %B", t);

    /* Direct request: "move the digital clock a bit down and the date
       a bit up so theyre not so far away from eachother". Measured the
       real gap first (rendered --preview, scanned lit pixel rows in
       the left/digital-clock column): time occupied rows 8-14, date
       rows 30-35, a 15px empty gap between them (rows 15-29). Moved
       each 4px toward the other -- time to y=12 (rows 12-18), date to
       y=26 (rows 26-31) -- shrinking the gap to 7px while staying well
       clear of the screen edges (top/bottom) and each other. */
    g15r_G15FPrint(c, time_str, 20, 12, G15_TEXT_LARGE, G15_JUSTIFY_LEFT, G15_COLOR_BLACK, 0);
    g15r_renderString(c, (unsigned char*)date_str, 0, G15_TEXT_SMALL, 10, 26);
    draw_analog_clock(c, t);
}

/* --- Custom Screens (v1.1): user-built dashboards for L2-L5 --- */

typedef struct {
    const char *key;    /* used in custom_screens.txt and the GUI dropdown */
    const char *label;  /* short on-screen label (fits the tiny font) */
    int is_percent;      /* naturally 0-100 -- bar-capable, no scale guessing */
    int is_temp;          /* temperature in C -- bar-capable via the existing 0-90C convention */
    int auto_center;    /* horizontally centered on the display instead of
                            anchored at the element's configured x -- direct
                            request: "id like the song name and artist always
                            auto centre, text lenghts differ from song to
                            song" (a fixed x can't center text whose width
                            changes every track). Left unset (0, C's default
                            for a trailing struct initializer field) on every
                            other row below on purpose. */
} sensor_def_t;

static const sensor_def_t SENSORS[] = {
    {"CPU_PCT", "CPU", 1, 0, 0},
    {"CPU_GHZ", "GHZ", 0, 0, 0},
    {"CPU_TEMP", "CPU", 0, 1, 0},
    {"RAM_PCT", "RAM", 1, 0, 0},
    {"RAM_AMOUNT", "AMT", 0, 0, 0},
    {"VRAM_PCT", "VRAM", 1, 0, 0},
    {"VRAM_AMOUNT", "AMT", 0, 0, 0},
    {"MAXTEMP", "MAXT", 0, 1, 0},
    {"GPU_PCT", "GPU", 1, 0, 0},
    {"GPU_EDGE_TEMP", "GPU", 0, 1, 0},
    {"GPU_HOTSPOT_TEMP", "HOT", 0, 1, 0},
    {"GPU_VRAM_TEMP", "VMEM", 0, 1, 0},
    {"SWAP_PCT", "SWAP", 1, 0, 0},
    {"DISK_ROOT_PCT", "DISK", 1, 0, 0},
    {"DISK_SECONDARY_PCT", "DSK2", 1, 0, 0},
    {"UPTIME", "UPTM", 0, 0, 0},
    {"NET_DOWN", "DN", 0, 0, 0},
    {"NET_UP", "UP", 0, 0, 0},
    /* Direct request: "when selected, i dont want them to say time:
       xx:xx or date : xxxx / drop the lables" -- an empty label is
       safe here without any new code path: label_text_width("") is 0
       (g15r_testG15FontWidth loops zero characters), draw_label("")
       renders nothing, and value_x = el->x + 0 + 4 just leaves a
       small natural margin instead of "TIME "/"DATE " prefixing the
       actual value. */
    {"TIME", "", 0, 0, 0},
    {"DATE", "", 0, 0, 0},
    /* Empty labels, same reasoning as TIME/DATE above -- the content
       itself (a song title, an artist name, "1:23/3:45") is already
       self-descriptive, and every pixel matters on this screen. */
    {"MEDIA_TITLE",       "",    0, 0, 1},
    {"MEDIA_ARTIST",      "",    0, 0, 1},
    {"MEDIA_ELAPSED", "", 0, 0, 0},
    {"MB_TEMP1", "MB1", 0, 1, 0},
    {"MB_TEMP2", "MB2", 0, 1, 0},
    {"MB_TEMP3", "MB3", 0, 1, 0},
    {"MB_TEMP4", "MB4", 0, 1, 0},
    {"MB_TEMP5", "MB5", 0, 1, 0},
    {"MB_TEMP6", "MB6", 0, 1, 0},
};

#define CPU_CORE_SENSOR_COUNT 8
static sensor_def_t cpu_core_sensors[CPU_CORE_SENSOR_COUNT];
static char cpu_core_keys[CPU_CORE_SENSOR_COUNT][16];
static char cpu_core_labels[CPU_CORE_SENSOR_COUNT][8];
static int cpu_core_sensors_ready = 0;
static const char *cpu_core_label_names[CPU_CORE_SENSOR_COUNT] = {
    "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8"
};

static void init_cpu_core_sensors(void) {
    if (cpu_core_sensors_ready) return;
    for (int i = 0; i < CPU_CORE_SENSOR_COUNT; i++) {
        snprintf(cpu_core_keys[i], sizeof(cpu_core_keys[i]), "CPU_CORE_%d", i);
        snprintf(cpu_core_labels[i], sizeof(cpu_core_labels[i]), "%s", cpu_core_label_names[i]);
        cpu_core_sensors[i] = (sensor_def_t){cpu_core_keys[i], cpu_core_labels[i], 1, 0, 0};
    }
    cpu_core_sensors_ready = 1;
}

static const sensor_def_t *find_sensor(const char *key) {
    for (size_t i = 0; i < sizeof(SENSORS) / sizeof(SENSORS[0]); i++)
        if (strcmp(SENSORS[i].key, key) == 0) return &SENSORS[i];
    init_cpu_core_sensors();
    for (int i = 0; i < CPU_CORE_SENSOR_COUNT; i++)
        if (strcmp(cpu_core_sensors[i].key, key) == 0) return &cpu_core_sensors[i];
    return NULL;
}

static double get_cpu_core_percent(int core) {
    static unsigned long long previous_total[CPU_CORE_SENSOR_COUNT];
    static unsigned long long previous_idle[CPU_CORE_SENSOR_COUNT];
    static int initialized[CPU_CORE_SENSOR_COUNT];
    if (core < 0 || core >= CPU_CORE_SENSOR_COUNT) return 0.0;

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0.0;
    char line[256];
    char expected[16];
    snprintf(expected, sizeof(expected), "cpu%d", core);
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[16];
        if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                   name, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 9 &&
            strcmp(name, expected) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    if (!found) return 0.0;

    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long idle_total = idle + iowait;
    if (!initialized[core]) {
        previous_total[core] = total;
        previous_idle[core] = idle_total;
        initialized[core] = 1;
        return 0.0;
    }
    if (total < previous_total[core] || idle_total < previous_idle[core]) {
        previous_total[core] = total;
        previous_idle[core] = idle_total;
        return 0.0;
    }
    unsigned long long delta_total = total - previous_total[core];
    unsigned long long delta_idle = idle_total - previous_idle[core];
    previous_total[core] = total;
    previous_idle[core] = idle_total;
    return delta_total > 0 && delta_idle <= delta_total
        ? 100.0 * (double)(delta_total - delta_idle) / (double)delta_total
        : 0.0;
}

/* Fills pct_for_bar (0-100, or -1 if this sensor has no honest bar scale /
   is currently unavailable) and a short display string for the value. */
static void get_sensor_value(const char *key, const char *format,
                             double *pct_for_bar, char *disp, size_t displen) {
    *pct_for_bar = -1;
    disp[0] = 0;

    if (strcmp(key, "CPU_PCT") == 0) {
        double v = get_cpu_percent();
        *pct_for_bar = v;
        snprintf(disp, displen, "%d%%", (int)(v + 0.5));
    } else if (strncmp(key, "CPU_CORE_", 9) == 0) {
        char *end = NULL;
        long core = strtol(key + 9, &end, 10);
        if (end && *end == '\0' && core >= 0 && core < CPU_CORE_SENSOR_COUNT) {
            double v = get_cpu_core_percent((int)core);
            *pct_for_bar = v;
            snprintf(disp, displen, "%d%%", (int)(v + 0.5));
        }
    } else if (strcmp(key, "CPU_GHZ") == 0) {
        snprintf(disp, displen, "%.1fGHz", get_cpu_ghz());
    } else if (strcmp(key, "CPU_TEMP") == 0) {
        int t = get_cpu_temp_c();
        if (t >= 0) {
            *pct_for_bar = t * 100.0 / 90.0;
            snprintf(disp, displen, "%d\xB0" "C", t);
        } else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "RAM_PCT") == 0) {
        long u, t; get_ram_kb(&u, &t);
        double v = t > 0 ? 100.0 * u / t : 0;
        *pct_for_bar = v;
        snprintf(disp, displen, "%d%%", (int)(v + 0.5));
    } else if (strcmp(key, "RAM_AMOUNT") == 0) {
        long u, t; get_ram_kb(&u, &t);
        snprintf(disp, displen, "%.1fG", u / (1024.0 * 1024.0));
    } else if (strcmp(key, "VRAM_PCT") == 0) {
        unsigned long long u, t; get_vram_bytes(&u, &t);
        if (t > 0) {
            double v = 100.0 * u / t;
            *pct_for_bar = v;
            snprintf(disp, displen, "%d%%", (int)(v + 0.5));
        } else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "VRAM_AMOUNT") == 0) {
        unsigned long long u, t; get_vram_bytes(&u, &t);
        if (t > 0) format_gb(u, disp, displen);
        else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "MAXTEMP") == 0) {
        if (max_temp_seen > 0) snprintf(disp, displen, "%d\xB0" "C", max_temp_seen);
        else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "GPU_PCT") == 0) {
        double v = get_gpu_percent();
        if (v < 0) snprintf(disp, displen, "N/A");
        else { *pct_for_bar = v; snprintf(disp, displen, "%d%%", (int)v); }
    } else if (strcmp(key, "GPU_EDGE_TEMP") == 0) {
        int t = get_gpu_temp_c(0);
        if (t >= 0) {
            *pct_for_bar = t * 100.0 / 90.0;
            snprintf(disp, displen, "%d\xB0" "C", t);
        } else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "GPU_HOTSPOT_TEMP") == 0) {
        int t = get_gpu_temp_c(1);
        if (t >= 0) {
            *pct_for_bar = t * 100.0 / 90.0;
            snprintf(disp, displen, "%d\xB0" "C", t);
        } else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "GPU_VRAM_TEMP") == 0) {
        int t = get_gpu_temp_c(2);
        if (t >= 0) {
            *pct_for_bar = t * 100.0 / 90.0;
            snprintf(disp, displen, "%d\xB0" "C", t);
        } else snprintf(disp, displen, "N/A");
    } else if (strcmp(key, "SWAP_PCT") == 0) {
        double v = get_swap_percent();
        *pct_for_bar = v;
        snprintf(disp, displen, "%d%%", (int)(v + 0.5));
    } else if (strcmp(key, "DISK_ROOT_PCT") == 0) {
        double v = get_disk_percent("/");
        if (v < 0) snprintf(disp, displen, "N/A");
        else { *pct_for_bar = v; snprintf(disp, displen, "%d%%", (int)(v + 0.5)); }
    } else if (strcmp(key, "DISK_SECONDARY_PCT") == 0) {
        double v = get_disk_percent(disk_secondary_path());
        if (v < 0) snprintf(disp, displen, "N/A");
        else { *pct_for_bar = v; snprintf(disp, displen, "%d%%", (int)(v + 0.5)); }
    } else if (strcmp(key, "UPTIME") == 0) {
        snprintf(disp, displen, "%.0fh", get_uptime_hours());
    } else if (strcmp(key, "NET_DOWN") == 0) {
        format_kbps(g_net_down_kbps, disp, displen);
    } else if (strcmp(key, "NET_UP") == 0) {
        format_kbps(g_net_up_kbps, disp, displen);
    } else if (strcmp(key, "TIME") == 0) {
        time_t now = time(NULL);
        const char *pattern = "%H:%M:%S";
        if (strcmp(format, "hm") == 0) pattern = "%H:%M";
        else if (strcmp(format, "ampm") == 0) pattern = "%I:%M %p";
        strftime(disp, displen, pattern, localtime(&now));
    } else if (strcmp(key, "DATE") == 0) {
        time_t now = time(NULL);
        const char *pattern = "%d %b";
        if (strcmp(format, "eu") == 0) pattern = "%d.%m.%Y";
        else if (strcmp(format, "eu_short") == 0) pattern = "%d.%m.";
        else if (strcmp(format, "iso") == 0) pattern = "%Y-%m-%d";
        strftime(disp, displen, pattern, localtime(&now));
    } else if (strcmp(key, "MEDIA_TITLE") == 0) {
        snprintf(disp, displen, "%s", g_media_title);
    } else if (strcmp(key, "MEDIA_ARTIST") == 0) {
        snprintf(disp, displen, "%s", g_media_artist);
    } else if (strcmp(key, "MEDIA_ELAPSED") == 0) {
        snprintf(disp, displen, "%s", g_media_elapsed);
    } else if (strncmp(key, "MB_TEMP", 7) == 0) {
        int n = atoi(key + 7);
        int t = n > 0 ? get_motherboard_temp_c(n - 1) : -1;
        if (t >= 0) {
            *pct_for_bar = t * 100.0 / 90.0;
            snprintf(disp, displen, "%d\xB0" "C", t);
        } else snprintf(disp, displen, "N/A");
    }
}

#define MAX_ELEMENTS 16
#define MAX_IMAGES 2 /* the screen is 160x43 -- rarely useful to fit more than this */
#define MAX_CUSTOM_SCREENS 4 /* L2, L3, L4, L5 */

typedef struct {
    char sensor[24];
    char label[32];
    char format[16];
    char style[8]; /* "number" or "bar" */
    char orientation[12]; /* "horizontal" or "vertical" for bars */
    int x, y;
    int width; /* bar length in px, only meaningful for style="bar" -- ignored for "number" */
    int height; /* vertical bar length in px */
    int font_size; /* G15_TEXT_SMALL(0)/MED(1)/LARGE(2)/HUGE(3) for label and value */
} element_t;

/* A converted-to-1bpp image placed on a custom screen -- see
   src/png-to-lcd.py (the conversion step) and draw_image_element()
   below (the render step, reusing g15r_drawXBM(), both already
   verified working independently and together before this was wired
   in). No resize in this first version -- width/height are fixed at
   import time by png-to-lcd.py's max_width parameter and the source
   image's own aspect ratio; only position (x, y) is editable, via the
   same drag mechanic as sensor elements. */
typedef struct {
    char path[128]; /* relative to data_dir(), e.g. "custom_screen_images/logo.bin" */
    int x, y, width, height;
} image_t;

/* Freeform user text -- not tied to any sensor, direct request: "L3 L4
   L5 have hard coded text i cant edit move do anything with. add an
   option for me to add text fields too" (referring to the "not set up
   yet" placeholder, correctly identified as non-editable by design --
   this is the real, editable alternative). Spaces are stored as
   underscores in custom_screens.txt (encoded/decoded entirely in
   Python and here) rather than teaching the existing simple
   space-delimited line parser to handle quoted strings -- keeps the
   parser exactly as simple as it already is for ELEMENT/IMAGE lines. */
#define MAX_TEXTS 4 /* small screen, plenty for freeform labels */
typedef struct {
    char content[48];
    int x, y;
    int font_size; /* G15_TEXT_SMALL(0)/MED(1)/LARGE(2)/HUGE(3) */
} text_t;

/* Real-time audio bar visualizer -- direct request: "add ... a little
   visualizer" alongside media info, later refined with a reference
   image ("this is how the visualizer needs to look like") showing a
   segmented, blocky multi-bar equalizer. Capped at 1 per screen (not
   MAX_ELEMENTS-style multiple) because there is exactly ONE persistent
   audio-capture stream shared process-wide (see audio_visualizer.h) --
   a second visualizer element would just duplicate the same bar data,
   not show anything independently meaningful. */
#define MAX_VISUALIZERS 1
typedef struct {
    int x, y, width, height;
} visualizer_t;

typedef struct {
    element_t elements[MAX_ELEMENTS];
    int count;
    image_t images[MAX_IMAGES];
    int image_count;
    text_t texts[MAX_TEXTS];
    int text_count;
    visualizer_t visualizers[MAX_VISUALIZERS];
    int visualizer_count;
} custom_screen_t;

static custom_screen_t custom_screens[MAX_CUSTOM_SCREENS];

/* Reloaded every time a custom screen is drawn (cheap, small file) so
   edits made live in the GUI show up on the next frame without needing
   the daemon restarted. */
static void load_custom_screens(void) {
    /* Real bug found while adding font-size support to this function:
       text_count was never reset here, unlike count and image_count.
       Harmless for the one-shot --preview mode (a fresh process always
       starts from a zero-initialized static array), but in the live
       LCD loop -- which reloads this file every 1-2s in the SAME
       long-running process -- editing or removing a text element while
       the service is running would append fresh entries past the
       stale old ones instead of replacing them, since parsing always
       starts writing at index cs->text_count. Never visibly hit yet
       (the real custom_screens.txt had zero TEXT lines until tonight),
       but would have shown as ghost/duplicate text on the real keyboard
       the moment text elements were actually used without a service
       restart in between. */
    for (int i = 0; i < MAX_CUSTOM_SCREENS; i++) {
        custom_screens[i].count = 0;
        custom_screens[i].image_count = 0;
        custom_screens[i].text_count = 0;
        custom_screens[i].visualizer_count = 0;
    }
    FILE *f = fopen(custom_screens_path(), "r");
    if (!f) return;
    char line[256];
    int current = -1;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strncmp(line, "SCREEN L", 8) == 0) {
            int n = atoi(line + 8);
            current = (n >= 2 && n <= 5) ? n - 2 : -1;
        } else if (strncmp(line, "ELEMENT ", 8) == 0 && current >= 0) {
            custom_screen_t *cs = &custom_screens[current];
            if (cs->count >= MAX_ELEMENTS) continue;
            element_t *el = &cs->elements[cs->count];
            el->sensor[0] = 0; el->style[0] = 0;
            el->label[0] = 0; el->format[0] = 0;
            strcpy(el->orientation, "horizontal");
            el->x = 0; el->y = 0;
            el->width = 40; /* matches the previous hardcoded bar length -- old
                                config lines with no width= keep looking identical */
            el->height = 28;
            el->font_size = G15_TEXT_SMALL; /* old lines with no font= keep looking identical */
            char rest[256];
            strncpy(rest, line + 8, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = 0;
            char *tok = strtok(rest, " ");
            while (tok) {
                char key[32], val[64];
                if (sscanf(tok, "%31[^=]=%63s", key, val) == 2) {
                    if (strcmp(key, "sensor") == 0) { strncpy(el->sensor, val, sizeof(el->sensor) - 1); el->sensor[sizeof(el->sensor) - 1] = 0; }
                    else if (strcmp(key, "label") == 0) {
                        strncpy(el->label, val, sizeof(el->label) - 1);
                        el->label[sizeof(el->label) - 1] = 0;
                        for (char *p = el->label; *p; p++) if (*p == '_') *p = ' ';
                    }
                    else if (strcmp(key, "format") == 0) {
                        strncpy(el->format, val, sizeof(el->format) - 1);
                        el->format[sizeof(el->format) - 1] = 0;
                    }
                    else if (strcmp(key, "style") == 0) { strncpy(el->style, val, sizeof(el->style) - 1); el->style[sizeof(el->style) - 1] = 0; }
                    else if (strcmp(key, "orientation") == 0 && strcmp(val, "vertical") == 0)
                        strcpy(el->orientation, "vertical");
                    else if (strcmp(key, "x") == 0) el->x = atoi(val);
                    else if (strcmp(key, "y") == 0) el->y = atoi(val);
                    else if (strcmp(key, "width") == 0) el->width = atoi(val);
                    else if (strcmp(key, "height") == 0) el->height = atoi(val);
                    else if (strcmp(key, "font") == 0) {
                        int f = atoi(val);
                        el->font_size = (f >= G15_TEXT_SMALL && f <= G15_TEXT_HUGE) ? f : G15_TEXT_SMALL;
                    }
                }
                tok = strtok(NULL, " ");
            }
            /* DISK_FRIGIDER_PCT was renamed to DISK_SECONDARY_PCT (a
               personal drive nickname replaced with a generic name) --
               this alias means an existing custom_screens.txt saved
               under the old key still loads and renders correctly
               instead of the element silently vanishing. The GUI's own
               save path performs the equivalent Python-side migration
               and rewrites the file under the new key on the next
               edit; this is the read-side half of that same migration. */
            if (strcmp(el->sensor, "DISK_FRIGIDER_PCT") == 0) {
                strncpy(el->sensor, "DISK_SECONDARY_PCT", sizeof(el->sensor) - 1);
                el->sensor[sizeof(el->sensor) - 1] = 0;
            }
            if (el->sensor[0]) cs->count++;
        } else if (strncmp(line, "IMAGE ", 6) == 0 && current >= 0) {
            custom_screen_t *cs = &custom_screens[current];
            if (cs->image_count >= MAX_IMAGES) continue;
            image_t *im = &cs->images[cs->image_count];
            im->path[0] = 0; im->x = 0; im->y = 0; im->width = 0; im->height = 0;
            char rest[256];
            strncpy(rest, line + 6, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = 0;
            char *tok = strtok(rest, " ");
            while (tok) {
                char key[32], val[192];
                if (sscanf(tok, "%31[^=]=%191s", key, val) == 2) {
                    if (strcmp(key, "path") == 0) { strncpy(im->path, val, sizeof(im->path) - 1); im->path[sizeof(im->path) - 1] = 0; }
                    else if (strcmp(key, "x") == 0) im->x = atoi(val);
                    else if (strcmp(key, "y") == 0) im->y = atoi(val);
                    else if (strcmp(key, "width") == 0) im->width = atoi(val);
                    else if (strcmp(key, "height") == 0) im->height = atoi(val);
                }
                tok = strtok(NULL, " ");
            }
            /* clamp to screen bounds -- guards against a hand-edited config
               (this format is deliberately hand-editable) requesting a
               malloc/fread far larger than the 160x43 screen could ever need */
            if (im->path[0] && im->width > 0 && im->width <= G15_LCD_WIDTH &&
                im->height > 0 && im->height <= G15_LCD_HEIGHT) cs->image_count++;
        } else if (strncmp(line, "TEXT ", 5) == 0 && current >= 0) {
            custom_screen_t *cs = &custom_screens[current];
            if (cs->text_count >= MAX_TEXTS) continue;
            text_t *tx = &cs->texts[cs->text_count];
            tx->content[0] = 0; tx->x = 0; tx->y = 0;
            tx->font_size = G15_TEXT_SMALL; /* old lines with no font= keep looking identical */
            char rest[256];
            strncpy(rest, line + 5, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = 0;
            char *tok = strtok(rest, " ");
            while (tok) {
                char key[32], val[192];
                if (sscanf(tok, "%31[^=]=%191s", key, val) == 2) {
                    if (strcmp(key, "content") == 0) {
                        strncpy(tx->content, val, sizeof(tx->content) - 1);
                        tx->content[sizeof(tx->content) - 1] = 0;
                        for (char *p = tx->content; *p; p++) if (*p == '_') *p = ' ';
                    }
                    else if (strcmp(key, "x") == 0) tx->x = atoi(val);
                    else if (strcmp(key, "y") == 0) tx->y = atoi(val);
                    else if (strcmp(key, "font") == 0) {
                        int f = atoi(val);
                        tx->font_size = (f >= G15_TEXT_SMALL && f <= G15_TEXT_HUGE) ? f : G15_TEXT_SMALL;
                    }
                }
                tok = strtok(NULL, " ");
            }
            if (tx->content[0]) cs->text_count++;
        } else if (strncmp(line, "VISUALIZER ", 11) == 0 && current >= 0) {
            custom_screen_t *cs = &custom_screens[current];
            if (cs->visualizer_count >= MAX_VISUALIZERS) continue;
            visualizer_t *vz = &cs->visualizers[cs->visualizer_count];
            vz->x = 0; vz->y = 0; vz->width = 0; vz->height = 0;
            char rest[256];
            strncpy(rest, line + 11, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = 0;
            char *tok = strtok(rest, " ");
            while (tok) {
                char key[32], val[64];
                if (sscanf(tok, "%31[^=]=%63s", key, val) == 2) {
                    if (strcmp(key, "x") == 0) vz->x = atoi(val);
                    else if (strcmp(key, "y") == 0) vz->y = atoi(val);
                    else if (strcmp(key, "width") == 0) vz->width = atoi(val);
                    else if (strcmp(key, "height") == 0) vz->height = atoi(val);
                }
                tok = strtok(NULL, " ");
            }
            if (vz->width > 0 && vz->height > 0) cs->visualizer_count++;
        }
    }
    fclose(f);
}

/* Real rendered bounds per element, recorded every time draw_element()
   runs -- so the GUI editor can position drag/resize hit-boxes exactly
   where things actually are instead of guessing. Python has no way to
   compute label_w itself (that needs the label font's real glyph
   metrics), which is exactly what made the old fixed-offset resize
   handle drift away from the real bar -- "hard to control", reported
   directly. Emitted as a sidecar file only in --preview mode (see
   main()); the live LCD-writing path never touches this, zero cost
   there. */
typedef struct {
    int label_x1, label_y1, label_x2, label_y2; /* covers just the label glyph */
    int is_bar;
    int is_vertical;
    int bar_x1, bar_x2, bar_y1, bar_y2;           /* only valid if is_bar */
    int value_x2; /* real measured right edge of the value text (after
                      any truncation) -- replaces an old fixed "+30px"
                      guess that badly undershot real long values like
                      a song title, confirmed directly: "the blue
                      bars... dont contain the whole variable element
                      (name)" */
} element_bounds_t;

static element_bounds_t g_element_bounds[MAX_ELEMENTS];
static int g_element_bounds_count = 0;

static void draw_element(g15canvas *c, element_t *el) {
    const sensor_def_t *def = find_sensor(el->sensor);
    if (!def) return;
    char default_core_label[8];
    const char *label = el->label[0] ? el->label : def->label;
    if (!el->label[0] && strncmp(el->sensor, "CPU_CORE_", 9) == 0) {
        int core = atoi(el->sensor + 9);
        if (core >= 0 && core < 8) {
            snprintf(default_core_label, sizeof(default_core_label), "C%d", core + 1);
            label = default_core_label;
        }
    }
    if (strcmp(el->sensor, "TIME") == 0 || strcmp(el->sensor, "DATE") == 0) {
        label = "";
    }
    double pct_for_bar;
    char disp[32];
    get_sensor_value(el->sensor, el->format, &pct_for_bar, disp, sizeof(disp));

    int vertical_bar = strcmp(el->style, "bar") == 0 &&
                       strcmp(el->orientation, "vertical") == 0 &&
                       (def->is_percent || def->is_temp) && pct_for_bar >= 0;
    int label_w = 0;
    char sized_label[sizeof(el->label) + 1];
    snprintf(sized_label, sizeof(sized_label), "%s", label);
    int label_height;
    int vertical_label_height = 0;
    if (vertical_bar) {
        int label_budget = el->width;
        int visible_width = G15_LCD_WIDTH - el->x - 1;
        if (label_budget > visible_width) label_budget = visible_width;
        truncate_builtin_label(sized_label, el->font_size, label_budget);
        label_height = measure_builtin_label_height(sized_label, el->font_size);
        vertical_label_height = el->height >= label_height + 2 ? label_height + 1 : 0;
        if (vertical_label_height) {
            label_w = measure_builtin_label_width(sized_label, el->font_size);
            draw_builtin_label(c, sized_label, el->x, el->y, el->font_size);
        } else {
            label_w = el->width;
        }
    } else {
        truncate_builtin_label(sized_label, el->font_size,
                               G15_LCD_WIDTH - TEXT_EDGE_MARGIN - el->x);
        label_height = measure_builtin_label_height(sized_label, el->font_size);
        label_w = measure_builtin_label_width(sized_label, el->font_size);
        draw_builtin_label(c, sized_label, el->x, el->y, el->font_size);
    }
    int value_x = el->x + label_w + 4;

    element_bounds_t *b = (g_element_bounds_count < MAX_ELEMENTS)
        ? &g_element_bounds[g_element_bounds_count++] : NULL;
    if (b) {
        b->label_x1 = el->x; b->label_y1 = el->y;
        b->label_x2 = el->x + label_w;
        b->label_y2 = el->y + (vertical_bar ? vertical_label_height : label_height);
        b->is_bar = 0;
        b->is_vertical = 0;
    }

    /* Real bug found by direct testing: a long value (a real song
       title, via MEDIA_TITLE) rendered straight past the 160px screen
       edge with no fallback -- "it just exits screen". Truncated here
       (measuring the BUILT-IN font's real width via render+scan,
       since it has no width-query API -- see
       measure_builtin_text_width()) before either draw path below
       ever renders it, so nothing this function draws can overflow. */
    int avail = G15_LCD_WIDTH - TEXT_EDGE_MARGIN - value_x;
    truncate_builtin_text(disp, el->font_size, avail);
    int value_w = measure_builtin_text_width(disp, el->font_size);

    if (def->auto_center) {
        /* Truncate against the FULL centered budget (not the
           x-anchored one above -- a centered string can use space on
           BOTH sides of the configured x, so re-measure/re-truncate
           against the whole display instead of just what's to the
           right of value_x) then re-center every draw, since a
           different track's title/artist is a different width every
           time. el->x (dragging up/down) still moves the row's Y --
           only X is forced to center. */
        truncate_builtin_text(disp, el->font_size, G15_LCD_WIDTH - 2 * TEXT_EDGE_MARGIN);
        value_w = measure_builtin_text_width(disp, el->font_size);
        value_x = (G15_LCD_WIDTH - value_w) / 2;
        if (value_x < TEXT_EDGE_MARGIN) value_x = TEXT_EDGE_MARGIN;
        if (b) { b->label_x1 = value_x; b->label_x2 = value_x; }
    }

    /* "bar" only ever applies to a sensor with an honest 0-100 scale
       (a true percent, or a temperature via the same 0-90C convention
       already used on the built-in stats screen). Anything else silently
       falls back to number style rather than inventing a scale. */
    if (vertical_bar) {
        int bar_x1 = el->x;
        int bar_width = el->width;
        int bar_y1 = el->y + vertical_label_height;
        int bar_height = el->height - vertical_label_height;
        if (bar_width < 2) bar_width = 2;
        if (bar_width > G15_LCD_WIDTH - bar_x1) bar_width = G15_LCD_WIDTH - bar_x1;
        if (bar_height < 2) bar_height = 2;
        if (bar_height > G15_LCD_HEIGHT - bar_y1) bar_height = G15_LCD_HEIGHT - bar_y1;
        draw_vertical_slim_bar(c, bar_x1, bar_y1, bar_width, bar_height, (int)pct_for_bar);
        if (b) {
            b->label_x1 = bar_x1;
            b->label_y1 = el->y;
            b->label_x2 = bar_x1 + bar_width;
            b->label_y2 = el->y + vertical_label_height;
            b->is_bar = 1;
            b->is_vertical = 1;
            b->bar_x1 = bar_x1; b->bar_x2 = bar_x1 + bar_width;
            b->bar_y1 = bar_y1; b->bar_y2 = bar_y1 + bar_height;
            b->value_x2 = bar_x1 + bar_width;
        }
    } else if (strcmp(el->style, "bar") == 0 && (def->is_percent || def->is_temp) && pct_for_bar >= 0) {
        int bar_x1 = value_x;
        int bar_x2 = bar_x1 + el->width;
        /* The value text after a bar starts past the bar itself, not
           at value_x -- truncate/measure again against the real
           remaining space from THERE, not the pre-bar budget above. */
        int bar_avail = G15_LCD_WIDTH - TEXT_EDGE_MARGIN - (bar_x2 + 4);
        truncate_builtin_text(disp, el->font_size, bar_avail);
        value_w = measure_builtin_text_width(disp, el->font_size);
        draw_slim_bar(c, bar_x1, bar_x2, el->y, BAR_H, (int)pct_for_bar);
        g15r_renderString(c, (unsigned char*)disp, 0, el->font_size, bar_x2 + 4, el->y);
        if (b) {
            b->is_bar = 1;
            b->is_vertical = 0;
            b->bar_x1 = bar_x1; b->bar_x2 = bar_x2;
            b->bar_y1 = el->y; b->bar_y2 = el->y + BAR_H;
            b->value_x2 = bar_x2 + 4 + value_w;
        }
    } else {
        g15r_renderString(c, (unsigned char*)disp, 0, el->font_size, value_x, el->y);
        if (b) b->value_x2 = value_x + value_w;
    }
}

/* Writes the bounds sidecar for --preview mode. Plain text, one line
   per element in the same order load_custom_screens() produced them
   (matches the GUI's own element list index-for-index). */
static void write_bounds_meta(const char *outpath) {
    /* Same atomic tmp+rename() pattern as write_ppm() above, same
       reasoning -- a stale/mismatched .meta read wouldn't show visible
       image corruption, just wrong hit-testing, but it's the same
       class of torn-read risk against the same reader. */
    char meta_path[300], tmp_path[310];
    snprintf(meta_path, sizeof(meta_path), "%s.meta", outpath);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);
    FILE *f = fopen(tmp_path, "w");
    if (!f) return;
    for (int i = 0; i < g_element_bounds_count; i++) {
        element_bounds_t *b = &g_element_bounds[i];
        if (b->is_bar) {
            fprintf(f, "%d label_x1=%d label_y1=%d label_x2=%d label_y2=%d bar_x1=%d bar_y1=%d bar_x2=%d bar_y2=%d value_x2=%d vertical=%d\n",
                    i, b->label_x1, b->label_y1, b->label_x2, b->label_y2,
                    b->bar_x1, b->bar_y1, b->bar_x2, b->bar_y2, b->value_x2, b->is_vertical);
        } else {
            fprintf(f, "%d label_x1=%d label_y1=%d label_x2=%d label_y2=%d value_x2=%d\n",
                    i, b->label_x1, b->label_y1, b->label_x2, b->label_y2, b->value_x2);
        }
    }
    fclose(f);
    rename(tmp_path, meta_path);
}

/* Loads a converted 1bpp image (see src/png-to-lcd.py) and draws it via
   g15r_drawXBM() -- same function, same call shape, already verified
   working end to end (both the conversion step and this render step)
   before this was wired into the real custom-screen system. Silently
   does nothing if the file can't be read (e.g. it was deleted by hand)
   rather than crashing the whole screen over one missing image. */
static void draw_image_element(g15canvas *c, image_t *im) {
    char full_path[300];
    snprintf(full_path, sizeof(full_path), "%s/%s", data_dir(), im->path);
    FILE *f = fopen(full_path, "rb");
    if (!f) return;
    long expected = (long)((im->width + 7) / 8) * im->height;
    unsigned char *data = malloc(expected);
    if (!data) { fclose(f); return; }
    size_t got = fread(data, 1, expected, f);
    fclose(f);
    if ((long)got == expected) {
        g15r_drawXBM(c, data, im->width, im->height, im->x, im->y);
    }
    free(data);
}

static void draw_text_element(g15canvas *c, text_t *tx) {
    /* Same real overflow bug/fix as draw_element()'s value text --
       freeform TEXT elements had no width limit either. */
    truncate_builtin_text(tx->content, tx->font_size, G15_LCD_WIDTH - TEXT_EDGE_MARGIN - tx->x);
    g15r_renderString(c, (unsigned char*)tx->content, 0, tx->font_size, tx->x, tx->y);
}

/* Segmented, blocky multi-bar equalizer -- direct visual reference
   given by the user (colored gradient bars with distinct LED-style
   blocks). This LCD is 1-bit monochrome (no color hardware exists on
   this device -- a hard, unavoidable constraint, explicitly flagged
   before building this), so the gradient becomes plain black blocks;
   the blocky segmented SHAPE is fully preserved and was visually
   verified (rendered standalone with known fake bar data, looked
   correct) before wiring in real audio. Bar levels come from
   g_viz_bars[] (audio_visualizer.h), updated once per draw cycle from
   a persistent parec capture of the PipeWire monitor source -- see
   that file for why this is player-agnostic (works with Brave,
   Spotify, anything) by construction. */
/* Minimum vertical segment count draw_visualizer_element() guarantees
   for a short element -- see the real bug this fixes in that
   function's own comment. */
#define VIZ_MIN_SEGMENTS 8
static void draw_visualizer_element(g15canvas *c, visualizer_t *vz) {
    /* Bar/segment size are FIXED constants -- dragging the element
       bigger adds MORE bars/segments at this same fixed size, rather
       than a fixed bar/segment COUNT just getting fatter to fill a
       bigger box (direct report: "i dragged it longer and did not
       extend" -- there was visibly nothing new to see, just bigger
       blocks). num_bars is capped at VIZ_NUM_BARS, the real number of
       frequency bins the DFT engine computes (see audio_visualizer.h)
       -- more on-screen columns than that would just repeat data, not
       show more real detail.

       bar_w doubled 2->4 per direct request ("a bit more wide, maybe
       make every single line, a double line"), then pulled back to 3
       per a later direct request ("make the lines a bit thinner, 1px
       maybe thinner, they look too big compared to the small text")
       -- still bolder than the original 2px hairlines, just not as
       heavy as 4px next to small label text. seg_h left at 2
       (unchanged) since bar width/thickness is a horizontal-only
       concern, not vertical segment resolution (see VIZ_MIN_SEGMENTS
       above for that separate fix). */
    int bar_w = 3, bar_gap = 1, seg_h = 2, seg_gap = 1;
    int num_bars = vz->width / (bar_w + bar_gap);
    if (num_bars > VIZ_NUM_BARS) num_bars = VIZ_NUM_BARS;
    if (num_bars < 1) num_bars = 1;
    int num_segments = vz->height / (seg_h + seg_gap);

    /* Real bug found from direct comparison across two differently-
       sized visualizers on the same live config: "on L2 where i made
       it pretty tall, it looks brilliant... [L5] just rises half of
       it most of the time, too many lines at once, it was once not
       sensible enough now its too sensitive." L2 (height=39) gets 13
       segments at this fixed 3px/segment spacing; L5 (height=13) only
       gets 4 -- with just 4 discrete levels, VIZ_SCALE's calibrated
       output (tuned to look right on L2's fine 13-level gradient)
       quantizes into big, jumpy steps that read as "too sensitive"
       and "half-full" (landing on 2/4 constantly), not an actual
       sensitivity problem. Fix: guarantee at least MIN_SEGMENTS by
       shrinking seg_h/seg_gap for short elements only -- tall ones
       (already >= the minimum, like L2) are completely unaffected,
       so this doesn't touch the config that's already confirmed
       looking right. */
    if (num_segments < VIZ_MIN_SEGMENTS) {
        int spacing = vz->height / VIZ_MIN_SEGMENTS;
        if (spacing < 1) spacing = 1;
        seg_gap = (spacing >= 2) ? 1 : 0;
        seg_h = spacing - seg_gap;
        if (seg_h < 1) seg_h = 1;
        num_segments = vz->height / (seg_h + seg_gap);
    }
    if (num_segments < 1) num_segments = 1;
    int y2 = vz->y + vz->height - 1;

    /* Direct request, then refined: "the zero positions where the
       bars rise from... i want to be as long as the display" then
       "the bottom bar should represent the bars that WOULD rise if
       the frequency triggered them" -- segmented per-bar-slot blocks
       (same bar_w/bar_gap spacing real bars use) spanning the full
       160px LCD width, not one solid undifferentiated strip. Always
       on, so the widget reads as "present, currently at zero" for
       every possible bar position the moment playback is idle,
       whether or not the element's own configured width covers that
       position. */
    for (int fx = 0; fx < G15_LCD_WIDTH; fx += bar_w + bar_gap) {
        int fx2 = fx + bar_w - 1;
        if (fx2 >= G15_LCD_WIDTH) fx2 = G15_LCD_WIDTH - 1;
        g15r_pixelBox(c, fx, y2 - seg_h + 1, fx2, y2, G15_COLOR_BLACK, 1, 1);
    }

    for (int b = 0; b < num_bars; b++) {
        double level = g_viz_bars[b] * VIZ_SCALE; /* VIZ_SCALE calibrated against real playing audio, see audio_visualizer.h */
        if (level > 1.0) level = 1.0;
        int lit = (int)(level * num_segments + 0.5);
        if (lit > num_segments) lit = num_segments;
        /* Segment 0 is the always-on baseline drawn above -- bars
           only need to draw segments 1..lit-1 (the RISING part above
           zero), not re-draw the baseline itself per-bar. */
        int bx1 = vz->x + b * (bar_w + bar_gap);
        int bx2 = bx1 + bar_w - 1;
        for (int s = 1; s < lit; s++) {
            int sy2 = y2 - s * (seg_h + seg_gap);
            int sy1 = sy2 - seg_h + 1;
            if (sy1 < vz->y) break;
            g15r_pixelBox(c, bx1, sy1, bx2, sy2, G15_COLOR_BLACK, 1, 1);
        }
    }
}

static void draw_custom_screen(g15canvas *c, int screen_num) {
    g_element_bounds_count = 0;
    load_custom_screens();
    custom_screen_t *cs = &custom_screens[screen_num - 2];
    /* Direct request: "this placeholder text on l3 l4 l5 gone" -- the
       "L3 / not set up yet" label used to be drawn here whenever a
       screen was genuinely empty. Confirmed via a real screenshot that
       it read as permanently stuck/hardcoded content rather than an
       honest empty-state indicator, so an unconfigured screen now just
       draws nothing -- matches what the real LCD should show for a
       screen with nothing on it. */
    if (cs->count == 0 && cs->image_count == 0 && cs->text_count == 0 && cs->visualizer_count == 0) {
        return;
    }
    for (int i = 0; i < cs->count; i++) draw_element(c, &cs->elements[i]);
    for (int i = 0; i < cs->image_count; i++) draw_image_element(c, &cs->images[i]);
    for (int i = 0; i < cs->text_count; i++) draw_text_element(c, &cs->texts[i]);
    for (int i = 0; i < cs->visualizer_count; i++) draw_visualizer_element(c, &cs->visualizers[i]);
}

int main(int argc, char **argv) {
    /* Clean up the visualizer's forked parec child on shutdown --
       real leak found in testing: without this, every service
       restart orphaned a parec process forever. */
    signal(SIGTERM, viz_signal_cleanup);
    signal(SIGINT, viz_signal_cleanup);

    /* Three-tier font fallback chain, none of it fatal:
       1. font_path() -- the primary bundled/converted label font.
       2. If that's missing, unparseable, OR loads but fails
          label_font_is_sane() (see font_sanity.h -- guards against a
          font that "loads" but is corrupted, the exact failure class
          documented in this file's own history: a bad conversion once
          rendered 'S' as something closer to '6'), fall back to
          fallback_font_path() -- a second, genuinely clean SIL-OFL
          font (FALLBACK.ttf), same sanity check applied to it too.
       3. If even that's unavailable, draw_label()/label_text_width()/
          label_text_height() all fall back to libg15render's own
          built-in stock font whenever label_font is NULL, so the app
          stays fully usable (just with plainer labels) rather than
          refusing to start at all. */
    label_font = g15r_loadG15Font((char*)font_path());
    if (label_font && !label_font_is_sane(label_font)) {
        fprintf(stderr, "g510-lcd-stats: primary label font at %s failed a "
                         "glyph sanity check (looks corrupted) -- trying the "
                         "bundled fallback font instead\n", font_path());
        g15r_deleteG15Font(label_font);
        label_font = NULL;
    }
    if (!label_font) {
        label_font = g15r_loadG15Font((char*)fallback_font_path());
        if (label_font && !label_font_is_sane(label_font)) {
            fprintf(stderr, "g510-lcd-stats: fallback label font at %s also "
                             "failed a glyph sanity check -- using the stock "
                             "font for labels instead\n", fallback_font_path());
            g15r_deleteG15Font(label_font);
            label_font = NULL;
        }
    }
    if (!label_font) {
        fprintf(stderr, "g510-lcd-stats: no usable custom label font found (see "
                         "README's FONTS section) -- using the stock font for "
                         "labels instead\n");
    }

    /* One-shot preview mode: render a single screen to an image file
       through the exact same drawing code as the live LCD, then exit.
       Used by the GUI's live editor -- never touches /dev/g510-lcd. */
    if (argc >= 3 && strcmp(argv[1], "--preview") == 0) {
        int screen = atoi(argv[2]);
        const char *outpath = argc >= 4 ? argv[3] : "/tmp/g510_preview.ppm";
        g15canvas canvas;
        g15r_initCanvas(&canvas);
        update_net_speed();
        update_nvidia_stats();
        update_media_info();
        viz_set_preview_placeholder();
        if (screen == 1) draw_clock_screen(&canvas);
        else if (screen >= 2 && screen <= 5) {
            draw_custom_screen(&canvas, screen);
            write_bounds_meta(outpath);
        } else draw_stats_screen(&canvas);
        write_ppm(&canvas, outpath);
        return 0;
    }

    while (1) {
        g15canvas canvas;
        g15r_initCanvas(&canvas);

        /* Direct report after actually watching it live: "i can only
           see 4 max 5 line and barely moves" -- root cause: this whole
           loop only ran once per second (sleep(1) below), far too slow
           for a visualizer to look alive. update_net_speed()/
           update_media_info() don't need to run any faster than that
           (network throughput and playerctl's D-Bus query are both
           genuinely expensive to poll faster, and nothing downstream
           needs sub-second freshness for either) -- decoupled onto
           their own ~1s cadence via a wall-clock check, independent of
           how fast the outer loop itself now spins when a visualizer
           is actually on screen (see below). update_visualizer() runs
           every iteration regardless -- it's cheap (a few thousand
           multiply-adds, see audio_visualizer.h) and IS the thing that
           needs to be fast. */
        static time_t last_slow_update = 0;
        time_t now_t = time(NULL);
        if (difftime(now_t, last_slow_update) >= 1.0) {
            update_net_speed();
            update_nvidia_stats();
            update_media_info();
            last_slow_update = now_t;
        }
        update_visualizer();

        int screen = read_screen();
        if (screen == 1) {
            draw_clock_screen(&canvas);
            send_frame(&canvas);
            sleep(1);
            continue;
        }
        if (screen >= 2 && screen <= 5) {
            draw_custom_screen(&canvas, screen);
            send_frame(&canvas);
            /* Only speed up the redraw loop for screens that actually
               HAVE a visualizer on them -- a plain sensor/text/image
               screen gains nothing from redrawing 10x/sec and would
               just burn CPU and USB bandwidth for no visible change. */
            custom_screen_t *cs = &custom_screens[screen - 2];
            if (cs->visualizer_count > 0) {
                /* Was ~10fps (100ms). Direct request: "i need the
                   visualizer respons faster" -- the real bottleneck
                   turned out to be capture latency (see
                   --latency-msec in audio_visualizer.h, ~0.7s fixed
                   down to ~50ms), but once that's fixed this draw
                   cadence becomes the next-largest source of lag, so
                   it's doubled too. 20fps is still cheap for a tiny
                   160x43 monochrome frame over USB. */
                usleep(50000); /* ~20fps */
            } else {
                sleep(1);
            }
            continue;
        }

        draw_stats_screen(&canvas);
        send_frame(&canvas);
        sleep(2);
    }
    return 0;
}
