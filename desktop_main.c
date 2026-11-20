/*
 * FreaxOS 2026 - desktop/desktop_main.c
 * Point d'entrée du bureau / Desktop entry point.
 *
 * FR : desktop_task devient la tâche « init » du noyau portable (à la place
 *      du shell) via kernel_set_init_task(). Le VFS est peuplé (pages /www,
 *      notes, bundles du Store), la conf est chargée depuis
 *      /etc/freaxdesktop.conf, puis le gestionnaire de fenêtres tourne sur
 *      X11. Mode --screenshot : rend des scènes en PPM sans affichage.
 * EN : desktop_task becomes the portable kernel's "init" task (instead of
 *      the shell) via kernel_set_init_task(). The VFS is seeded (/www pages,
 *      notes, Store bundles), config loads from /etc/freaxdesktop.conf, then
 *      the window manager runs on X11. --screenshot mode renders PPM scenes
 *      headless.
 */
#include "desktop.h"
#include "html.h"
#include "freax/kernel.h"
#include "freax/hal.h"
#include "freax/vfs.h"
#include "freax/sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *g_shot_dir;               /* NULL = mode X11 / X11 mode */

void hal_host_install(void);                 /* sim/hal_host.c */

/* ------------------------------------------------------------------ */
/* peuplement du ramfs / ramfs seeding                                 */
/* ------------------------------------------------------------------ */

static void seed_file(const char *path, const char *content)
{
    if (vfs_resolve(path))
        return;
    struct vnode *f = vfs_create(path);
    if (!f)
        return;
    vfs_write(f, content, (u32)strlen(content), 0);
}

static void desktop_seed(void)
{
    vfs_mkdir("/www");
    vfs_mkdir("/home");
    vfs_mkdir("/etc");
    seed_file("/www/accueil.html",
        "<h1>FreaxOS 2026</h1>"
        "<p><b>Liquid Glass</b> — le bureau du noyau FreaxOS. <i>The FreaxOS "
        "kernel desktop.</i></p>"
        "<hr>"
        "<p>Bienvenue ! Ce navigateur rend les pages internes stockées dans "
        "le ramfs du noyau (/www) avec le moteur HTML intégré. Essayez :</p>"
        "<li><a href=\"freax://freaxos.test/noyau\">Le noyau et ses "
        "sous-systèmes</a></li>"
        "<li><a href=\"freax://freaxos.test/012\">Pourquoi 0.12 ?</a></li>"
        "<li><a href=\"freax://freaxos.test/api\">API d'installation des "
        "apps</a></li>"
        "<hr>"
        "<p>Astuce : F2 ouvre le launcher, la barre du bas est en verre "
        "liquide — son intensité se règle dans Réglages.</p>"
        "<p><i>Tip: F2 opens the launcher; the bottom bar is liquid glass — "
        "tune it in Settings.</i></p>");
    seed_file("/www/noyau.html",
        "<h2>Le noyau portable</h2>"
        "<p>FreaxOS embarque un noyau en C (module Rust optionnel) avec :</p>"
        "<li><b>printk</b> + anneau de journal klog (/proc/klog)</li>"
        "<li>allocateur <b>first-fit</b> avec fusion et checks</li>"
        "<li>ordonnanceur <b>round-robin</b> coopératif avec sommeil réel</li>"
        "<li>11 <b>syscalls</b>, ramfs dynamique et /proc</li>"
        "<hr>"
        "<p>Le bureau est lui-même une <b>tâche noyau</b> : la tâche « init » "
        "est remplacée par desktop_task, qui dessine le framebuffer et pilote "
        "les fenêtres pendant que les sous-systèmes tournent.</p>"
        "<p><i>The desktop itself is a kernel task: the \"init\" task is "
        "replaced by desktop_task, which drives the framebuffer and windows "
        "while the subsystems run.</i></p>");
    seed_file("/www/012.html",
        "<h2>Pourquoi 0.12 ?</h2>"
        "<p>En février <b>1992</b>, la version <b>0.12</b> de Linux est la "
        "première publiée sous licence <b>GNU GPL</b> — le certificat de "
        "naissance juridique de Linux.</p>"
        "<p>FreaxOS 2026 rejoue ce moment : un petit noyau complet, lisible, "
        "et un bureau moderne par-dessus. Le nom « Freax » est celui que "
        "Linus Torvalds avait choisi avant que le FTP d'Helsinki ne soit "
        "renommé « linux ».</p>"
        "<p><i>In February 1992, Linux 0.12 became the first GPL release. "
        "FreaxOS 2026 replays that moment, desktop included.</i></p>");
    seed_file("/www/api.html",
        "<h2>API d'installation</h2>"
        "<p>La Boutique installe des <b>bundles</b> via le VFS :</p>"
        "<pre>/repo/&lt;id&gt;.app    paquet source\n/apps/&lt;id&gt;.app    installé\n"
        "/etc/apps.conf    registre (un id par ligne)</pre>"
        "<p>API C : store_install(\"calc\") — int, 0 = ok. Les apps du "
        "launcher sont filtrées par le registre.</p>"
        "<p><i>C API: store_install(\"calc\") returns 0 on success; the "
        "launcher reads the registry.</i></p>");
    seed_file("/home/notes.txt",
        "Merci Linus pour le 0.12 — et pour le liquid glass, on a fait "
        "notre chemin depuis la 386.\n");
    store_seed();
    if (!vfs_resolve("/etc/freaxdesktop.conf"))
        conf_save();                          /* valeurs par défaut / defaults */
}

