/*
 * FreaxOS 2026 - desktop/theme.c
 * Thèmes, fonds d'écran procéduraux, configuration persistée / Themes,
 * procedural wallpapers, persisted configuration.
 *
 * FR : la conf vit dans /etc/freaxdesktop.conf via le VFS du noyau — la
 *      personnalisation survit au reboot virtuel.
 * EN : config lives in /etc/freaxdesktop.conf through the kernel VFS —
 *      customization survives a virtual reboot.
 */
#include "desktop.h"
#include "freax/vfs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static struct theme g_theme = { "aurora", RGB(0x22, 0xD3, 0xEE), 55, 0 };
static u32 *g_wall = NULL;      /* couche fond d'écran / wallpaper layer */

/* ------------------------------------------------------------------ */
/* Configuration / config                                              */
/* ------------------------------------------------------------------ */

void conf_set(const char *key, const char *val)
{
    if (!key || !val)
        return;
    if (strcmp(key, "wallpaper") == 0)
        theme_set_wallpaper(val);
    else if (strcmp(key, "accent") == 0) {
        long r = strtol(val + 0, NULL, 16);
        long gg = strtol(val + 2, NULL, 16);
        long b = strtol(val + 4, NULL, 16);
        g_theme.accent = RGB((u8)r, (u8)gg, (u8)b);
    }
    else if (strcmp(key, "glass") == 0)
        theme_set_glass((int)strtol(val, NULL, 10));
    else if (strcmp(key, "lang") == 0)
        theme_set_lang(val[0] == 'e' ? 1 : 0);
    else if (strcmp(key, "ext_clock") == 0)
        desklet_set("clock", val[0] == '1');
    else if (strcmp(key, "ext_sysmon") == 0)
        desklet_set("sysmon", val[0] == '1');
    else if (strcmp(key, "ext_notes") == 0)
        desklet_set("notes", val[0] == '1');
    else if (strcmp(key, "ext_tray") == 0)
        desklet_set("tray", val[0] == '1');
}

const char *conf_get(const char *key, const char *dflt)
{
    static char v[32];
    if (!key)
        return dflt;
    if (strcmp(key, "wallpaper") == 0) return g_theme.wallpaper;
    if (strcmp(key, "lang") == 0)      return g_theme.lang ? "en" : "fr";
    snprintf(v, sizeof v, "%06X", g_theme.accent & 0xFFFFFF);
    if (strcmp(key, "accent") == 0)    return v;
    snprintf(v, sizeof v, "%d", g_theme.glass);
    if (strcmp(key, "glass") == 0)     return v;
    (void)dflt;
    return NULL;
}

void conf_save(void)
{
    struct vnode *etc = vfs_resolve("/etc");
    if (!etc) {
        vfs_mkdir("/etc");
        etc = vfs_resolve("/etc");
        if (!etc)
            return;
    }
    struct vnode *f = vfs_resolve("/etc/freaxdesktop.conf");
    if (!f)
        f = vfs_create("/etc/freaxdesktop.conf");
    if (!f)
        return;
    vfs_truncate(f);
    char line[96];
    vfs_write(f, "wallpaper=", 10, f->size);
    vfs_write(f, conf_get("wallpaper", ""), (u32)strlen(conf_get("wallpaper", "")), f->size);
    vfs_write(f, "\naccent=", 8, f->size);
    vfs_write(f, conf_get("accent", "22D3EE"), 6, f->size);
    vfs_write(f, "\nglass=", 7, f->size);
    vfs_write(f, conf_get("glass", "55"), (u32)strlen(conf_get("glass", "55")), f->size);
    vfs_write(f, "\nlang=", 6, f->size);
    vfs_write(f, conf_get("lang", "fr"), (u32)strlen(conf_get("lang", "fr")), f->size);
    vfs_write(f, "\next_clock=", 11, f->size);
    vfs_write(f, desklet_enabled("clock") ? "1" : "0", 1, f->size);
    vfs_write(f, "\next_sysmon=", 12, f->size);
    vfs_write(f, desklet_enabled("sysmon") ? "1" : "0", 1, f->size);
    vfs_write(f, "\next_notes=", 11, f->size);
    vfs_write(f, desklet_enabled("notes") ? "1" : "0", 1, f->size);
    vfs_write(f, "\next_tray=", 10, f->size);
    vfs_write(f, desklet_enabled("tray") ? "1" : "0", 1, f->size);
    (void)line;
    (void)etc;
}

void conf_load(void)
{
    struct vnode *f = vfs_resolve("/etc/freaxdesktop.conf");
    if (!f)
        return;
    char buf[512];
    u32 pos = 0;
    int n = vfs_read(f, buf, (u32)sizeof buf - 1, &pos);
    if (n <= 0)
        return;
    buf[n] = '\0';
    /* parse key=value ligne par ligne / parse key=value line by line */
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l;
         l = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq = '\0';
        conf_set(l, eq + 1);
    }
}

/* ------------------------------------------------------------------ */
/* Thème / theme                                                       */
/* ------------------------------------------------------------------ */

struct theme *theme_get(void) { return &g_theme; }

void theme_set_accent(u32 rgb) { g_theme.accent = rgb | 0xFF000000u; }

void theme_set_glass(int v) { g_theme.glass = v < 0 ? 0 : (v > 100 ? 100 : v); }

void theme_set_lang(int lang) { g_theme.lang = lang ? 1 : 0; }

const char *tr(const char *fr, const char *en)
{
    return g_theme.lang ? en : fr;
}

