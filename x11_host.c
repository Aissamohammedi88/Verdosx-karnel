/*
 * FreaxOS 2026 - desktop/x11_host.c
 * Affichage X11 par chargement dynamique / X11 display via dlopen.
 *
 * FR : aucune dépendance à la compilation — libX11 est ouvert avec dlopen()
 *      à l'exécution. Le framebuffer ARGB32 du bureau est poussé par
 *      XPutImage. Les événements souris/clavier arrivent au gestionnaire de
 *      fenêtres via devt. Si X11 manque, le mode --screenshot reste usable.
 * EN : no build-time dependency — libX11 is opened with dlopen() at runtime.
 *      The ARGB32 desktop framebuffer is pushed with XPutImage. Mouse/keyboard
 *      events reach the window manager as devt. Without X11, --screenshot
 *      still works.
 */
#include "desktop.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- mini liaisons Xlib / minimal Xlib bindings ---- */

typedef struct x11_Display x11_Display;
typedef unsigned long x11_Window;
typedef unsigned long x11_Atom;
typedef unsigned long x11_KeySym;
typedef unsigned long x11_GC;
typedef void *x11_XPointer;

/* réplique exacte du layout XImage LP64 / exact LP64 XImage layout */
struct x11_XImage {
    int width, height;
    int xoffset;
    int format;                       /* ZPixmap = 2 */
    char *data;
    int byte_order;                   /* LSBFirst = 0 */
    int bitmap_unit, bitmap_bit_order, bitmap_pad;
    int depth;
    int bytes_per_line;
    unsigned long red_mask, green_mask, blue_mask;
    x11_XPointer obdata;
    struct {
        void *create_image, *destroy_image, *get_pixel, *put_pixel,
             *sub_image, *add_pixel;
    } f;
};

/* réplique XEvent LP64 (192 octets) / XEvent LP64 replica (192 bytes) */
struct x11_event {
    int type;
    unsigned long pad[23];
};

_Static_assert(sizeof(struct x11_XImage) == 128,
               "XImage layout mismatch (expected LP64)");
_Static_assert(sizeof(struct x11_event) == 192,
               "XEvent size mismatch (expected LP64)");

/* types d'événements / event types */
#define XEV_KEYPRESS    2
#define XEV_KEYRELEASE  3
#define XEV_BUTTONPRESS 4
#define XEV_BUTTONREL   5
#define XEV_MOTION      6
#define XEV_EXPOSE      12
#define XEV_CLIENTMSG   33

#define X_ZPIXMAP   2
#define X_LSBFIRST  0

/* offsets dans struct x11_event (LP64) / field offsets in x11_event */
#define XOFF_XANY_SERIAL  1                       /* longs : x2..x4  */
#define XOFF_WINDOW       4                       /* long idx 4      */
#define XOFF_ROOT         5
#define XOFF_TIME         7
#define XOFF_X            8                       /* int idx 8*8..    */
#define XOFF_Y            9
#define XOFF_BUTTON       10                      /* state then button */
#define XOFF_FORMAT       6                       /* client msg long 6 */

struct x11_api {
    void *lib;
    x11_Display *(*OpenDisplay)(const char *);
    int (*CloseDisplay)(x11_Display *);
    int (*DefaultScreen)(x11_Display *);
    x11_Window (*RootWindow)(x11_Display *, int);
    unsigned long (*BlackPixel)(x11_Display *, int);
    x11_Window (*CreateSimpleWindow)(x11_Display *, x11_Window, int, int,
                                     unsigned, unsigned, unsigned,
                                     unsigned long, unsigned long);
    int (*MapWindow)(x11_Display *, x11_Window);
    int (*SelectInput)(x11_Display *, x11_Window, long);
    x11_GC (*CreateGC)(x11_Display *, x11_Window, unsigned long,
                       x11_XPointer);
    int (*NextEvent)(x11_Display *, struct x11_event *);
    int (*Pending)(x11_Display *);
    int (*LookupString)(struct x11_event *, char *, int, x11_KeySym *,
                        x11_XPointer);
    int (*PutImage)(x11_Display *, x11_Window, x11_GC, struct x11_XImage *,
                    int, int, int, int, int, unsigned);
    int (*Flush)(x11_Display *);
    int (*DestroyWindow)(x11_Display *, x11_Window);
    int (*FreeGC)(x11_Display *, x11_GC);
    x11_Atom (*InternAtom)(x11_Display *, const char *, int);
    int (*InitImage)(struct x11_XImage *);
};

static struct x11_api X;
static x11_Display *g_dpy;
static x11_Window g_win;
static x11_GC g_gc;
static x11_Atom g_del;
static struct x11_XImage g_img;
static u32 *g_fb;
static int g_w, g_h;

#define INPUT_MASK (0x1L | 0x2L | 0x4L | 0x8L | 0x40L | 0x8000L | 0x20000L)
/* KeyPress|KeyRelease|ButtonPress|ButtonRelease|PointerMotion|Exposure|
   StructureNotify */

/* offsets réels XEvent LP64 / actual LP64 XEvent offsets */
#define EV_X      16          /* int idx : x @64  */
#define EV_Y      17          /* int idx : y @68  */
#define EV_BUTTON 21          /* int idx : button @84 */
#define CM_L0     7           /* long idx : data.l[0] @56 */

