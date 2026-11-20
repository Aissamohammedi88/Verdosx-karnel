/*
 * FreaxOS 2026 - desktop/wm.c
 * Gestionnaire de fenêtres « Liquid Glass » / Liquid Glass window manager.
 *
 * FR : fenêtres déplaçables (z-order, focus, réduire/fermer/maximiser), barre
 *      des tâches translucide avec flou du fond (le « liquid glass »), launcher
 *      en grille, desklets flottants. Tout est composé dans un framebuffer
 *      ARGB32 par le logiciel, sans dépendance graphique externe.
 * EN : draggable windows (z-order, focus, min/close/max), translucent taskbar
 *      with background blur (the "liquid glass"), grid launcher, floating
 *      desklets. Everything composed in an ARGB32 software framebuffer.
 */
#include "desktop.h"
#include "freax/vfs.h"
#include "freax/hal.h"
#include "freax/sched.h"
#include "freax/shell.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* état global / global state                                          */
/* ------------------------------------------------------------------ */

static struct dwin g_wins[DWIN_MAX];
static int g_win_count;
static int g_z_top;
static int g_launcher;
static int g_dragging;
static u32 *g_scratch;
static int g_scratch_cap;

static u32 glass_alpha(void)
{
    struct theme *t = theme_get();
    return (u32)(40 + t->glass * 120 / 100);      /* 40..160 */
}

static int glass_radius(void)
{
    return 2 + theme_get()->glass / 20;           /* 2..7 */
}

static u32 glass_tint(void)
{
    return RGBA(24, 28, 40, (u8)glass_alpha());
}

/* verre : flou + teinte + bord lumineux / glass: blur + tint + light border */
static void glass_panel(struct gfx *g, int x, int y, int w, int h, int r)
{
    int need = w * h;
    if (need > g_scratch_cap) {
        u32 *ns = realloc(g_scratch, (u32)need * sizeof(u32));
        if (!ns)
            return;
        g_scratch = ns;
        g_scratch_cap = need;
    }
    gfx_blur(g, x, y, w, h, glass_radius(), g_scratch);
    gfx_round_f(g, x, y, w, h, r, glass_tint());
    gfx_round_o(g, x, y, w, h, r, RGBA(255, 255, 255, 38));
    gfx_hline(g, x + r, x + w - r, y, RGBA(255, 255, 255, 70));
}

void term_capture_begin(char *buf, int cap)
{
    console_set_capture(buf, cap);
}

/* ------------------------------------------------------------------ */
/* fenêtres / windows                                                  */
/* ------------------------------------------------------------------ */

static struct dwin *win_at(int x, int y)
{
    struct dwin *best = NULL;
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (!w->used || w->minimized)
            continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            if (!best || w->z > best->z)
                best = w;
    }
    return best;
}

struct dwin *wm_focused(void)
{
    struct dwin *best = NULL;
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (w->used && !w->minimized)
            if (!best || w->z > best->z)
                best = w;
    }
    return best;
}

int wm_window_count(void) { return g_win_count; }

struct dwin *wm_window(int i)
{
    return (i >= 0 && i < DWIN_MAX) ? &g_wins[i] : NULL;
}

int wm_launcher_open(void) { return g_launcher; }

void wm_toggle_launcher(void) { g_launcher = !g_launcher; }

static void default_rect(const struct app *a, int *x, int *y, int *w, int *h)
{
    if (!strcmp(a->id, "terminal"))      *w = 640, *h = 380;
    else if (!strcmp(a->id, "files"))    *w = 460, *h = 400;
    else if (!strcmp(a->id, "browser"))  *w = 700, *h = 470;
    else if (!strcmp(a->id, "store"))    *w = 560, *h = 450;
    else if (!strcmp(a->id, "settings")) *w = 540, *h = 450;
    else if (!strcmp(a->id, "ext"))      *w = 480, *h = 400;
    else if (!strcmp(a->id, "editor"))   *w = 560, *h = 420;
    else if (!strcmp(a->id, "calc"))     *w = 300, *h = 430;
    else if (!strcmp(a->id, "monitor"))  *w = 430, *h = 390;
    else                                 *w = 470, *h = 390;
    *x = 70 + (g_win_count * 28) % 220;
    *y = 60 + (g_win_count * 22) % 140;
    if (*x + *w > DESK_W - 8)
        *x = DESK_W - 8 - *w;
    if (*y + *h > DESK_H - TASKBAR_H - 8)
        *y = DESK_H - TASKBAR_H - 8 - *h;
}

