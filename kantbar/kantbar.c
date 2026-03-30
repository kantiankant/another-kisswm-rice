/*
 * kantbar — minimal status bar for KissWM-Improved
 * shows: workspaces, brightness, volume, battery, time
 * compiles with: cc kantbar.c -o kantbar -lX11 -lXft $(pkg-config --cflags fontconfig)
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t need_redraw = 0;
static void handle_usr1(int sig) { (void)sig; need_redraw = 1; }

/* persistent file descriptors — opened once, pread'd on demand */
static int fd_brn_cur  = -1;
static int fd_brn_max  = -1;
static int fd_bat_cap  = -1;
static int fd_bat_sta  = -1;
static int brn_max_val = -1;   /* max brightness never changes, read once */

/* cached volume — updated only on SIGUSR1 to avoid popen every redraw */
static char vol_cache[32] = "vol ?";

static int
open_first(const char **paths)
{
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd >= 0) return fd;
    }
    return -1;
}

static int
pread_int(int fd)
{
    if (fd < 0) return -1;
    char buf[32];
    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

static void
update_volume(void)
{
    FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!f) { snprintf(vol_cache, sizeof(vol_cache), "vol ?"); return; }
    float vol = 0;
    char muted[16] = "";
    fscanf(f, "Volume: %f %15s", &vol, muted);
    pclose(f);
    if (strstr(muted, "MUTED"))
        snprintf(vol_cache, sizeof(vol_cache), "vol M");
    else
        snprintf(vol_cache, sizeof(vol_cache), "vol %d%%", (int)(vol * 100));
}

/* ── config ───────────────────────────────────────────────── */
#define BAR_HEIGHT      18
#define FONT_NAME       "monospace:size=9"
#define PAD             10

#define COL_BG          "#000000"
#define COL_FG          "#ffffff"
#define COL_WS_ACT_BG   "#ffffff"
#define COL_WS_ACT_FG   "#000000"
#define COL_WS_INA_BG   "#000000"
#define COL_WS_INA_FG   "#555555"

#define NUM_WORKSPACES  8
#define BAT_PATH        "/sys/class/power_supply/BAT1"
/* ─────────────────────────────────────────────────────────── */

static Display     *dpy;
static Window       win;
static int          screen;
static XftDraw     *xftdraw;
static XftFont     *font;
static int          bar_width;

static XftColor col_bg, col_fg;
static XftColor col_ws_act_bg, col_ws_act_fg;
static XftColor col_ws_ina_bg, col_ws_ina_fg;

static Atom net_current_desktop;
static Atom net_number_of_desktops;
static Atom net_active_window;
static Atom net_wm_desktop;
static Atom net_wm_strut;
static Atom net_wm_strut_partial;
static Atom net_wm_window_type;
static Atom net_wm_window_type_dock;
static Atom net_wm_state;
static Atom net_wm_state_sticky;

static void
alloc_color(XftColor *c, const char *hex)
{
    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                           DefaultColormap(dpy, screen), hex, c)) {
        fprintf(stderr, "kantbar: cannot allocate colour %s\n", hex);
        exit(1);
    }
}

static void
set_struts(void)
{
    /* reserve top of screen */
    long strut[4] = { 0, 0, BAR_HEIGHT, 0 };
    long strut_partial[12] = {
        0, 0, BAR_HEIGHT, 0,
        0, 0,
        0, bar_width,
        0, 0,
        0, 0
    };
    XChangeProperty(dpy, win, net_wm_strut, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)strut, 4);
    XChangeProperty(dpy, win, net_wm_strut_partial, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)strut_partial, 12);
}

static void
set_dock(void)
{
    XChangeProperty(dpy, win, net_wm_window_type, XA_ATOM, 32,
                    PropModeReplace,
                    (unsigned char *)&net_wm_window_type_dock, 1);
    XChangeProperty(dpy, win, net_wm_state, XA_ATOM, 32,
                    PropModeReplace,
                    (unsigned char *)&net_wm_state_sticky, 1);
}

static int
get_num_desktops(void)
{
    Atom actual_type;
    int actual_format;
    unsigned long n, bytes_left;
    unsigned char *data = NULL;
    int num = 1;

    if (XGetWindowProperty(dpy, RootWindow(dpy, screen),
                           net_number_of_desktops, 0, 1, False,
                           XA_CARDINAL, &actual_type, &actual_format,
                           &n, &bytes_left, &data) == Success
        && data && actual_type == XA_CARDINAL) {
        num = (int)*(unsigned long *)data;
        XFree(data);
    }
    return num > 0 ? num : 1;
}