int x11_open(int w, int h, const char *title)
{
    const char *libs[] = { "libX11.so.6", "libX11.so", NULL };
    int ok = 0;
    for (int i = 0; libs[i] && !ok; i++) {
        X.lib = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
        if (X.lib)
            ok = 1;
    }
    if (!ok)
        return -1;
#define SYM(field, name) \
    *(void **)(&X.field) = dlsym(X.lib, name); \
    if (!X.field) { dlclose(X.lib); return -1; }
    SYM(OpenDisplay, "XOpenDisplay")
    SYM(DefaultScreen, "XDefaultScreen")
    SYM(RootWindow, "XRootWindow")
    SYM(BlackPixel, "XBlackPixel")
    SYM(CreateSimpleWindow, "XCreateSimpleWindow")
    SYM(MapWindow, "XMapWindow")
    SYM(SelectInput, "XSelectInput")
    SYM(CreateGC, "XCreateGC")
    SYM(NextEvent, "XNextEvent")
    SYM(Pending, "XPending")
    SYM(LookupString, "XLookupString")
    SYM(PutImage, "XPutImage")
    SYM(Flush, "XFlush")
    SYM(DestroyWindow, "XDestroyWindow")
    SYM(FreeGC, "XFreeGC")
    SYM(InternAtom, "XInternAtom")
    SYM(InitImage, "XInitImage")
#undef SYM

    g_dpy = X.OpenDisplay(NULL);
    if (!g_dpy)
        return -1;
    int scr = X.DefaultScreen(g_dpy);
    g_w = w;
    g_h = h;
    g_win = X.CreateSimpleWindow(g_dpy, X.RootWindow(g_dpy, scr),
                                 40, 40, (unsigned)w, (unsigned)h, 1,
                                 X.BlackPixel(g_dpy, scr),
                                 X.BlackPixel(g_dpy, scr));
    g_fb = malloc((u32)w * (u32)h * sizeof(u32));
    if (!g_fb)
        return -1;
    memset(&g_img, 0, sizeof g_img);
    g_img.width = w;
    g_img.height = h;
    g_img.format = X_ZPIXMAP;
    g_img.data = (char *)g_fb;
    g_img.byte_order = X_LSBFIRST;
    g_img.bitmap_unit = 32;
    g_img.bitmap_bit_order = X_LSBFIRST;
    g_img.bitmap_pad = 32;
    g_img.depth = 24;
    g_img.bytes_per_line = w * 4;
    g_img.red_mask = 0xff0000ul;
    g_img.green_mask = 0xff00ul;
    g_img.blue_mask = 0xfful;
    if (!X.InitImage(&g_img))
        return -1;
    g_gc = X.CreateGC(g_dpy, g_win, 0, NULL);
    X.SelectInput(g_dpy, g_win, INPUT_MASK);
    g_del = X.InternAtom(g_dpy, "WM_DELETE_WINDOW", 0);
    (void)g_del;   /* WM_DELETE détecté via ClientMessage / via ClientMessage */
    X.MapWindow(g_dpy, g_win);
    (void)title;
    return 0;
}

u32 *x11_fb(void) { return g_fb; }

void x11_flush(void)
{
    if (!g_dpy)
        return;
    X.PutImage(g_dpy, g_win, g_gc, &g_img, 0, 0, 0, 0, (unsigned)g_w,
               (unsigned)g_h);
    X.Flush(g_dpy);
}

void x11_close(void)
{
    if (!g_dpy)
        return;
    X.DestroyWindow(g_dpy, g_win);
    X.FreeGC(g_dpy, g_gc);
    X.CloseDisplay(g_dpy);
    g_dpy = NULL;
}

static int map_key(x11_KeySym ks)
{
    switch (ks) {
    case 0xFF08: return KEY_BACKSPACE;
    case 0xFF0D: return KEY_RETURN;
    case 0xFF1B: return KEY_ESCAPE;
    case 0xFF09: return KEY_TAB;
    case 0xFF52: return KEY_UP;
    case 0xFF53: return KEY_RIGHT;
    case 0xFF54: return KEY_DOWN;
    case 0xFF50: return KEY_LEFT;
    case 0xFFBF: return KEY_F2;                  /* XK_F2 */
    }
    return 0;
}

/* renvoie 1 si un événement a été traduit / returns 1 when one event
   has been translated */
int x11_poll(struct devt *e)
{
    if (!g_dpy || !X.Pending(g_dpy))
        return 0;
    struct x11_event ev;
    X.NextEvent(g_dpy, &ev);
    int type = ev.type;
    if (type == XEV_MOTION) {
        int *raw = (int *)&ev;
        e->type = DEV_MOTION;
        e->mx = raw[EV_X];
        e->my = raw[EV_Y];
        return 1;
    }
    if (type == XEV_BUTTONPRESS || type == XEV_BUTTONREL) {
        int *raw = (int *)&ev;
        e->type = type == XEV_BUTTONPRESS ? DEV_DOWN : DEV_UP;
        e->mx = raw[EV_X];
        e->my = raw[EV_Y];
        e->btn = raw[EV_BUTTON];
        e->key = 0;
        e->wheel = 0;
        return 1;
    }
    if (type == XEV_KEYPRESS) {
        char buf[8];
        x11_KeySym ks = 0;
        memset(buf, 0, sizeof buf);
        X.LookupString(&ev, buf, (int)sizeof buf - 1, &ks, NULL);
        e->type = DEV_KEY;
        e->mx = 0;
        e->my = 0;
        e->btn = 0;
        e->key = map_key(ks);
        if (!e->key && buf[0] >= 32 && buf[0] < 127) {
            e->text[0] = buf[0];
            e->text[1] = '\0';
        } else if (!e->key) {
            e->text[0] = '\0';
            return 0;                            /* touche ignorée / ignored */
        } else {
            e->text[0] = '\0';
        }
        return 1;
    }
    if (type == XEV_CLIENTMSG) {
        unsigned long *l = (unsigned long *)&ev;
        if (l[CM_L0] == g_del) {
            e->type = DEV_QUIT;
            return 1;
        }
    }
    return 0;                                    /* expose/structure: ignorés */
}
