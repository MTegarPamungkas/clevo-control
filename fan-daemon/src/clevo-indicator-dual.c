/*
 ============================================================================
 Name        : clevo-indicator-dual.c
 Description : Ubuntu fan control indicator for Clevo laptops (DUAL FAN)

 Based on clevo-indicator by AqD <iiiaqd@gmail.com/m/jonas-diemer>
  -> https://github.com/SkyLandTW/clevo-indicator

 Modified to control BOTH fans (CPU fan #1 + GPU fan #2) on dual-fan
 Clevo models (e.g. P7xxDM, N1xxED, NH5x_7x, ND, NE, etc.) where the
 upstream version only spins up the left/CPU fan.

 EC protocol (verified against tuxedo-fan-control native/ec_access.cc):
   Write fan duty  -> cmd 0x99, fan-index (0x01=CPU, 0x02=GPU, 0x03=Fan3),
                      raw duty byte (0-255)
   Set fan to AUTO -> cmd 0x99, 0xff, fan-index
   Read CPU fan RPM -> EC register 0xD0 (hi) / 0xD1 (lo)
   Read GPU fan RPM -> EC register 0xD2 (hi) / 0xD3 (lo)
   Read CPU temp     -> EC register 0x07
   Read GPU temp     -> EC register 0xCD

 Build:
   make
   sudo chown root bin/clevo-indicator-dual
   sudo chmod u+s   bin/clevo-indicator-dual
 ============================================================================
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib-unix.h>
#include <libayatana-appindicator/app-indicator.h>

#define NAME "clevo-indicator-dual"

#define EC_SC 0x66
#define EC_DATA 0x62

#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

/* EC registers can be read via EC_SC_READ_CMD or /sys/kernel/debug/ec/ec0/io:
 *
 * 1. modprobe ec_sys
 * 2. od -Ax -t x1 /sys/kernel/debug/ec/ec0/io
 */

#define EC_REG_CPU_TEMP 0x07
#define EC_REG_GPU_TEMP 0xCD
#define EC_REG_FAN1_DUTY 0xCE        /* CPU fan duty (mirror of last written) */
#define EC_REG_FAN1_RPMS_HI 0xD0
#define EC_REG_FAN1_RPMS_LO 0xD1
#define EC_REG_FAN2_RPMS_HI 0xD2     /* GPU fan RPM (second fan) */
#define EC_REG_FAN2_RPMS_LO 0xD3

#define EC_CMD_FAN_DUTY 0x99
#define EC_FAN_INDEX_CPU 0x01
#define EC_FAN_INDEX_GPU 0x02
#define EC_FAN_INDEX_AUTO 0xff       /* followed by fan index => auto mode */

#define MAX_FAN_RPM 4400.0

/* Fan duty below this value is clamped because most Clevo fans stall below ~40%.
 * The EC write helper enforces a sane minimum. */
#define MIN_FAN_DUTY 40
#define MAX_FAN_DUTY 100

typedef enum {
    NA = 0, AUTO = 1, MANUAL = 2, QUIET = 3
} MenuItemType;

typedef enum {
    FAN_CPU = 1, FAN_GPU = 2
} FanIndex;

static void main_init_share(void);
static int main_ec_worker(void);
static void main_ui_worker(int argc, char** argv, pid_t worker_pid);
static gboolean main_on_signal(gpointer user_data);
static void main_on_worker_exit(GPid pid, gint status, gpointer user_data);
static int main_dump_fan(void);
static int main_test_fan(int duty_percentage, int fan_index);
static gboolean ui_update(gpointer user_data);
static void ui_command_set_fan(long fan_duty);
static void ui_command_quit(gchar* command);
static void ui_toggle_menuitems(int fan_duty);
static void ec_on_sigterm(int signum);
static int ec_init(void);
static int ec_auto_duty_adjust(int quiet, int temp, long now_ms);
static void ec_auto_curve_reset(void);
static int ec_auto_curve_active(void);
static bool ec_sample_telemetry(void);
static int ec_query_cpu_temp(void);
static int ec_query_gpu_temp(void);
static int ec_gpu_temp_from_sample(int raw);
static int ec_query_fan1_duty(void);
static int ec_query_fan1_rpms(void);
static int ec_query_fan2_rpms(void);
static int ec_write_fan_duty(int duty_percentage, int fan_index);
static int ec_write_fan_auto(int fan_index);
static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);
static int check_proc_instances(const char* proc_name);
static int ec_lock_acquire(const char* mode);
static int ec_sock_open(void);
static int ec_sock_poll(int* cmd_duty);
static void ec_sock_wait(int timeout_ms);
static void ec_sock_close(void);
static int state_load(void);
static void state_save(int duty);
static void get_time_string(char* buffer, size_t max, const char* format);
static void signal_term(__sighandler_t handler);

static AppIndicator* indicator = NULL;

static long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct {
    char label[256];
    GCallback callback;
    long option;
    MenuItemType type;
    GtkWidget* widget;

} static menuitems[] = {
        { "Set BOTH FANS to AUTO", G_CALLBACK(ui_command_set_fan), 0, AUTO, NULL },
        { "Set BOTH FANS to QUIET (app curve)", G_CALLBACK(ui_command_set_fan), -1, QUIET, NULL },
        { "", NULL, 0L, NA, NULL },
        { "Set BOTH FANS to  40%", G_CALLBACK(ui_command_set_fan), 40, MANUAL, NULL },
        { "Set BOTH FANS to  50%", G_CALLBACK(ui_command_set_fan), 50, MANUAL, NULL },
        { "Set BOTH FANS to  60%", G_CALLBACK(ui_command_set_fan), 60, MANUAL, NULL },
        { "Set BOTH FANS to  70%", G_CALLBACK(ui_command_set_fan), 70, MANUAL, NULL },
        { "Set BOTH FANS to  80%", G_CALLBACK(ui_command_set_fan), 80, MANUAL, NULL },
        { "Set BOTH FANS to  90%", G_CALLBACK(ui_command_set_fan), 90, MANUAL, NULL },
        { "Set BOTH FANS to 100%", G_CALLBACK(ui_command_set_fan), 100, MANUAL, NULL },
        { "", NULL, 0L, NA, NULL },
        { "Quit", G_CALLBACK(ui_command_quit), 0L, NA, NULL }
};

static int menuitem_count = (sizeof(menuitems) / sizeof(menuitems[0]));

struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int gpu_temp;
    volatile int fan1_duty;
    volatile int fan1_rpms;
    volatile int fan2_duty;
    volatile int fan2_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    /* QUIET uses an earlier takeover and a lower duty floor than AUTO. */
    volatile int quiet;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
    /* Every UI command bumps cmd_seq; the worker applies the transition once.
     * Without this, "set 100% then AUTO" never reached the EC: the AUTO handler
     * zeroed auto_duty_val, and the only path writing 0xff required it to be
     * non-zero, so the fans stayed latched at 100%. */
    volatile int cmd_seq;
    volatile int cmd_duty;   /* 0 = AUTO, 40..100 = fixed duty */
} static *share_info = NULL;

static pid_t parent_pid = 0;

/* Exit code of the indicator process. An unexpected worker death is a failure,
 * so systemd's Restart=on-failure brings the daemon back; quitting the tray
 * icon from its menu is a clean success and must stay down. */
static int main_exit_code = EXIT_SUCCESS;

int main(int argc, char* argv[]) {
    /* stdout is a pipe under systemd, which would otherwise buffer "restored
     * MANUAL 80%" for minutes and hide exactly what the daemon is doing. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Dual fan control utility for Clevo laptops (CPU + GPU)\n");
    /* Decide dump-vs-indicator first: in dump mode a busy EC must produce the
     * "stop the service" hint, not the indicator's duplicate-instance dialog. */
    int dump_mode = (argc <= 1
            && (getenv("DISPLAY") == NULL || strlen(getenv("DISPLAY")) == 0))
            || (argc > 1 && argv[1][0] == '-');
    if (!dump_mode && check_proc_instances(NAME) > 1) {
        printf("Multiple running instances!\n");
        char* display = getenv("DISPLAY");
        if (display != NULL && strlen(display) > 0) {
            int desktop_uid = getuid();
            setuid(desktop_uid);
            //
            gtk_init(&argc, &argv);
            GtkWidget* dialog = gtk_message_dialog_new(NULL, 0,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    "Multiple running instances of %s!", NAME);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        return EXIT_FAILURE;
    }
    if (ec_lock_acquire(dump_mode ? "dump" : "indicator") != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (ec_init() != EXIT_SUCCESS) {
        printf("unable to control EC: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (argc <= 1) {
        char* display = getenv("DISPLAY");
        if (display == NULL || strlen(display) == 0) {
            return main_dump_fan();
        } else {
            parent_pid = getpid();
            main_init_share();
            pid_t worker_pid = fork();
            if (worker_pid == 0) {
                signal(SIGCHLD, SIG_DFL);
                signal_term(&ec_on_sigterm);
                /* A GUI that disconnects before reading its reply must not
                 * kill the worker; writes simply fail with EPIPE. */
                signal(SIGPIPE, SIG_IGN);
                return main_ec_worker();
            } else if (worker_pid > 0) {
                main_ui_worker(argc, argv, worker_pid);
                share_info->exit = 1;
                waitpid(worker_pid, NULL, 0);
                return main_exit_code;
            } else {
                printf("unable to create worker: %s\n", strerror(errno));
                return EXIT_FAILURE;
            }
        }
    } else {
        if (argv[1][0] == '-') {
            printf(
                    "\n\
Usage: clevo-indicator-dual [fan-duty-percentage] [fan-index]\n\
\n\
Dump/Control fan duty on dual-fan Clevo laptops. Display indicator by default.\n\
\n\
Arguments:\n\
  [fan-duty-percentage]   Target fan duty in percentage, from %d to %d.\n\
                          Applied to BOTH fans if [fan-index] is omitted.\n\
  [fan-index]             1 = CPU fan only, 2 = GPU fan only.\n\
                          (default: both fans = 1 then 2)\n\
  -?                      Display this help and exit\n\
\n\
Without arguments this program attempts to display an indicator in the\n\
Ubuntu tray area for fan information display and control of BOTH fans.\n\
The indicator requires this program to have setuid=root flag but run from\n\
the desktop user, because a root user is not allowed to display a desktop\n\
indicator while a non-root user is not allowed to control Clevo EC\n\
(Embedded Controller responsible for the fans). Fix permissions:\n\
    sudo chown root clevo-indicator-dual\n\
    sudo chmod u+s   clevo-indicator-dual\n\
\n\
This is a DUAL-FAN fork: it writes fan duty to EC command 0x99 for BOTH\n\
fan index 0x01 (CPU) and fan index 0x02 (GPU), so both fans spin up.\n\
\n\
DO NOT MANIPULATE OR QUERY EC I/O PORTS WHILE THIS PROGRAM IS RUNNING.\n\
\n",
                    MIN_FAN_DUTY, MAX_FAN_DUTY);
            return main_dump_fan();
        } else {
            int val = atoi(argv[1]);
            if (val < MIN_FAN_DUTY || val > MAX_FAN_DUTY) {
                printf("invalid fan duty %d! (valid %d-%d)\n", val,
                        MIN_FAN_DUTY, MAX_FAN_DUTY);
                return EXIT_FAILURE;
            }
            int fan_index = 0; /* 0 = both */
            if (argc >= 3) {
                fan_index = atoi(argv[2]);
                if (fan_index != 1 && fan_index != 2) {
                    printf("invalid fan index %d! (1=CPU, 2=GPU)\n",
                            fan_index);
                    return EXIT_FAILURE;
                }
            }
            return main_test_fan(val, fan_index);
        }
    }
    return EXIT_SUCCESS;
}

static void main_init_share(void) {
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED,
            -1, 0);
    share_info = shm;
    share_info->exit = 0;
    share_info->cpu_temp = 0;
    share_info->gpu_temp = -1;   /* -1 = unknown, not "0C" */
    share_info->fan1_duty = 0;
    share_info->fan1_rpms = 0;
    share_info->fan2_duty = 0;
    share_info->fan2_rpms = 0;
    share_info->auto_duty = 1;
    share_info->auto_duty_val = 0;
    share_info->quiet = 0;
    share_info->manual_next_fan_duty = 0;
    share_info->manual_prev_fan_duty = 0;
    share_info->cmd_seq = 0;
    share_info->cmd_duty = 0;
}

static int main_ec_worker(void) {
    /* No setuid(0) here: the binary is setuid root, so the effective uid is
     * already 0 and EC access works. Leaving the real uid as the desktop user
     * keeps this process killable by that user's session manager — otherwise
     * a stopped service left an unkillable root worker behind.
     *
     * Loading ec_sys is the exception: modprobe is itself setuid and checks
     * the *real* uid, so a plain system("modprobe ec_sys") fails with
     * "Operation not permitted" and the daemon then dies on the missing
     * /sys/kernel/debug/ec/ec0/io. Do it in a child that raises its real uid
     * back to root, and leave our own real uid alone. */
    pid_t mod_pid = fork();
    if (mod_pid == 0) {
        if (setuid(0) != 0)
            _exit(1);
        execl("/usr/bin/modprobe", "modprobe", "ec_sys", (char*) NULL);
        _exit(1);
    } else if (mod_pid > 0) {
        waitpid(mod_pid, NULL, 0);
    }

    /* Restore the mode the user last chose. A fixed duty left over from a
     * previous run is exactly what they asked for, so putting it back is not
     * "sticking": it is the state being remembered across logins. With no
     * saved state the fans go back to the firmware curve. */
    const int saved_duty = state_load();
    int manual_active = 0;
    if (saved_duty == -1) {
        /* Quiet is app-managed, so hand the fans to the firmware first and let
         * the curve take over on the next poll if the machine is warm. */
        ec_write_fan_auto(FAN_CPU);
        ec_write_fan_auto(FAN_GPU);
        share_info->auto_duty = 1;
        share_info->quiet = 1;
        share_info->auto_duty_val = 0;
        printf("fan mode: restored QUIET (app curve, both fans)\n");
    } else if (saved_duty >= MIN_FAN_DUTY) {
        ec_write_fan_duty(saved_duty, FAN_CPU);
        ec_write_fan_duty(saved_duty, FAN_GPU);
        manual_active = 1;
        share_info->auto_duty = 0;
        share_info->auto_duty_val = 0;
        printf("fan mode: restored MANUAL %d%% (both fans)\n", saved_duty);
    } else {
        ec_write_fan_auto(FAN_CPU);
        ec_write_fan_auto(FAN_GPU);
        share_info->auto_duty = 1;
        share_info->quiet = 0;
        printf("fan mode: restored AUTO (firmware curve, both fans)\n");
    }
    if (ec_sock_open() != 0)
        printf("warning: control socket unavailable: %s\n", strerror(errno));
    int applied_seq = share_info->cmd_seq;
    int prev_temp = -1;
    long next_sample_ms = 0;
    long last_valid_sample_ms = monotonic_ms();
    while (share_info->exit == 0) {
        // check parent
        if (parent_pid != 0 && kill(parent_pid, 0) == -1) {
            printf("worker on parent death\n");
            break;
        }
        /* A GUI request arrives through the socket and is applied here, in
         * the same thread that owns the EC. */
        int sock_duty = 0;
        if (ec_sock_poll(&sock_duty)) {
            ec_auto_curve_reset();
            next_sample_ms = 0;
            state_save(sock_duty);
            if (sock_duty == 0) {
                ec_write_fan_auto(FAN_CPU);
                ec_write_fan_auto(FAN_GPU);
                manual_active = 0;
                share_info->auto_duty = 1;
                share_info->quiet = 0;
                share_info->auto_duty_val = 0;
                printf("fan mode: AUTO (firmware curve, both fans) [socket]\n");
            } else if (sock_duty < 0) {
                /* Hand the fans back first: the quiet curve takes over on the
                 * next poll only if the temperature calls for it. */
                ec_write_fan_auto(FAN_CPU);
                ec_write_fan_auto(FAN_GPU);
                manual_active = 0;
                share_info->auto_duty = 1;
                share_info->quiet = 1;
                share_info->auto_duty_val = 0;
                printf("fan mode: QUIET (app curve, both fans) [socket]\n");
            } else {
                ec_write_fan_duty(sock_duty, FAN_CPU);
                ec_write_fan_duty(sock_duty, FAN_GPU);
                manual_active = 1;
                share_info->auto_duty = 0;
                share_info->quiet = 0;
                share_info->auto_duty_val = 0;
                printf("fan mode: MANUAL %d%% (both fans) [socket]\n", sock_duty);
            }
        }
        /* Apply a UI command exactly once. */
        int seq = share_info->cmd_seq;
        if (seq != applied_seq) {
            int duty = share_info->cmd_duty;
            applied_seq = seq;
            ec_auto_curve_reset();
            next_sample_ms = 0;
            state_save(duty);
            if (duty == 0) {
                ec_write_fan_auto(FAN_CPU);
                ec_write_fan_auto(FAN_GPU);
                manual_active = 0;
                share_info->quiet = 0;
                share_info->auto_duty_val = 0;
                printf("fan mode: AUTO (firmware curve, both fans)\n");
            } else if (duty < 0) {
                ec_write_fan_auto(FAN_CPU);
                ec_write_fan_auto(FAN_GPU);
                manual_active = 0;
                share_info->quiet = 1;
                share_info->auto_duty_val = 0;
                printf("fan mode: QUIET (app curve, both fans)\n");
            } else {
                ec_write_fan_duty(duty, FAN_CPU);
                ec_write_fan_duty(duty, FAN_GPU);
                manual_active = 1;
                share_info->quiet = 0;
                share_info->auto_duty_val = 0;
                printf("fan mode: MANUAL %d%% (both fans)\n", duty);
            }
        }
        /* Serve socket activity while waiting for the next sensor sample.
         * Bound the wait so tray commands and shutdown are checked promptly. */
        const long remaining = next_sample_ms - monotonic_ms();
        if (remaining > 0) {
            ec_sock_wait(remaining > 100 ? 100 : (int) remaining);
            continue;
        }

        const bool valid_sample = ec_sample_telemetry();
        const long sample_ms = monotonic_ms();
        int temp_now = -1;
        if (valid_sample) {
            temp_now = MAX(share_info->cpu_temp, share_info->gpu_temp);
            last_valid_sample_ms = sample_ms;
        }
        if (!manual_active) {
            const bool telemetry_lost = sample_ms - last_valid_sample_ms >= 3000;
            if (telemetry_lost)
                ec_auto_curve_reset();
            int next_duty = ec_auto_duty_adjust(share_info->quiet, temp_now, sample_ms);
            if (next_duty != 0 && next_duty != share_info->auto_duty_val) {
                char s_time[256];
                get_time_string(s_time, 256, "%m/%d %H:%M:%S");
                printf(
                        "%s CPU=%dC, GPU=%dC, %s fan duty to %d%% (BOTH fans)\n",
                        s_time, share_info->cpu_temp, share_info->gpu_temp,
                        share_info->quiet ? "quiet" : "auto", next_duty);
                ec_write_fan_duty(next_duty, FAN_CPU);
                ec_write_fan_duty(next_duty, FAN_GPU);
                share_info->auto_duty_val = next_duty;
            } else if (next_duty == 0 && share_info->auto_duty_val != 0) {
                char s_time[256];
                get_time_string(s_time, 256, "%m/%d %H:%M:%S");
                printf("%s CPU=%dC, %s - fans back to firmware AUTO\n",
                        s_time, share_info->cpu_temp,
                        telemetry_lost ? "EC telemetry unavailable" : "cool enough");
                ec_write_fan_auto(FAN_CPU);
                ec_write_fan_auto(FAN_GPU);
                share_info->auto_duty_val = 0;
            }
        }
        /* Adaptive cadence: 5 Hz only while the curve is in charge or the
         * temperature is moving. On a steady machine that is on the firmware
         * curve (or on a fixed manual duty), 750 ms is plenty: the EC reacts
         * to heat on its own, and a real spike trips the `moving` guard on
         * the next sample, which puts the loop back at 5 Hz. */
        const int moving = prev_temp >= 0 && temp_now >= 0
                && abs(temp_now - prev_temp) >= 2;
        prev_temp = temp_now;
        const int sleep_ms = (!manual_active
                && (ec_auto_curve_active() || moving)) ? 200 : 750;
        next_sample_ms = monotonic_ms() + sleep_ms;
    }
    /* Never leave the EC latched at a manual duty. */
    ec_sock_close();
    ec_write_fan_auto(FAN_CPU);
    ec_write_fan_auto(FAN_GPU);
    printf("worker quit\n");
    return EXIT_SUCCESS;
}

static bool ec_sample_telemetry(void) {
    int fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;

    /* ec_sys issues one ACPI EC transaction per byte read. The CPU
     * temperature and the GPU/fan register block are all we need. */
    unsigned char cpu;
    unsigned char fan[EC_REG_FAN2_RPMS_LO - EC_REG_GPU_TEMP + 1];
    const bool ok = pread(fd, &cpu, sizeof(cpu), EC_REG_CPU_TEMP) == sizeof(cpu)
            && pread(fd, fan, sizeof(fan), EC_REG_GPU_TEMP) == sizeof(fan);
    close(fd);
    if (!ok || cpu == 0 || cpu >= 128)
        return false;

    share_info->cpu_temp = cpu;
    share_info->gpu_temp = ec_gpu_temp_from_sample(fan[0]);
    share_info->fan1_duty = calculate_fan_duty(fan[EC_REG_FAN1_DUTY - EC_REG_GPU_TEMP]);
    share_info->fan1_rpms = calculate_fan_rpms(
            fan[EC_REG_FAN1_RPMS_HI - EC_REG_GPU_TEMP],
            fan[EC_REG_FAN1_RPMS_LO - EC_REG_GPU_TEMP]);
    share_info->fan2_rpms = calculate_fan_rpms(
            fan[EC_REG_FAN2_RPMS_HI - EC_REG_GPU_TEMP],
            fan[EC_REG_FAN2_RPMS_LO - EC_REG_GPU_TEMP]);
    /* Most ECs have no separate duty register for the GPU fan. */
    share_info->fan2_duty = share_info->fan1_duty;
    return true;
}

static void main_ui_worker(int argc, char** argv, pid_t worker_pid) {
    printf("Indicator (dual fan)...\n");
    int desktop_uid = getuid();
    setuid(desktop_uid);
    //
    gtk_init(&argc, &argv);

    /* SIGTERM at logout must stop the worker too, not leave root-owned EC
     * readers behind. g_unix_signal_add runs these in the main loop, so they
     * may safely touch GTK (a raw signal handler may not). */
    g_unix_signal_add(SIGTERM, &main_on_signal, GINT_TO_POINTER(SIGTERM));
    g_unix_signal_add(SIGINT, &main_on_signal, GINT_TO_POINTER(SIGINT));
    g_unix_signal_add(SIGHUP, &main_on_signal, GINT_TO_POINTER(SIGHUP));
    signal(SIGPIPE, SIG_IGN);

    /* A child watch only fires when the worker actually terminates. The old
     * SIGCHLD handler also fired when the worker was merely stopped — which
     * systemd does while it settles a service into its cgroup — and killed
     * the whole daemon about half a second after login. */
    g_child_watch_add(worker_pid, &main_on_worker_exit, NULL);
    //
    GtkWidget* indicator_menu = gtk_menu_new();
    for (int i = 0; i < menuitem_count; i++) {
        GtkWidget* item;
        if (strlen(menuitems[i].label) == 0) {
            item = gtk_separator_menu_item_new();
        } else {
            item = gtk_menu_item_new_with_label(menuitems[i].label);
            g_signal_connect_swapped(item, "activate",
                    G_CALLBACK(menuitems[i].callback),
                    (void* ) menuitems[i].option);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(indicator_menu), item);
        menuitems[i].widget = item;
    }
    gtk_widget_show_all(indicator_menu);
    //
    indicator = app_indicator_new(NAME,
            "/usr/share/icons/hicolor/scalable/apps/clevo-indicator-dual.svg",
            APP_INDICATOR_CATEGORY_HARDWARE);
    g_assert(APP_IS_INDICATOR(indicator));
    app_indicator_set_label(indicator, "Init..", "XX");
    /* ACTIVE, not ATTENTION: hosts render NeedsAttention items with the
     * (empty) attention icon instead of IconName, which showed as a
     * missing-icon placeholder. */
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_ordering_index(indicator, -2);
    app_indicator_set_title(indicator, "Clevo Dual");
    app_indicator_set_menu(indicator, GTK_MENU(indicator_menu));
    g_timeout_add(500, &ui_update, NULL);
    ui_toggle_menuitems(share_info->quiet ? -1
            : (share_info->auto_duty ? 0 : share_info->fan1_duty));
    gtk_main();
    printf("main on UI quit\n");
}

static gboolean main_on_signal(gpointer user_data) {
    const int signum = GPOINTER_TO_INT(user_data);
    printf("main on signal: %s\n", strsignal(signum));
    if (share_info != NULL)
        share_info->exit = 1;
    gtk_main_quit();
    return G_SOURCE_CONTINUE;
}

static void main_on_worker_exit(GPid pid, gint status, gpointer user_data) {
    (void) user_data;
    g_spawn_close_pid(pid);
    if (WIFEXITED(status))
        fprintf(stderr, "worker exited with status %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "worker died from signal %d\n", WTERMSIG(status));
    /* Not a menu quit: let systemd restart the daemon instead of silently
     * leaving the fan without its EC owner. */
    main_exit_code = EXIT_FAILURE;
    if (share_info != NULL)
        share_info->exit = 1;
    gtk_main_quit();
}

static int main_dump_fan(void) {
    printf("Dump fan information (dual)\n");
    printf("  CPU FAN Duty: %d%%\n", ec_query_fan1_duty());
    printf("  CPU FAN RPMs: %d RPM\n", ec_query_fan1_rpms());
    printf("  GPU FAN RPMs: %d RPM\n", ec_query_fan2_rpms());
    printf("  CPU Temp:     %dC\n", ec_query_cpu_temp());
    int gpu = ec_query_gpu_temp();
    if (gpu > 0)
        printf("  GPU Temp:     %dC\n", gpu);
    else
        printf("  GPU Temp:     n/a  (dGPU runtime-suspended; EC reg 0xCD dead)\n");
    return EXIT_SUCCESS;
}

static int main_test_fan(int duty_percentage, int fan_index) {
    printf("Change fan duty to %d%% (index=%s)\n", duty_percentage,
            fan_index == 0 ? "BOTH" : (fan_index == 1 ? "CPU" : "GPU"));
    if (fan_index == 0 || fan_index == 1)
        ec_write_fan_duty(duty_percentage, FAN_CPU);
    if (fan_index == 0 || fan_index == 2)
        ec_write_fan_duty(duty_percentage, FAN_GPU);
    printf("\n");
    main_dump_fan();
    return EXIT_SUCCESS;
}

static gboolean ui_update(gpointer user_data) {
    /* Label: CPUtemp / GPUtemp and the two fan RPMs so you can SEE that
     * both fans are spinning. */
    char label[256];
    if (share_info->gpu_temp > 0)
        sprintf(label, "%dC %dC", share_info->cpu_temp, share_info->gpu_temp);
    else
        sprintf(label, "%dC", share_info->cpu_temp);
    app_indicator_set_label(indicator, label, "XXXXXX");

    /* Tooltip: detailed dual-fan status */
    char title[256];
    if (share_info->gpu_temp > 0) {
        snprintf(title, sizeof(title),
                "Clevo Dual Fan | CPU %dC %dRPM | GPU %dC %dRPM",
                share_info->cpu_temp, share_info->fan1_rpms,
                share_info->gpu_temp, share_info->fan2_rpms);
    } else {
        snprintf(title, sizeof(title),
                "Clevo Dual Fan | CPU %dC %dRPM | GPU -- %dRPM",
                share_info->cpu_temp, share_info->fan1_rpms,
                share_info->fan2_rpms);
    }
    app_indicator_set_title(indicator, title);

    /* Icon reflects the faster of the two fans. */
    char icon_name[256];
    int max_rpms = MAX(share_info->fan1_rpms, share_info->fan2_rpms);
    double load = ((double) max_rpms) / MAX_FAN_RPM * 100.0;
    double load_r = round(load / 5.0) * 5.0;
    if (load_r > 100.0)
        load_r = 100.0;
    /* An absolute path resolves regardless of the host's icon theme; a bare
     * theme name renders as a missing-icon placeholder under Yaru. */
    strcpy(icon_name, "/usr/share/icons/hicolor/scalable/apps/clevo-indicator-dual.svg");
    app_indicator_set_icon(indicator, icon_name);
    return G_SOURCE_CONTINUE;
}

static void ui_command_set_fan(long fan_duty) {
    int fan_duty_val = (int) fan_duty;
    if (fan_duty_val == 0) {
        printf("clicked on fan duty auto (both fans)\n");
        share_info->auto_duty = 1;
        share_info->quiet = 0;
    } else if (fan_duty_val < 0) {
        printf("clicked on fan duty quiet (both fans)\n");
        share_info->auto_duty = 1;
        share_info->quiet = 1;
    } else {
        printf("clicked on fan duty: %d (both fans)\n", fan_duty_val);
        share_info->auto_duty = 0;
        share_info->quiet = 0;
    }
    share_info->auto_duty_val = 0;
    share_info->manual_next_fan_duty = fan_duty_val;
    /* Publish the command: the worker applies it exactly once. */
    share_info->cmd_duty = fan_duty_val;
    share_info->cmd_seq = share_info->cmd_seq + 1;
    ui_toggle_menuitems(fan_duty_val);
}

static void ui_command_quit(gchar* command) {
    printf("clicked on quit\n");
    gtk_main_quit();
}

static void ui_toggle_menuitems(int fan_duty) {
    for (int i = 0; i < menuitem_count; i++) {
        if (menuitems[i].widget == NULL)
            continue;
        if (fan_duty == 0)
            gtk_widget_set_sensitive(menuitems[i].widget,
                    menuitems[i].type != AUTO);
        else if (fan_duty < 0)
            gtk_widget_set_sensitive(menuitems[i].widget,
                    menuitems[i].type != QUIET);
        else
            gtk_widget_set_sensitive(menuitems[i].widget,
                    menuitems[i].type != MANUAL
                            || (int) menuitems[i].option != fan_duty);
    }
}

static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0)
        return EXIT_FAILURE;
    if (ioperm(EC_SC, 1, 1) != 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static void ec_on_sigterm(int signum) {
    /* Signal handlers may only touch a sig_atomic_t-like flag: printf() and
     * strsignal() can deadlock against a stdio lock the interrupted code
     * already holds. */
    (void) signum;
    if (share_info != NULL)
        share_info->exit = 1;
}

/* The tray menu and the GUI socket both change the fan mode, and the user
 * expects that choice to survive a logout, a crash and a reboot. The state
 * lives in /var/lib, which is root-owned, so a setuid-root daemon can never
 * be tricked into following a symlink planted in a user-writable directory. */
#define STATE_DIR "/var/lib/clevo-indicator-dual"
#define STATE_FILE STATE_DIR "/fan-mode"

static int state_load(void) {
    FILE* file = fopen(STATE_FILE, "r");
    if (file == NULL)
        return 0;
    int duty = 0;
    if (fscanf(file, "%d", &duty) != 1)
        duty = 0;
    fclose(file);
    if (duty == -1)
        return -1;   /* quiet */
    if (duty < MIN_FAN_DUTY || duty > MAX_FAN_DUTY)
        duty = 0;
    return duty;
}

static void state_save(int duty) {
    if (duty != 0 && duty != -1
            && (duty < MIN_FAN_DUTY || duty > MAX_FAN_DUTY))
        return;
    if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST)
        return;

    /* Written beside the target and renamed over it, so a crash leaves either
     * the old mode or the new one, never a truncated file. */
    char temp[64];
    snprintf(temp, sizeof(temp), STATE_FILE ".tmp.%ld", (long) getpid());
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return;
    char buffer[16];
    int size = snprintf(buffer, sizeof(buffer), "%d\n", duty);
    ssize_t written = write(fd, buffer, (size_t) size);
    close(fd);
    if (written != size || rename(temp, STATE_FILE) != 0)
        unlink(temp);
}

/* Both curves start on firmware control. QUIET takes over sooner with a
 * lower duty floor. Smoothing, separate up/down thresholds and dwell
 * suppress changes caused by short temperature spikes. */
#define AUTO_TAKEOVER_C  70
#define AUTO_RELEASE_C   64
#define QUIET_TAKEOVER_C 54
#define QUIET_RELEASE_C  52

/* How much of each new sample to mix in. The average rises fast and falls
 * slowly: reacting late to heat risks damage, while reacting late to cooling
 * only means the fan runs a moment longer. A symmetric average was tried
 * first and lagged so far that a genuine 78C spike smoothed to 63C, so the
 * fans never spun up when they were actually needed. */
#define FAN_SMOOTH_UP_SHIFT   1   /* halves the gap per sample */
#define FAN_SMOOTH_DOWN_SHIFT 3   /* an eighth per sample */

/* A new duty must survive this long before it is written. Raising is allowed
 * to be quicker than lowering: reacting late to heat risks damage, while
 * reacting late to cooling only means the fan runs a moment longer. */
#define FAN_DWELL_UP_MS   1200
#define FAN_DWELL_DOWN_MS 4000

/* Release only after a sustained cool period at the lowest app duty. */
#define FAN_RELEASE_MS 12000
#define FAN_DOWN_STEP 10

struct fan_step {
    int hi;   /* go up to this duty at or above this smoothed temperature */
    int lo;   /* come back down below this one */
    int duty;
};

/* Highest first: the first band whose `hi` is reached wins on the way up, and
 * the first whose `lo` is still exceeded holds on the way down. */
static const struct fan_step FAN_STEPS[] = {
    { 88, 82, 100 },
    { 82, 76,  90 },
    { 76, 70,  80 },
    { 70, 66,  70 },
    { 66, 64,  60 },
};
#define FAN_STEP_COUNT ((int) (sizeof(FAN_STEPS) / sizeof(FAN_STEPS[0])))

static const struct fan_step QUIET_STEPS[] = {
    { 88, 82, 100 },
    { 82, 76,  90 },
    { 76, 70,  80 },
    { 70, 64,  70 },
    { 64, 58,  60 },
    { 58, 56,  50 },
    { 52, 50,  40 },
};
#define QUIET_STEP_COUNT ((int) (sizeof(QUIET_STEPS) / sizeof(QUIET_STEPS[0])))

/* Curve state survives between polls; it is reset whenever the mode changes,
 * so AUTO state never leaks into QUIET or back. */
static int curve_holding = 0;
static double curve_smoothed = -1;
static int curve_target = 0;
static int curve_candidate = 0;
static long curve_candidate_since = 0;
static long curve_cool_since = -1;

static void ec_auto_curve_reset(void) {
    curve_holding = 0;
    curve_smoothed = -1;
    curve_target = 0;
    curve_candidate = 0;
    curve_candidate_since = 0;
    curve_cool_since = -1;
}

static int ec_auto_curve_active(void) {
    return curve_holding;
}

static int ec_auto_duty_adjust(int quiet, int temp, long now_ms) {
    const struct fan_step* steps = quiet ? QUIET_STEPS : FAN_STEPS;
    const int step_count = quiet ? QUIET_STEP_COUNT : FAN_STEP_COUNT;
    const int takeover_c = quiet ? QUIET_TAKEOVER_C : AUTO_TAKEOVER_C;
    const int release_c = quiet ? QUIET_RELEASE_C : AUTO_RELEASE_C;
    const int floor_duty = steps[step_count - 1].duty;

    if (temp < 0) {
        curve_cool_since = -1;
        curve_candidate = curve_target;
        return curve_target;
    }

    /* A hot sample bypasses smoothing and dwell at the top of the curve. */
    if (temp >= steps[0].hi) {
        curve_holding = 1;
        curve_smoothed = temp;
        curve_target = steps[0].duty;
        curve_candidate = curve_target;
        curve_cool_since = -1;
        return curve_target;
    }

    /* Asymmetric exponential moving average: fast up, slow down. */
    if (curve_smoothed < 0)
        curve_smoothed = temp;
    else if (temp > curve_smoothed)
        curve_smoothed += (temp - curve_smoothed) / (1 << FAN_SMOOTH_UP_SHIFT);
    else
        curve_smoothed += (temp - curve_smoothed) / (1 << FAN_SMOOTH_DOWN_SHIFT);

    if (curve_holding && temp <= release_c && curve_target <= floor_duty) {
        if (curve_cool_since < 0)
            curve_cool_since = now_ms;
        if (curve_target == 0 || now_ms - curve_cool_since >= FAN_RELEASE_MS) {
            ec_auto_curve_reset();
            curve_smoothed = temp;
            return 0;
        }
    } else {
        curve_cool_since = -1;
    }

    /* Not engaged yet: stay on the firmware curve until the takeover point. */
    if (!curve_holding && (temp < takeover_c || curve_smoothed < takeover_c))
        return 0;

    if (!curve_holding && temp > curve_smoothed) {
        /* First engagement: do not act on a lagging average. The firmware may
         * already be running harder than the smoothed value suggests (a fast
         * rise sampled at 750 ms while released), so start from the real
         * temperature instead of dropping below what the EC is doing. */
        curve_smoothed = temp;
    }

    curve_holding = 1;

    /* Pick the band for the smoothed temperature. Going up uses `hi`; staying
     * uses `lo`, which is what makes each band sticky. */
    int want = 0;
    for (int i = 0; i < step_count; i++) {
        if (curve_smoothed >= steps[i].hi
                || (curve_target >= steps[i].duty && curve_smoothed >= steps[i].lo)) {
            want = steps[i].duty;
            break;
        }
    }
    if (want == 0)
        want = floor_duty;
    if (want < curve_target)
        want = MAX(want, curve_target - FAN_DOWN_STEP);

    /* Dwell: only act once the target has been stable long enough. This is
     * what stops a two-second spike from spinning the fans up and back down. */
    if (want != curve_candidate) {
        curve_candidate = want;
        curve_candidate_since = now_ms;
        return curve_target;   /* keep what we have while the candidate settles */
    }
    const long dwell = (want > curve_target) ? FAN_DWELL_UP_MS : FAN_DWELL_DOWN_MS;
    if (want != curve_target && (now_ms - curve_candidate_since) >= dwell) {
        curve_target = want;
        return curve_target;
    }
    return curve_target;
}

static int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

/* GPU temperature.
 *
 * EC register 0xCD reads 0 on this machine (Axioo Pongo 725 / NH5xR) even
 * while the dGPU is awake, so a plain read displays a bogus "0C" and makes
 * the auto curve ignore the GPU entirely.
 *
 * Reading the temperature through NVML requires opening /dev/nvidia0, which
 * takes a runtime-PM reference and resets the autosuspend timer. Polling it
 * therefore kept the discrete GPU awake forever: a feedback loop where
 * watching the temperature was the reason it stayed hot and battery-hungry.
 *
 * The fix is to read the temperature only while something else is clearly
 * already using the GPU, and never more often than the sample below. If the
 * GPU is suspended we report -1 ("unknown") and let the firmware's own curve
 * look after it.
 */
#define DGPU_POWER_PATH "/sys/bus/pci/devices/0000:01:00.0/power/runtime_status"

static int gpu_temp_cached = -1;
static time_t gpu_temp_stamp = 0;
static time_t gpu_temp_last_good = 0;

/* A suspended GPU needs no temperature sampling at all; when it is awake a
 * sample every few seconds is plenty for a fan curve. */
#define GPU_TEMP_SAMPLE_SEC 5

static int ec_query_gpu_temp(void) {
    return ec_gpu_temp_from_sample(ec_io_read(EC_REG_GPU_TEMP));
}

static int ec_gpu_temp_from_sample(int raw) {
    if (raw > 0 && raw < 128)
        return raw;

    /* Register is dead on this model. */
    time_t now = time(NULL);
    if (now - gpu_temp_stamp < GPU_TEMP_SAMPLE_SEC)
        return gpu_temp_cached;

    FILE* f = fopen(DGPU_POWER_PATH, "r");
    if (f == NULL)
        return gpu_temp_cached;
    char state[32] = { 0 };
    const bool active = fgets(state, sizeof(state), f) != NULL
                        && strncmp(state, "active", 6) == 0;
    fclose(f);

    /* Suspended: do not touch NVML at all. Opening the device is what wakes
     * the GPU, so sampling a sleeping one would defeat runtime PM. */
    if (!active) {
        gpu_temp_cached = -1;
        gpu_temp_stamp = now;
        gpu_temp_last_good = 0;
        return gpu_temp_cached;
    }

    gpu_temp_stamp = now;
    FILE* p = popen(
            "/usr/bin/timeout -k 0.2s 0.6s /usr/bin/nvidia-smi "
            "--query-gpu=temperature.gpu --format=csv,noheader 2>/dev/null",
            "r");
    if (p != NULL) {
        int t = -1;
        if (fscanf(p, "%d", &t) == 1 && t > 0 && t < 128) {
            gpu_temp_cached = t;
            gpu_temp_last_good = now;
        }
        pclose(p);
    }
    if (now - gpu_temp_last_good >= 3 * GPU_TEMP_SAMPLE_SEC)
        gpu_temp_cached = -1;
    return gpu_temp_cached;
}

static int ec_query_fan1_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN1_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN1_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

static int ec_query_fan1_duty(void) {
    int raw_duty = ec_io_read(EC_REG_FAN1_DUTY);
    return calculate_fan_duty(raw_duty);
}

static int ec_query_fan2_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN2_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN2_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

/* Write fan duty to a specific fan.
 * fan_index: FAN_CPU (0x01) or FAN_GPU (0x02).
 * This is the KEY difference vs upstream clevo-indicator, which only ever
 * wrote index 0x01. We now also write 0x02 so the second fan spins up. */
static int ec_write_fan_duty(int duty_percentage, int fan_index) {
    if (duty_percentage < MIN_FAN_DUTY || duty_percentage > MAX_FAN_DUTY) {
        printf("Wrong fan duty to write: %d (valid %d-%d)\n", duty_percentage,
                MIN_FAN_DUTY, MAX_FAN_DUTY);
        return EXIT_FAILURE;
    }
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    uint32_t idx = (fan_index == FAN_GPU) ? 0x02 : 0x01;
    return ec_io_do(EC_CMD_FAN_DUTY, idx, v_i);
}

/* 0x99 0xff <index> hands that fan back to the firmware curve. */
static int ec_write_fan_auto(int fan_index) {
    uint32_t idx = (fan_index == FAN_GPU) ? 0x02 : 0x01;
    return ec_io_do(EC_CMD_FAN_DUTY, EC_FAN_INDEX_AUTO, idx);
}

static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x\n",
                port, data, flag, value);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    //wait_ec(EC_SC, EC_SC_IBF_FREE);
    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(cmd, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, IBF, 0);
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}

