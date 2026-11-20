/*
 * FreaxOS 2026 - tests/test_desktop.c
 * Suite « desktop » : bureau Liquid Glass / Liquid Glass desktop suite.
 *
 * FR : thème + conf persistée (roundtrip VFS), moteur HTML (parse/layout/
 *      liens), boutique (install/uninstall + registre), rendu gfx (pixels,
 *      blend, texte non vide, blur), desklets et catalogue d'apps.
 *      Tout tourne sur le noyau réel réinitialisé, sans X11.
 * EN : theme + persisted config (VFS roundtrip), HTML engine (parse/layout/
 *      links), store (install/uninstall + registry), gfx rendering (pixels,
 *      blend, non-empty text, blur), desklets and the app catalog.
 *      Everything runs on the real reset kernel, no X11 needed.
 */
#include "framework.h"
#include "desktop.h"
#include "html.h"
#include "freax/kernel.h"
#include "freax/vfs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void suite_desktop(void)
{
    static u32 fb[320 * 200];
    struct gfx g;
    gfx_init(&g, fb, 320, 200);

    test_reset_kernel();
    theme_init();
    wm_init();

    /* ---- gfx : pixels, blend, texte, blur ---- */
    {
        gfx_fill(&g, RGB(0, 0, 0));
        CHECK(gfx_get(&g, 10, 10) == RGB(0, 0, 0));
        gfx_px(&g, 5, 5, RGB(255, 0, 0));
        CHECK(gfx_get(&g, 5, 5) == RGB(255, 0, 0));
        gfx_px(&g, -3, 5, RGB(1, 2, 3));          /* clipping sans crash */
        CHECK(gfx_get(&g, 5, 5) == RGB(255, 0, 0));

        /* blend : alpha 128 sur blanc → ~128 de gris rouge / blend math */
        gfx_fill(&g, RGB(255, 255, 255));
        gfx_blend_rect(&g, 0, 0, 10, 10, RGBA(255, 0, 0, 128));
        u32 c = gfx_get(&g, 3, 3);
        CHECK(GFX_R(c) == 255);
        CHECK(GFX_G(c) == 127 && GFX_B(c) == 127);   /* 255*(255-128)/255 */

        /* texte : change bien des pixels / text actually paints */
        gfx_fill(&g, RGB(0, 0, 0));
        gfx_text(&g, 10, 10, "FreaxOS", RGB(255, 255, 255));
        int painted = 0;
        for (int i = 0; i < 320 * 200; i++)
            if (fb[i])
                painted++;
        CHECK(painted > 30);
        CHECK(gfx_text_w("abc", 1) == 27);
        CHECK(gfx_text_w("abc", 2) == 54);

        /* blur : la zone devient lissée mais reste dans le cadre / blur */
        gfx_fill(&g, RGB(0, 0, 0));
        gfx_rect_f(&g, 0, 0, 8, 8, RGB(255, 255, 255));
        static u32 scratch[64 * 64];
        gfx_blur(&g, 0, 0, 32, 32, 2, scratch);
        u32 mid = gfx_get(&g, 6, 6);
        CHECK(GFX_R(mid) > 0 && GFX_R(mid) < 255);
        CHECK(gfx_get(&g, 60, 60) == RGB(0, 0, 0));

        /* arrondi + cercles ne débordent pas / rounded + circles clip */
        gfx_round_f(&g, 200, 100, 200, 100, 12, RGBA(255, 255, 255, 200));
        gfx_circle_f(&g, 10, 190, 30, RGB(0, 0, 255));
        gfx_gradient_v(&g, 0, 0, 50, 50, RGB(255, 0, 0), RGB(0, 0, 255));
        CHECK(gfx_get(&g, 319, 199) != 0);
        CHECK(GFX_A(gfx_get(&g, 319, 199)) == 255);
    }

    /* ---- html : parse, layout, liens / parse, layout, links ---- */
    {
        test_reset_kernel();
        const char *doc =
            "<h1>Titre</h1><p>Bonjour <b>Freax</b> <i>2026</i>.</p>"
            "<li>item un</li><li>item deux</li>"
            "<hr><p><a href=\"freax://freaxos.test/noyau\">noyau</a> "
            "&amp; &lt;tags&gt;</p>";
        struct html_doc *h = html_open(doc, 400);
        CHECK(h != NULL);
        CHECK(h->natom > 10);
        CHECK(h->height > 80);
        CHECK(h->nlink == 1);
        int links = 0;
        for (int i = 0; i < h->natom; i++)
            if (h->link[i] >= 0)
                links++;
        CHECK(links >= 1);
        const char *href = NULL;
        CHECK(html_hit_link(h, h->x[0], h->y[0], &href) == 0);
        /* trouve l'atome du lien / find the link atom */
        int li = -1;
        for (int i = 0; i < h->natom && li < 0; i++)
            if (h->link[i] >= 0)
                li = i;
        CHECK(li >= 0);
        CHECK(html_hit_link(h, h->x[li], h->y[li], &href) == 1);
        CHECK(href && strstr(href, "noyau") != NULL);
        html_close(h);

        /* entités + page introuvable / entities + missing page */
        struct html_doc *h2 = html_open("<p>a&amp;b &lt;x&gt;</p>", 300);
        int found_amp = 0;
        for (int i = 0; i < h2->natom; i++)
            if (strstr(h2->text[i], "a&b"))
                found_amp = 1;
        CHECK(found_amp);
        html_close(h2);
    }

    /* ---- conf : roundtrip VFS / persisted config roundtrip ---- */
    {
        test_reset_kernel();
        theme_init();
        wm_init();
        vfs_mkdir("/etc");
        CHECK(theme_set_wallpaper("mint") == 0);
        CHECK(theme_set_wallpaper("inconnu") == -1);
        theme_set_accent(RGB(0xF4, 0x3F, 0x6E));
        theme_set_glass(200);                     /* borné / clamped */
        CHECK(theme_get()->glass == 100);
        theme_set_glass(-5);
        CHECK(theme_get()->glass == 0);
        theme_set_glass(70);
        theme_set_lang(1);
        CHECK(strcmp(tr("oui", "yes"), "yes") == 0);
        theme_set_lang(0);
        CHECK(strcmp(tr("oui", "yes"), "oui") == 0);
        desklet_set("notes", 1);
        CHECK(desklet_enabled("notes") == 1);
        CHECK(desklet_enabled("clock") == 1);
        CHECK(desklet_enabled("sysmon") == 1);
        desklet_set("clock", 0);
        CHECK(desklet_enabled("clock") == 0);
        desklet_set("clock", 1);
        conf_save();
        CHECK(vfs_resolve("/etc/freaxdesktop.conf") != NULL);
        /* recharge : la conf doit restaurer / reload restores state */
        theme_set_wallpaper("aurora");
        theme_set_glass(55);
        theme_set_lang(0);
        desklet_set("notes", 0);
        conf_load();
        CHECK(strcmp(theme_get()->wallpaper, "mint") == 0);
        CHECK(theme_get()->glass == 70);
        CHECK(theme_get()->lang == 0);
        CHECK(desklet_enabled("notes") == 1);
        CHECK(desklet_enabled("clock") == 1);
    }

    /* ---- boutique : install/uninstall via VFS / store via VFS ---- */
    {
        test_reset_kernel();
        store_seed();
        CHECK(vfs_resolve("/repo/editor.app") != NULL);
        CHECK(vfs_resolve("/apps") != NULL);
        CHECK(store_catalog_count() == 5);
        CHECK(strcmp(store_catalog_id(0), "editor") == 0);
        CHECK(store_is_installed("editor") == 1);
        CHECK(store_is_installed("about") == 1);
        CHECK(store_installed_count() >= 5);
        CHECK(store_install("editor") == 0);      /* déjà là : ok / no-op */
        CHECK(store_uninstall("editor") == 0);
        CHECK(store_is_installed("editor") == 0);
        CHECK(vfs_resolve("/apps/editor.app") == NULL);
        CHECK(store_install("editor") == 0);
        CHECK(store_is_installed("editor") == 1);
        CHECK(vfs_resolve("/apps/editor.app") != NULL);
        int found_ed = 0;
        for (int i = 0; i < store_installed_count(); i++)
            if (!strcmp(store_installed_id(i), "editor"))
                found_ed = 1;
        CHECK(found_ed);
        /* visibilité du launcher / launcher visibility */
        CHECK(app_by_id("terminal") != NULL);
        CHECK(app_by_id("aucune") == NULL);
        CHECK(app_catalog_count() == 10);
        CHECK(app_catalog(0) && strcmp(app_catalog(0)->id, "terminal") == 0);
    }

    /* ---- fenêtres : ouverture, focus, fermeture / windows lifecycle ---- */
    {
        test_reset_kernel();
        wm_init();
        CHECK(wm_window_count() == 0);
        wm_open_app("terminal");
        CHECK(wm_window_count() == 1);
        CHECK(wm_focused() != NULL);
        wm_open_app("files");
        CHECK(wm_window_count() == 2);
        struct dwin *f = wm_focused();
        CHECK(f && strcmp(f->app->id, "files") == 0);
        wm_open_app("terminal");                  /* focus au lieu de doublon */
        CHECK(wm_window_count() == 2);
        f = wm_focused();
        CHECK(f && strcmp(f->app->id, "terminal") == 0);
        /* composition complète sans crash / full compose, no crash */
        wm_frame(&g, 12345);
        wm_toggle_launcher();
        CHECK(wm_launcher_open() == 1);
        wm_frame(&g, 12345);
        wm_toggle_launcher();
        CHECK(wm_launcher_open() == 0);
        /* interactions souris : launcher + fenêtre / pointer interactions */
        struct devt e;
        memset(&e, 0, sizeof e);
        e.type = DEV_DOWN;
        e.btn = 1;
        e.mx = 46;                                /* bouton launcher */
        e.my = DESK_H - 28;
        wm_input(&e, 1000);
        CHECK(wm_launcher_open() == 1);
        e.type = DEV_KEY;
        e.key = KEY_ESCAPE;
        wm_input(&e, 1000);
        CHECK(wm_launcher_open() == 0);
        /* drag par la barre de titre (fenêtre au-dessus) / focused drag */
        struct dwin *t = wm_focused();
        CHECK(t != NULL);
        int ox = t->x, oy = t->y;
        e.type = DEV_DOWN;
        e.mx = t->x + 10;
        e.my = t->y + 10;
        wm_input(&e, 1000);
        e.type = DEV_MOTION;
        e.mx = ox + 50;
        e.my = oy + 30;
        wm_input(&e, 1000);
        e.type = DEV_UP;
        wm_input(&e, 1000);
        CHECK(t->x == ox + 40 && t->y == oy + 20);   /* mx - off(10) */
        wm_close_win(t);
        CHECK(wm_window_count() == 1);
    }

    /* ---- navigateur : page interne / browser internal page ---- */
    {
        test_reset_kernel();
        wm_init();
        /* seed minimal comme desktop_task / minimal seed as desktop_task */
        vfs_mkdir("/www");
        struct vnode *f = vfs_create("/www/test.html");
        CHECK(f != NULL);
        const char *body = "<h1>Page test</h1><p>contenu</p>";
        vfs_write(f, body, (u32)strlen(body), 0);
        wm_open_app("browser");
        struct dwin *b = wm_focused();
        CHECK(b && strcmp(b->app->id, "browser") == 0);
        wm_frame(&g, 999);
        CHECK(wm_window_count() == 1);
        wm_close_win(b);
    }
}