static const struct {
    const char *id;
    const char *fr, *en;
} WALLS[] = {
    { "aurora",   "Aurora",       "Aurora"       },
    { "mint",     "Menthe",       "Mint"         },
    { "archblue", "Bleu Arch",    "Arch Blue"    },
    { "sunset",   "Crépuscule",   "Sunset"       },
    { "grid",     "Grille nuit",  "Night Grid"   },
};

int theme_wallpaper_count(void) { return 5; }

const char *theme_wallpaper_id(int i)
{
    return (i >= 0 && i < theme_wallpaper_count()) ? WALLS[i].id : "";
}

const char *theme_wallpaper_label(int i, int lang)
{
    if (i < 0 || i >= theme_wallpaper_count())
        return "";
    return lang ? WALLS[i].en : WALLS[i].fr;
}

int theme_set_wallpaper(const char *id)
{
    for (int i = 0; i < theme_wallpaper_count(); i++) {
        if (strcmp(WALLS[i].id, id) == 0) {
            snprintf(g_theme.wallpaper, sizeof g_theme.wallpaper, "%s", id);
            return 0;
        }
    }
    return -1;
}

void theme_init(void)
{
    /* rien d'alloué ici : la couche fond est créée par theme_apply */
}

/* dessin des fonds procéduraux / procedural wallpaper drawing */
static void draw_wall(struct gfx *g)
{
    const char *w = g_theme.wallpaper;
    u32 a = g_theme.accent;

    if (strcmp(w, "mint") == 0) {
        gfx_gradient_diag(g, 0, 0, g->w, g->h,
                          RGB(0x07, 0x2B, 0x1F), RGB(0x0D, 0x4F, 0x3C));
        gfx_blend_rect(g, 0, 0, g->w, g->h, RGBA(0, 40, 30, 60));
        for (int i = 0; i < 5; i++)
            gfx_circle_o(g, g->w / 2, g->h / 2, 90 + i * 85, RGBA(0, 255, 190, 22));
    } else if (strcmp(w, "archblue") == 0) {
        gfx_gradient_diag(g, 0, 0, g->w, g->h,
                          RGB(0x04, 0x08, 0x1E), RGB(0x11, 0x2E, 0x5A));
        for (int i = 0; i < 6; i++)
            gfx_line(g, 60 + i * 170, 0, -80 + i * 170, g->h,
                     RGBA(80, 140, 255, 18));
    } else if (strcmp(w, "sunset") == 0) {
        gfx_gradient_v(g, 0, 0, g->w, g->h,
                       RGB(0x2B, 0x10, 0x3A), RGB(0xC2, 0x4B, 0x22));
        gfx_circle_f(g, g->w / 2, (int)(g->h * 0.72), 110, RGBA(255, 190, 90, 40));
        gfx_circle_f(g, g->w / 2, (int)(g->h * 0.72), 60, RGBA(255, 210, 120, 60));
    } else if (strcmp(w, "grid") == 0) {
        gfx_gradient_v(g, 0, 0, g->w, g->h,
                       RGB(0x05, 0x06, 0x0C), RGB(0x10, 0x14, 0x22));
        for (int x = 0; x < g->w; x += 48)
            gfx_vline(g, x, 0, g->h, RGBA(120, 160, 255, 14));
        for (int y = 0; y < g->h; y += 48)
            gfx_hline(g, 0, g->w, y, RGBA(120, 160, 255, 14));
    } else { /* aurora (défaut / default) */
        gfx_gradient_diag(g, 0, 0, g->w, g->h,
                          RGB(0x07, 0x0B, 0x1C), RGB(0x1B, 0x10, 0x3B));
        for (int i = 0; i < 3; i++) {
            u32 band = i == 0 ? RGBA(34, 211, 238, 26)
                     : i == 1 ? RGBA(168, 85, 247, 24)
                              : RGBA(59, 130, 246, 22);
            for (int y = 0; y < g->h; y++) {
                int xoff = (int)(180 * ((y + i * 130) % 340) / 340.0f * 2.0f);
                gfx_hline(g, i * 240 - 120 + xoff, i * 240 + 260 + xoff, y, band);
            }
        }
        /* étoiles / stars */
        for (int i = 0; i < 90; i++) {
            int x = (i * 137 + 41) % g->w;
            int y = (i * 61 + 17) % (g->h - TASKBAR_H);
            u32 sc = i % 3 == 0 ? RGBA(255, 255, 255, 150)
                                : RGBA(255, 255, 255, 70);
            gfx_px(g, x, y, sc);
            if (i % 7 == 0) gfx_px(g, x + 1, y, sc);
        }
    }
    (void)a;
}

void theme_apply(struct gfx *g)
{
    /* couche fond cachée, redessinée si taille ou thème change / cached */
    static int wall_w, wall_h;
    static char wall_id[24];
    if (g_wall && wall_w == g->w && wall_h == g->h &&
        !strcmp(wall_id, g_theme.wallpaper)) {
        memcpy(g->fb, g_wall, (u32)g->w * (u32)g->h * sizeof(u32));
        return;
    }
    if (g_wall)
        free(g_wall);
    g_wall = malloc((u32)g->w * (u32)g->h * sizeof(u32));
    if (!g_wall)
        return;
    struct gfx wg;
    gfx_init(&wg, g_wall, g->w, g->h);
    draw_wall(&wg);
    wall_w = g->w;
    wall_h = g->h;
    snprintf(wall_id, sizeof wall_id, "%s", g_theme.wallpaper);
    memcpy(g->fb, g_wall, (u32)g->w * (u32)g->h * sizeof(u32));
}