static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    return raw_rpm > 0 ? (2156220 / raw_rpm) : 0;
}

/* Serialize EC access between every process of this program. The EC needs
 * ordered multi-byte command sequences and has no kernel-level locking, so a
 * second reader/writer (a CLI dump, a second indicator) corrupts the replies:
 * that is where impossible readings like "25978 RPM" or "0C" come from.
 * The lock is taken before the UI/worker fork, so it covers the whole run. */
#define EC_LOCK_PATH "/run/clevo-indicator-dual.lock"

/* A small Unix socket so a GUI can read live telemetry and request fan
 * changes without touching the EC itself. The daemon owns the EC; anyone
 * else poking the I/O ports corrupts its replies, which is exactly the class
 * of bug that produced impossible readings like "25978 RPM" before.
 *
 * Protocol is one line in, one line out:
 *   "status"          -> "ok cpu=NN gpu=NN rpm1=NN rpm2=NN duty=NN auto=N mode=NAME"
 *   "set <0|40..100>" -> "ok"   (0 = hand back to the firmware curve)
 *   "set quiet"       -> "ok"   (low app curve with a 40% floor)
 */
#define EC_SOCK_PATH "/run/clevo-indicator-dual.sock"

static int ec_sock_fd = -1;

static void ec_sock_wait(int timeout_ms) {
    struct pollfd listener = { .fd = ec_sock_fd, .events = POLLIN };
    poll(&listener, 1, timeout_ms);
}