static int
get_current_desktop(void)
{
    Atom actual_type;
    int actual_format;
    unsigned long n, bytes_left;
    unsigned char *data = NULL;
    int desk = -1;

    if (XGetWindowProperty(dpy, RootWindow(dpy, screen),
                           net_current_desktop, 0, 1, False,
                           XA_CARDINAL, &actual_type, &actual_format,
                           &n, &bytes_left, &data) == Success
        && data && actual_type == XA_CARDINAL) {
        desk = (int)*(unsigned long *)data;
        XFree(data);
        if (desk >= 0) return desk;
    }

    data = NULL;
    if (XGetWindowProperty(dpy, RootWindow(dpy, screen),
                           net_active_window, 0, 1, False,
                           XA_WINDOW, &actual_type, &actual_format,
                           &n, &bytes_left, &data) == Success && data) {
        Window active = *(Window *)data;
        XFree(data);
        data = NULL;

        if (active && active != None) {
            if (XGetWindowProperty(dpy, active,
                                   net_wm_desktop, 0, 1, False,
                                   XA_CARDINAL, &actual_type, &actual_format,
                                   &n, &bytes_left, &data) == Success && data) {
                desk = (int)*(unsigned long *)data;
                XFree(data);
                return desk;
            }
        }
    }

    return 0;
}

static void
get_battery(char *out, size_t len)
{
    int cap = pread_int(fd_bat_cap);
    if (cap < 0) { snprintf(out, len, "no bat"); return; }

    char buf[32] = "Unknown";
    if (fd_bat_sta >= 0) {
        ssize_t n = pread(fd_bat_sta, buf, sizeof(buf) - 1, 0);
        if (n > 0) buf[n] = '\0';
        /* strip newline */
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }

    char sym = ' ';
    if (strncmp(buf, "Charging", 8) == 0) sym = '+';
    else if (strncmp(buf, "Full", 4) == 0) sym = '=';

    snprintf(out, len, "bat %c%d%%", sym, cap);
}

static void
get_brightness(char *out, size_t len)
{
    int cur = pread_int(fd_brn_cur);
    if (cur < 0 || brn_max_val <= 0) { snprintf(out, len, "brn ?"); return; }
    snprintf(out, len, "brn %d%%", (cur * 100) / brn_max_val);
}

static void
draw_text(int *x, const char *text, XftColor *fg, XftColor *bg, int pad)
{
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, font, (const FcChar8 *)text, strlen(text), &ext);
    int w = ext.xOff + pad * 2;

    XftDrawRect(xftdraw, bg, *x, 0, w, BAR_HEIGHT);
    XftDrawStringUtf8(xftdraw, fg, font,
                      *x + pad,
                      (BAR_HEIGHT + font->ascent - font->descent) / 2,
                      (const FcChar8 *)text, strlen(text));
    *x += w;
}

static void
draw_sep(int *x)
{
    XftDrawRect(xftdraw, &col_ws_ina_fg, *x, 3, 1, BAR_HEIGHT - 6);
    *x += 1;
}

static void
draw_bar(void)
{
    int x = 0;
    char buf[128];
    time_t t;
    struct tm *tm;
    int cur_desk  = get_current_desktop();
    int num_desks = get_num_desktops();

    XftDrawRect(xftdraw, &col_bg, 0, 0, bar_width, BAR_HEIGHT);

    /* ── left: workspaces ── */
    for (int i = 0; i < num_desks; i++) {
        snprintf(buf, sizeof(buf), " %d ", i + 1);
        if (i == cur_desk)
            draw_text(&x, buf, &col_ws_act_fg, &col_ws_act_bg, 0);
        else
            draw_text(&x, buf, &col_ws_ina_fg, &col_ws_ina_bg, 0);
    }

    /* ── right: brn | vol | bat | time ── */
    char brn[32], bat[32], clk[32];
    get_brightness(brn, sizeof(brn));
    get_battery(bat, sizeof(bat));
    t = time(NULL);
    tm = localtime(&t);
    strftime(clk, sizeof(clk), "%H:%M", tm);

    /* measure total right-side width */
    const char *fields[] = { brn, vol_cache, bat, clk };
    int nfields = 4;
    int right_w = 0;
    for (int i = 0; i < nfields; i++) {
        XGlyphInfo e;
        XftTextExtentsUtf8(dpy, font, (const FcChar8 *)fields[i],
                           strlen(fields[i]), &e);
        right_w += e.xOff + PAD * 2;
    }
    right_w += (nfields - 1); /* separators */

    int rx = bar_width - right_w;
    for (int i = 0; i < nfields; i++) {
        if (i > 0) draw_sep(&rx);
        draw_text(&rx, fields[i], &col_fg, &col_bg, PAD);
    }

    XFlush(dpy);
}