static void wm_raise(struct dwin *w)
{
    w->z = ++g_z_top;
}

void *win_state_alloc(struct dwin *w, int size)
{
    if (w->state && w->state_cap >= size)
        return w->state;
    void *ns = realloc(w->state, (u32)size);
    if (!ns)
        return w->state;
    memset(ns, 0, (u32)size);
    w->state = ns;
    w->state_cap = size;
    return ns;
}

void *win_state(struct dwin *w) { return w->state; }

void wm_close_win(struct dwin *w)
{
    if (!w || !w->used)
        return;
    free(w->state);
    w->state = NULL;
    w->used = 0;
    g_win_count--;
}

void wm_open_app(const char *id)
{
    /* déjà ouverte → focus / already open → focus */
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (w->used && w->app && !strcmp(w->app->id, id)) {
            w->minimized = 0;
            wm_raise(w);
            return;
        }
    }
    const struct app *a = app_by_id(id);
    if (!a)
        return;
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (w->used)
            continue;
        memset(w, 0, sizeof *w);
        w->used = 1;
        w->app = a;
        default_rect(a, &w->x, &w->y, &w->w, &w->h);
        wm_raise(w);
        g_win_count++;
        if (a->open)
            a->open(w);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* dessin des fenêtres / window drawing                                */
/* ------------------------------------------------------------------ */

static void draw_titlebar(struct gfx *g, struct dwin *w, int focused)
{
    glass_panel(g, w->x, w->y, w->w, TITLE_H, 10);
    if (focused)
        gfx_hline(g, w->x + 12, w->x + 60, w->y, w->app ? theme_get()->accent
                                                             : RGBA(255, 255, 255, 70));
    /* icône + titre / icon + title */
    if (w->app) {
        w->app->icon(g, w->x + 10, w->y + 7, 16, theme_get()->accent);
        gfx_text(g, w->x + 32, w->y + 8,
                 theme_get()->lang ? w->app->name_en : w->app->name_fr,
                 focused ? RGBA(240, 244, 252, 255) : RGBA(170, 178, 195, 255));
    }
    /* boutons / buttons : –  □  × */
    int bx = w->x + w->w - 78;
    u32 dim = focused ? RGBA(255, 255, 255, 190) : RGBA(255, 255, 255, 90);
    gfx_text(g, bx, w->y + 8, "-", dim);
    gfx_text(g, bx + 26, w->y + 6, "[]", dim);
    gfx_text(g, bx + 54, w->y + 7, "x", focused ? RGBA(255, 120, 120, 255)
                                                : RGBA(255, 120, 120, 140));
}

static void draw_window(struct gfx *g, struct dwin *w, u32 ticks)
{
    if (w->minimized)
        return;
    int focused = wm_focused() == w;
    /* corps translucide / translucent body */
    gfx_round_f(g, w->x, w->y + TITLE_H - 2, w->w, w->h - TITLE_H + 2, 10,
                RGBA(16, 19, 30, (u8)(170 + glass_alpha() / 4)));
    gfx_round_o(g, w->x, w->y + TITLE_H - 2, w->w, w->h - TITLE_H + 2, 10,
                RGBA(255, 255, 255, focused ? 60 : 30));
    draw_titlebar(g, w, focused);
    int cw = w->w - 20;
    int ch = w->h - TITLE_H - 14;
    if (w->app && w->app->draw)
        w->app->draw(w, g, w->x + 10, w->y + TITLE_H + 4, cw, ch);
    (void)ticks;
}

/* ------------------------------------------------------------------ */
/* desklets / desklets (widgets flottants)                             */
/* ------------------------------------------------------------------ */