static int ec_sock_open(void) {
    struct sockaddr_un addr;
    if (strlen(EC_SOCK_PATH) >= sizeof(addr.sun_path))
        return -1;
    ec_sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (ec_sock_fd < 0)
        return -1;
    unlink(EC_SOCK_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, EC_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(ec_sock_fd, (struct sockaddr*) &addr, sizeof(addr)) != 0) {
        close(ec_sock_fd);
        ec_sock_fd = -1;
        return -1;
    }
    /* Any local user may read telemetry; setting a duty is the same
     * permission the tray menu already granted, so the socket is 0666 and the
     * daemon still validates the value. */
    chmod(EC_SOCK_PATH, 0666);
    listen(ec_sock_fd, 8);
    return 0;
}

static void ec_sock_close(void) {
    if (ec_sock_fd >= 0) {
        close(ec_sock_fd);
        ec_sock_fd = -1;
        unlink(EC_SOCK_PATH);
    }
}

/* Returns 1 when a mode command was received and *cmd_duty was set. */
static int ec_sock_poll(int* cmd_duty) {
    if (ec_sock_fd < 0)
        return 0;
    int fd = accept(ec_sock_fd, NULL, NULL);
    if (fd < 0)
        return 0;

    /* The client can connect before it writes. Bound that wait so a stalled
     * client cannot stop the fan loop. */
    struct pollfd client = { .fd = fd, .events = POLLIN };
    if (poll(&client, 1, 25) <= 0) {
        close(fd);
        return 0;
    }

    int applied = 0;
    char req[64] = { 0 };
    ssize_t n = recv(fd, req, sizeof(req) - 1, MSG_DONTWAIT);
    if (n > 0) {
        req[n] = '\0';
        char* nl = strchr(req, '\n');
        if (nl != NULL)
            *nl = '\0';

        char reply[192];
        if (strcmp(req, "status") == 0) {
            const char* mode = "manual";
            if (share_info->auto_duty)
                mode = share_info->quiet ? "quiet" : "auto";
            snprintf(reply, sizeof(reply),
                    "ok cpu=%d gpu=%d rpm1=%d rpm2=%d duty=%d auto=%d mode=%s\n",
                    share_info->cpu_temp, share_info->gpu_temp,
                    share_info->fan1_rpms, share_info->fan2_rpms,
                    share_info->fan1_duty, share_info->auto_duty, mode);
        } else if (strcmp(req, "set quiet") == 0) {
            *cmd_duty = -1;
            applied = 1;
            strcpy(reply, "ok\n");
        } else if (strncmp(req, "set ", 4) == 0) {
            char* end;
            const char* value = req + 4;
            long v = strtol(value, &end, 10);
            if (end != value && *end == '\0'
                    && (v == 0 || (v >= MIN_FAN_DUTY && v <= MAX_FAN_DUTY))) {
                *cmd_duty = (int) v;
                applied = 1;
                strcpy(reply, "ok\n");
            } else {
                snprintf(reply, sizeof(reply), "err valid %d-%d or 0\n",
                        MIN_FAN_DUTY, MAX_FAN_DUTY);
            }
        } else {
            strcpy(reply, "err unknown\n");
        }
        send(fd, reply, strlen(reply), MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    close(fd);
    return applied;
}


static int ec_lock_fd = -1;

static int ec_lock_acquire(const char* mode) {
    ec_lock_fd = open(EC_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (ec_lock_fd < 0) {
        /* /run may be unwritable in odd setups; do not block the program. */
        fprintf(stderr, "warning: cannot open %s: %s (EC not locked)\n",
                EC_LOCK_PATH, strerror(errno));
        return EXIT_SUCCESS;
    }
    if (flock(ec_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr,
                "EC is busy: another clevo-indicator-dual process owns it.\n"
                "Querying the EC while it is in use returns garbage values.\n");
        if (strcmp(mode, "dump") == 0) {
            fprintf(stderr,
                    "Stop it first:  systemctl --user stop clevo-indicator-dual\n");
        }
        close(ec_lock_fd);
        ec_lock_fd = -1;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int check_proc_instances(const char* proc_name) {
    /* /proc/<pid>/comm is truncated to TASK_COMM_LEN-1 = 15 characters, so a
     * 20-character name like "clevo-indicator-dual" never matched the old
     * strncmp(proc_name_len) and the duplicate-instance guard was dead code. */
    char short_name[16];
    snprintf(short_name, sizeof(short_name), "%s", proc_name);
    int proc_name_len = strlen(short_name);
    pid_t this_pid = getpid();
    DIR* dir;
    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc");
        return -1;
    }
    int instance_count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char* endptr;
        long lpid = strtol(ent->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;
        if (lpid == this_pid)
            continue;
        char buf[512];
        snprintf(buf, sizeof(buf), "/proc/%ld/comm", lpid);
        FILE* fp = fopen(buf, "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) != NULL) {
                if ((buf[proc_name_len] == '\n' || buf[proc_name_len] == '\0')
                        && strncmp(buf, short_name, proc_name_len) == 0) {
                    fprintf(stderr, "Process: %ld\n", lpid);
                    instance_count += 1;
                }
            }
            fclose(fp);
        }
    }
    closedir(dir);
    return instance_count;
}

static void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

static void signal_term(__sighandler_t handler) {
    signal(SIGHUP, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    /* SIGPIPE is ignored in the worker instead: a GUI that disconnects before
     * reading its reply must not shut the whole daemon down. */
    signal(SIGALRM, handler);
    signal(SIGTERM, handler);
    signal(SIGUSR1, handler);
    signal(SIGUSR2, handler);
}