int
main(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "kantbar: cannot open display\n");
        return 1;
    }
    screen    = DefaultScreen(dpy);
    bar_width = DisplayWidth(dpy, screen);

    net_current_desktop      = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    net_number_of_desktops   = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    net_active_window        = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    net_wm_desktop           = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    net_wm_strut             = XInternAtom(dpy, "_NET_WM_STRUT", False);
    net_wm_strut_partial     = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    net_wm_window_type       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_dock  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    net_wm_state             = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_sticky      = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);

    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel  = BlackPixel(dpy, screen);
    wa.event_mask        = ExposureMask | PropertyChangeMask;

    /* bar at the top — dmenu spawns at top too, but we don't raise
     * ourselves in the event loop so dmenu wins the stacking order */
    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        0, 0, bar_width, BAR_HEIGHT, 0,
                        DefaultDepth(dpy, screen),
                        InputOutput,
                        DefaultVisual(dpy, screen),
                        CWOverrideRedirect | CWBackPixel | CWEventMask,
                        &wa);

    set_dock();
    set_struts();

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win); /* raise once at startup only */
    XFlush(dpy);

    XSelectInput(dpy, RootWindow(dpy, screen), PropertyChangeMask);

    font = XftFontOpenName(dpy, screen, FONT_NAME);
    if (!font) {
        fprintf(stderr, "kantbar: cannot open font %s\n", FONT_NAME);
        return 1;
    }

    xftdraw = XftDrawCreate(dpy, win,
                            DefaultVisual(dpy, screen),
                            DefaultColormap(dpy, screen));

    alloc_color(&col_bg,         COL_BG);
    alloc_color(&col_fg,         COL_FG);
    alloc_color(&col_ws_act_bg,  COL_WS_ACT_BG);
    alloc_color(&col_ws_act_fg,  COL_WS_ACT_FG);
    alloc_color(&col_ws_ina_bg,  COL_WS_INA_BG);
    alloc_color(&col_ws_ina_fg,  COL_WS_INA_FG);

    signal(SIGUSR1, handle_usr1);

    /* open sysfs fds once — pread'd on every redraw, no open/close theatre */
    const char *brn_cur_paths[] = {
        "/sys/class/backlight/intel_backlight/brightness",
        "/sys/class/backlight/amdgpu_bl0/brightness",
        "/sys/class/backlight/acpi_video0/brightness",
        NULL
    };
    const char *brn_max_paths[] = {
        "/sys/class/backlight/intel_backlight/max_brightness",
        "/sys/class/backlight/amdgpu_bl0/max_brightness",
        "/sys/class/backlight/acpi_video0/max_brightness",
        NULL
    };
    const char *bat_cap_paths[] = {
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/BAT1/capacity",
        NULL
    };
    const char *bat_sta_paths[] = {
        "/sys/class/power_supply/BAT0/status",
        "/sys/class/power_supply/BAT1/status",
        NULL
    };

    fd_brn_cur  = open_first(brn_cur_paths);
    fd_brn_max  = open_first(brn_max_paths);
    fd_bat_cap  = open_first(bat_cap_paths);
    fd_bat_sta  = open_first(bat_sta_paths);
    brn_max_val = pread_int(fd_brn_max);   /* read once — never changes */

    update_volume();   /* initial volume fetch */

    draw_bar();

    int xfd = ConnectionNumber(dpy);
    XEvent ev;
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int r = select(xfd + 1, &fds, NULL, NULL, &tv);

        if (need_redraw) {
            need_redraw = 0;
            update_volume();
            draw_bar();
            continue;
        }

        if (r < 0) continue; /* EINTR from signal, loop again */

        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose)
                draw_bar();
            else if (ev.type == PropertyNotify) {
                Atom a = ev.xproperty.atom;
                if (a == net_current_desktop   ||
                    a == net_number_of_desktops ||
                    a == net_active_window      ||
                    a == net_wm_desktop)
                    draw_bar();
            }
        }
        /* redraw every second for clock updates */
        draw_bar();
    }

    return 0;
}