/* ------------------------------------------------------------------ */
/* tâche bureau / desktop task (init du noyau !)                       */
/* ------------------------------------------------------------------ */

static void shot_frame(struct gfx *g, const char *name, u32 ticks)
{
    wm_frame(g, ticks);
    char path[256];
    snprintf(path, sizeof path, "%s/desktop-%s.ppm", g_shot_dir, name);
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return;
    fprintf(fp, "P6\n%d %d\n255\n", g->w, g->h);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            u32 c = g->fb[(u32)y * (u32)g->w + (u32)x];
            unsigned char rgb[3] = { (unsigned char)GFX_R(c),
                                     (unsigned char)GFX_G(c),
                                     (unsigned char)GFX_B(c) };
            fwrite(rgb, 1, 3, fp);
        }
    fclose(fp);
    printf("  screenshot -> %s\n", path);
}

static void run_scenes(struct gfx *g)
{
    u32 tk = g_hal->ticks();
    /* 1 : bureau avec Terminal + Fichiers / desktop with Terminal + Files */
    wm_init();
    wm_open_app("terminal");
    wm_open_app("files");
    shot_frame(g, "01-bureau-liquid-glass", tk);
    /* 2 : launcher ouvert / launcher open */
    wm_init();
    wm_open_app("terminal");
    wm_toggle_launcher();
    shot_frame(g, "02-launcher", tk);
    /* 3 : navigateur / browser (devant / on top) */
    wm_init();
    wm_open_app("terminal");
    wm_open_app("browser");
    shot_frame(g, "03-navigateur", tk);
    /* 4 : boutique en cours d'installation / store mid-install */
    wm_init();
    store_uninstall("editor");
    wm_open_app("store");
    {
        struct devt e;
        memset(&e, 0, sizeof e);
        e.type = DEV_DOWN;
        e.btn = 1;
        for (int i = 0; i < DWIN_MAX; i++) {
            struct dwin *dw = wm_window(i);
            if (dw && dw->used && dw->app && !strcmp(dw->app->id, "store")) {
                e.mx = dw->x + dw->w - 64;
                e.my = dw->y + TITLE_H + 4 + 40 + 28;
                wm_input(&e, tk);
                e.type = DEV_UP;
                wm_input(&e, tk);
            }
        }
        usleep(1400000);                      /* laisse finir / let it finish */
    }
    shot_frame(g, "04-boutique", g_hal->ticks());
    /* 5 : réglages thème menthe / settings on mint wallpaper */
    wm_init();
    theme_set_wallpaper("mint");
    wm_open_app("settings");
    shot_frame(g, "05-reglages-mint", tk);
    /* 6 : thème pro sombre + EN / dark pro theme + EN */
    wm_init();
    theme_set_wallpaper("grid");
    theme_set_glass(80);
    theme_set_lang(1);
    wm_open_app("editor");
    wm_open_app("calc");
    shot_frame(g, "06-dark-pro-en", tk);
    /* restaure / restore defaults */
    wm_init();
    theme_set_wallpaper("aurora");
    theme_set_glass(55);
    theme_set_lang(0);
    conf_save();
}

void desktop_task(void)
{
    desktop_seed();
    conf_load();
    wm_init();

    struct gfx g;
    u32 *fb = NULL;
    if (g_shot_dir) {
        fb = malloc((u32)DESK_W * (u32)DESK_H * sizeof(u32));
        if (!fb) {
            sched_shutdown();
            return;
        }
        gfx_init(&g, fb, DESK_W, DESK_H);
        run_scenes(&g);
        free(fb);
        sched_shutdown();
        return;
    }

    if (x11_open(DESK_W, DESK_H, "FreaxOS 2026 — Liquid Glass") != 0) {
        printf("X11 indisponible — lancez : ./build/freax-desktop "
               "--screenshot docs/img\n");
        printf("X11 not available — run the screenshot mode instead.\n");
        sched_shutdown();
        return;
    }
    gfx_init(&g, x11_fb(), DESK_W, DESK_H);
    wm_open_app("terminal");
    wm_open_app("browser");

    int quit = 0;
    while (!quit) {
        struct devt e;
        while (x11_poll(&e)) {
            if (e.type == DEV_QUIT) {
                quit = 1;
                break;
            }
            wm_input(&e, g_hal->ticks());
        }
        if (quit)
            break;
        wm_frame(&g, g_hal->ticks());
        x11_flush();
        usleep(16000);                        /* ~60 fps */
        sched_yield();                        /* vraie tâche noyau / real task */
    }
    x11_close();
    sched_shutdown();
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int heap_kib = 8192;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--screenshot")) {
            g_shot_dir = "docs/img";
            if (i + 1 < argc && argv[i + 1][0] != '-')
                g_shot_dir = argv[++i];
        } else if (!strcmp(argv[i], "--mem") && i + 1 < argc) {
            heap_kib = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("FreaxOS Desktop 2026 — Liquid Glass\n"
                   "  freax-desktop [--screenshot [dir]] [--mem KiB]\n");
            return 0;
        }
    }
    if (g_shot_dir) {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "mkdir -p %s", g_shot_dir);
        if (system(cmd) != 0) {
            /* ignoré : le fopen échouera visiblement / fopen will fail loud */
        }
    }

    void *heap = malloc((u32)heap_kib * 1024);
    if (!heap) {
        fprintf(stderr, "desktop : tas hôte indisponible / no host heap\n");
        return 1;
    }
    hal_host_install();
    kernel_set_init_task(desktop_task);
    kernel_boot(heap, (u32)heap_kib * 1024);
    free(heap);
    return 0;
}