static int g_ext_clock = 1, g_ext_sysmon = 1, g_ext_notes = 0, g_ext_tray = 1;

int desklet_enabled(const char *id)
{
    if (!strcmp(id, "clock"))  return g_ext_clock;
    if (!strcmp(id, "sysmon")) return g_ext_sysmon;
    if (!strcmp(id, "notes"))  return g_ext_notes;
    if (!strcmp(id, "tray"))   return g_ext_tray;
    return 0;
}

void desklet_set(const char *id, int on)
{
    if (!strcmp(id, "clock"))  g_ext_clock = !!on;
    if (!strcmp(id, "sysmon")) g_ext_sysmon = !!on;
    if (!strcmp(id, "notes"))  g_ext_notes = !!on;
    if (!strcmp(id, "tray"))   g_ext_tray = !!on;
}

/* uptime depuis les ticks du HAL / uptime from HAL ticks */
static void fmt_uptime(u32 ticks, char *out, int cap)
{
    u32 secs = ticks / 100;
    snprintf(out, (u32)cap, "%02u:%02u:%02u", secs / 3600,
             (secs / 60) % 60, secs % 60);
}

static void desklet_card(struct gfx *g, int x, int y, int w, int h)
{
    glass_panel(g, x, y, w, h, 12);
}

void desklets_draw(struct gfx *g, u32 ticks)
{
    char buf[64];
    int x = DESK_W - 236;

    if (g_ext_clock) {
        desklet_card(g, x, 18, 216, 86);
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
                 tm->tm_sec);
        gfx_text_scale(g, x + 16, 28, buf, RGBA(240, 244, 252, 255), 2);
        strftime(buf, sizeof buf, "%A %d %B", tm);
        gfx_text(g, x + 16, 66, buf, RGBA(170, 178, 195, 255));
    }
    if (g_ext_sysmon) {
        int y = 18 + (g_ext_clock ? 98 : 0);
        desklet_card(g, x, y, 216, 92);
        gfx_text(g, x + 16, y + 8, tr("Moniteur", "Monitor"),
                 RGBA(190, 197, 214, 255));
        /* mémoire via /proc/meminfo / memory via /proc/meminfo */
        struct vnode *v = vfs_resolve("/proc/meminfo");
        char mem[256];
        if (v) {
            u32 pos = 0;
            int n = vfs_read(v, mem, sizeof mem - 1, &pos);
            mem[n > 0 ? n : 0] = '\0';
            u32 used = 0, total = 1;
            /* une seule passe : strtok est à état partagé / single pass:
               strtok state is shared, two interleaved loops lose lines */
            for (char *line = strtok(mem, "\n"); line;
                 line = strtok(NULL, "\n")) {
                if (!strncmp(line, "HeapUsed", 8)) {
                    const char *cln = strchr(line, ':');
                    if (cln)
                        used = (u32)strtoul(cln + 1, NULL, 10);
                } else if (!strncmp(line, "HeapTotal", 9)) {
                    const char *cln = strchr(line, ':');
                    if (cln)
                        total = (u32)strtoul(cln + 1, NULL, 10);
                }
            }
            if (total < 1) total = 1;
            int bw = 184;
            gfx_round_f(g, x + 16, y + 34, bw, 10, 5, RGBA(255, 255, 255, 40));
            gfx_round_f(g, x + 16, y + 34, bw * (int)used / (int)total, 10, 5,
                        theme_get()->accent);
            snprintf(buf, sizeof buf, "memoire / memory : %u / %u octets",
                     used, total);
            gfx_text(g, x + 16, y + 52, buf, RGBA(150, 158, 175, 255));
        }
        fmt_uptime(ticks, buf, sizeof buf);
        gfx_text(g, x + 16, y + 70, buf, RGBA(150, 158, 175, 255));
    }
    if (g_ext_notes) {
        int y = 18 + (g_ext_clock ? 98 : 0) + (g_ext_sysmon ? 104 : 0);
        desklet_card(g, x, y, 216, 74);
        gfx_text(g, x + 16, y + 8, tr("Notes", "Notes"),
                 RGBA(190, 197, 214, 255));
        struct vnode *v = vfs_resolve("/home/notes.txt");
        char note[200];
        if (v) {
            u32 pos = 0;
            int n = vfs_read(v, note, sizeof note - 1, &pos);
            note[n > 0 ? n : 0] = '\0';
            char *nl = strchr(note, '\n');
            if (nl) *nl = '\0';
            gfx_text(g, x + 16, y + 34, note, RGBA(220, 226, 238, 255));
        }
    }
}

