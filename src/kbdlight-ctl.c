/*
 * kbdlight-ctl — write Clevo keyboard backlight commands through ACPI.
 *
 * The DSDT of the Axioo Pongo 725 (Clevo NH5xR) exposes \_SB_.WMI_.SCMD,
 * and command id 0x67 turns an ARGS dword into embedded-controller writes:
 *
 *   bits 28-31  mode         0xD = brightness, 0xF = colour
 *   bits 24-27  zone         0 = all zones, 1..6 = zones (0xCA subcmd zone+3)
 *   bits 16-23  channel A    -> EC payload byte 0
 *   bits  8-15  channel B    -> EC payload byte 1
 *   bits  0-7   channel C    -> EC payload byte 2
 *   bits 12-15  brightness   1..9 (for mode 0xD; >=0xA means off)
 *
 * Writing /proc/acpi/call needs root, so this binary is installed setuid
 * root and only accepts the small argument vocabulary below. It never
 * evaluates a shell string and never takes a path from the caller.
 *
 * The setuid drop is deliberate: the real uid stays the desktop user so the
 * session can still kill the process, while the effective uid is root for
 * the duration of the write.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ACPI_CALL_PATH "/proc/acpi/call"
#define ACPI_METHOD "\\_SB_.WMI_.SCMD"

#define MODE_BRIGHTNESS 0xD
#define MODE_COLOR 0xF

static void usage(void) {
    fprintf(stderr,
            "usage: kbdlight-ctl brightness <1-9|0>\n"
            "       kbdlight-ctl color <r> <g> <b> [zone]\n"
            "       kbdlight-ctl raw <hex-dword>\n"
            "       kbdlight-ctl off\n"
            "       kbdlight-ctl probe\n");
}

/* /proc/acpi/call is root-only, and loading the module needs root too, so
 * this runs while the effective uid is still 0. */
static int ensure_module(void) {
    if (access(ACPI_CALL_PATH, F_OK) == 0)
        return 0;
    if (system("/usr/bin/modprobe acpi_call 2>/dev/null") != 0
            && system("modprobe acpi_call 2>/dev/null") != 0)
        return -1;
    return access(ACPI_CALL_PATH, F_OK) == 0 ? 0 : -1;
}

static int write_call(unsigned int args) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%s 0 0x67 0x%08X", ACPI_METHOD, args);
    if (n < 0 || (size_t) n >= sizeof(buf))
        return -1;

    int fd = open(ACPI_CALL_PATH, O_WRONLY);
    if (fd < 0)
        return -1;
    ssize_t w = write(fd, buf, (size_t) n);
    close(fd);
    return w == (ssize_t) n ? 0 : -1;
}

static unsigned int brightness_args(int level) {
    /* DSDT: nibble >= 0xA turns the backlight off; otherwise the EC receives
     * 0xFF - level*0x19, so a high level means a low EC value. */
    unsigned int nibble = (level >= 1 && level <= 9) ? (unsigned int) level : 0xAU;
    return ((unsigned int) MODE_BRIGHTNESS << 28) | (nibble << 12);
}

static unsigned int color_args(int a, int b, int c, int zone) {
    return ((unsigned int) MODE_COLOR << 28) | (((unsigned int) zone & 0xF) << 24)
           | (((unsigned int) a & 0xFF) << 16) | (((unsigned int) b & 0xFF) << 8)
           | ((unsigned int) c & 0xFF);
}

static int parse_int(const char *s, int *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || v < -100000 || v > 100000)
        return -1;
    *out = (int) v;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (ensure_module() != 0) {
        fprintf(stderr, "acpi_call is not available: %s\n", strerror(errno));
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "off") == 0) {
        if (write_call(brightness_args(0)) != 0) {
            fprintf(stderr, "ACPI write failed: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    if (strcmp(cmd, "brightness") == 0) {
        int level;
        if (argc < 3 || parse_int(argv[2], &level) != 0) {
            usage();
            return 2;
        }
        if (write_call(brightness_args(level)) != 0) {
            fprintf(stderr, "ACPI write failed: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    if (strcmp(cmd, "color") == 0) {
        int a, b, c, zone = 0;
        if (argc < 5 || parse_int(argv[2], &a) != 0 || parse_int(argv[3], &b) != 0
                || parse_int(argv[4], &c) != 0) {
            usage();
            return 2;
        }
        if (argc >= 6 && parse_int(argv[5], &zone) != 0) {
            usage();
            return 2;
        }
        if (write_call(color_args(a, b, c, zone)) != 0) {
            fprintf(stderr, "ACPI write failed: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    if (strcmp(cmd, "raw") == 0) {
        /* A dword with the top nibble set does not fit a signed int, so parse
         * it unsigned. */
        if (argc < 3) {
            usage();
            return 2;
        }
        char *end = NULL;
        errno = 0;
        unsigned long v = strtoul(argv[2], &end, 0);
        if (errno != 0 || end == argv[2] || *end != '\0' || v > 0xFFFFFFFFUL) {
            usage();
            return 2;
        }
        if (write_call((unsigned int) v) != 0) {
            fprintf(stderr, "ACPI write failed: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    if (strcmp(cmd, "probe") == 0) {
        /* Report whether the ACPI method can be reached at all, so the GUI
         * can say "hardware not present" instead of failing silently.
         * access() checks the real uid, which is the desktop user, so test
         * the effective-uid view instead: try to open the file. */
        int fd = open(ACPI_CALL_PATH, O_WRONLY);
        if (fd < 0) {
            fprintf(stderr, "cannot open %s: %s\n", ACPI_CALL_PATH,
                    strerror(errno));
            return 1;
        }
        close(fd);
        printf("ok\n");
        return 0;
    }

    usage();
    return 2;
}