int desklets_pointer(struct devt e)
{
    int x = DESK_W - 236;
    int y = 18;
    if (g_ext_clock)  y += 98;
    if (g_ext_sysmon && e.type == DEV_DOWN &&
        e.mx >= x && e.mx < x + 216 && e.my >= y && e.my < y + 92)
        return 1;                    /* consommé (pas d'action) / consumed */
    if (g_ext_notes && e.type == DEV_DOWN &&
        e.my >= y + (g_ext_sysmon ? 104 : 0) &&
        e.my < y + (g_ext_sysmon ? 104 : 0) + 74 && e.mx >= x && e.mx < x + 216) {
        wm_open_app("editor");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* barre des tâches / taskbar                                          */
/* ------------------------------------------------------------------ */

static int tray_cpu_used(void)
{
    /* indicateur modèle dérivé du vrai ordonnanceur / mock gauge derived
       from the real scheduler activity */
    u32 sw = sched_switch_count();
    return (int)(18 + ((sw * 7) % 46));
}

void tray_draw(struct gfx *g, int x, int y, u32 ticks)
{
    char buf[48];
    if (g_ext_tray) {
        snprintf(buf, sizeof buf, "CPU %d%%", tray_cpu_used());
        gfx_text(g, x, y + 14, buf, RGBA(200, 208, 224, 255));
        x += 86;
        snprintf(buf, sizeof buf, "MEM %d%%",
                 20 + (int)((ticks / 100) % 17));
        gfx_text(g, x, y + 14, buf, RGBA(200, 208, 224, 255));
        x += 86;
    }
    fmt_uptime(ticks, buf, sizeof buf);
    gfx_text(g, x, y + 14, buf, RGBA(200, 208, 224, 255));
}

void taskbar_draw(struct gfx *g, u32 ticks)
{
    glass_panel(g, 8, DESK_H - TASKBAR_H, DESK_W - 16, TASKBAR_H - 8, 16);
    /* bouton launcher / launcher button */
    gfx_round_f(g, 20, DESK_H - TASKBAR_H + 8, 52, 36, 10,
                RGBA(255, 255, 255, g_launcher ? 70 : 30));
    struct theme *t = theme_get();
    for (int i = 0; i < 3; i++)
        gfx_hline(g, 30, 62, DESK_H - TASKBAR_H + 17 + i * 9, t->accent);
    /* fenêtres ouvertes / open windows */
    int bx = 86;
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (!w->used)
            continue;
        int focused = wm_focused() == w && !w->minimized;
        int bwidth = 168;
        gfx_round_f(g, bx, DESK_H - TASKBAR_H + 8, bwidth, 36, 10,
                    focused ? RGBA(255, 255, 255, 64)
                            : RGBA(255, 255, 255, 26));
        w->app->icon(g, bx + 10, DESK_H - TASKBAR_H + 14, 22, t->accent);
        gfx_text(g, bx + 40, DESK_H - TASKBAR_H + 17,
                 t->lang ? w->app->name_en : w->app->name_fr,
                 focused ? RGBA(240, 244, 252, 255) : RGBA(185, 192, 208, 255));
        bx += bwidth + 10;
        if (bx > DESK_W - 340)
            break;
    }
    tray_draw(g, DESK_W - 306, DESK_H - TASKBAR_H + 8, ticks);
}

/* zone cliquable d'un bouton de fenêtre / window button hitbox */
static int taskbar_buttons(const struct devt *e)
{
    int bx = 86;
    for (int i = 0; i < DWIN_MAX; i++) {
        struct dwin *w = &g_wins[i];
        if (!w->used)
            continue;
        if (e->mx >= bx && e->mx <= bx + 178 &&
            e->my >= DESK_H - TASKBAR_H && e->my < DESK_H) {
            if (wm_focused() == w && !w->minimized)
                w->minimized = 1;
            else {
                w->minimized = 0;
                wm_raise(w);
            }
            return 1;
        }
        bx += 178;
        if (bx > DESK_W - 340)
            break;
    }
    return 0;
}

int taskbar_pointer(const struct devt *e, u32 ticks)
{
    if (e->my < DESK_H - TASKBAR_H)
        return 0;
    if (e->type != DEV_DOWN)
        return 1;                    /* dans la barre : consommé / consumed */
    if (e->mx >= 20 && e->mx <= 72) {
        wm_toggle_launcher();
        return 1;
    }
    if (taskbar_buttons(e))
        return 1;
    (void)ticks;
    return 1;
}

/* ------------------------------------------------------------------ */
/* launcher                                                            */
/* ------------------------------------------------------------------ */

/* apps visibles : base toujours + extras si installées / base + installed */
static int launcher_app_visible(int idx)
{
    const struct app *a = app_catalog(idx);
    static const char *base[] = { "terminal", "files", "browser", "store",
                                  "settings" };
    for (unsigned i = 0; i < sizeof base / sizeof *base; i++)
        if (!strcmp(a->id, base[i]))
            return 1;
    return store_is_installed(a->id);
}

static void launcher_draw(struct gfx *g)
{
    int w = 620, h = 440;
    int x = 16, y = DESK_H - TASKBAR_H - h - 8;
    glass_panel(g, x, y, w, h, 18);
    gfx_text_scale(g, x + 24, y + 18, tr("Applications", "Applications"),
                   RGBA(235, 240, 250, 255), 2);
    int col = 0, row = 0;
    for (int i = 0; i < app_catalog_count(); i++) {
        if (!launcher_app_visible(i))
            continue;
        const struct app *a = app_catalog(i);
        int ix = x + 28 + col * 148, iy = y + 66 + row * 118;
        int hover = 0;
        (void)hover;
        gfx_round_f(g, ix - 10, iy - 10, 124, 104, 12, RGBA(255, 255, 255, 24));
        a->icon(g, ix + 34, iy + 4, 44, theme_get()->accent);
        gfx_text(g, ix + 34 - (gfx_text_w(theme_get()->lang ? a->name_en
                                                           : a->name_fr, 1) - 54) / 2,
                 iy + 62, theme_get()->lang ? a->name_en : a->name_fr,
                 RGBA(210, 217, 230, 255));
        col++;
        if (col >= 4) { col = 0; row++; }
    }
}

static int launcher_pointer(const struct devt *e)
{
    int w = 620, h = 440;
    int x = 16, y = DESK_H - TASKBAR_H - h - 8;
    if (e->mx < x || e->mx >= x + w || e->my < y || e->my >= y + h)
        return 0;                        /* dehors : ferme / outside: closes */
    int col = 0, row = 0;
    for (int i = 0; i < app_catalog_count(); i++) {
        if (!launcher_app_visible(i))
            continue;
        int ix = x + 28 + col * 148, iy = y + 66 + row * 118;
        if (e->mx >= ix - 10 && e->mx < ix + 114 &&
            e->my >= iy - 10 && e->my < iy + 94) {
            wm_open_app(app_catalog(i)->id);
            g_launcher = 0;
            return 1;
        }
        col++;
        if (col >= 4) { col = 0; row++; }
    }
    return 1;                            /* dedans sans action / inside, idle */
}

/* ------------------------------------------------------------------ */
/* entrées / input dispatch                                            */
/* ------------------------------------------------------------------ */

static void forward_pointer(struct dwin *w, const struct devt *e, int down)
{
    if (!w || !w->app || !w->app->pointer)
        return;
    w->app->pointer(w, e->mx - w->x - 10, e->my - w->y - TITLE_H - 4,
                    e->btn, down, w->w - 20, w->h - TITLE_H - 14);
}

void wm_input(const struct devt *e, u32 ticks)
{
    if (e->type == DEV_QUIT)
        return;
    if (e->type == DEV_MOTION) {
        if (g_dragging) {
            struct dwin *f = wm_focused();
            if (f) {
                f->x = e->mx - f->drag_off_x;
                f->y = e->my - f->drag_off_y;
                if (f->x < -f->w + 60) f->x = -f->w + 60;
                if (f->x > DESK_W - 40) f->x = DESK_W - 40;
                if (f->y < 0) f->y = 0;
                if (f->y > DESK_H - TASKBAR_H - TITLE_H)
                    f->y = DESK_H - TASKBAR_H - TITLE_H;
            }
        }
        return;
    }

    if (e->type == DEV_KEY) {
        if (e->key == KEY_F2) { wm_toggle_launcher(); return; }
        if (g_launcher && e->key == KEY_ESCAPE) { g_launcher = 0; return; }
        struct dwin *f = wm_focused();
        if (!g_launcher && f && f->app && f->app->key &&
            f->app->key(f, e->key, e->text))
            return;
        return;
    }

    if (e->type == DEV_WHEEL) {
        struct dwin *f = wm_focused();
        if (f && f->app && f->app->pointer)
            f->app->pointer(f, 0, 0, e->wheel, 2, 0, 0);
        return;
    }

    if (e->type == DEV_DOWN) {
        if (g_launcher) {
            if (launcher_pointer(e))
                return;
            g_launcher = 0;
            return;
        }
        if (taskbar_pointer(e, ticks))
            return;
        if (desklets_pointer(*e))
            return;
        struct dwin *w = win_at(e->mx, e->my);
        if (w) {
            wm_raise(w);
            /* barre de titre : drag + boutons / titlebar: drag + buttons */
            if (e->my < w->y + TITLE_H) {
                int bx = w->x + w->w - 78;
                if (e->mx >= bx && e->mx < bx + 78) {
                    if (e->mx >= bx + 48) { wm_close_win(w); return; }
                    if (e->mx >= bx + 24) {           /* maximiser / maximize */
                        static int sx, sy, sw, sh;
                        if (w->w != DESK_W - 20) {
                            sx = w->x; sy = w->y; sw = w->w; sh = w->h;
                            w->x = 8; w->y = 8;
                            w->w = DESK_W - 16;
                            w->h = DESK_H - TASKBAR_H - 12;
                        } else {
                            w->x = sx; w->y = sy; w->w = sw; w->h = sh;
                        }
                        return;
                    }
                    w->minimized = 1;
                    return;
                }
                w->dragging = 1;
                g_dragging = 1;
                w->drag_off_x = e->mx - w->x;
                w->drag_off_y = e->my - w->y;
                return;
            }
            forward_pointer(w, e, 1);
            return;
        }
        return;                              /* bureau : clic libre / desktop */
    }

    if (e->type == DEV_UP) {
        g_dragging = 0;
        for (int i = 0; i < DWIN_MAX; i++)
            g_wins[i].dragging = 0;
        struct dwin *w = win_at(e->mx, e->my);
        forward_pointer(w, e, 0);
    }
}

/* ------------------------------------------------------------------ */
/* composition / composition                                           */
/* ------------------------------------------------------------------ */

void wm_frame(struct gfx *g, u32 ticks)
{
    theme_apply(g);
    desklets_draw(g, ticks);
    /* fenêtres par z croissant / windows by ascending z */
    for (int zz = 1; zz <= g_z_top; zz++)
        for (int i = 0; i < DWIN_MAX; i++)
            if (g_wins[i].used && g_wins[i].z == zz)
                draw_window(g, &g_wins[i], ticks);
    taskbar_draw(g, ticks);
    if (g_launcher)
        launcher_draw(g);
}

void wm_init(void)
{
    memset(g_wins, 0, sizeof g_wins);
    g_win_count = 0;
    g_z_top = 0;
    g_launcher = 0;
    g_dragging = 0;
    /* valeurs par défaut des extensions / extension defaults */
    g_ext_clock = 1;
    g_ext_sysmon = 1;
    g_ext_notes = 0;
    g_ext_tray = 1;
}
