/*
 * FreaxOS 2026 - desktop/apps1.c
 * Applications du bureau (1/2) / Desktop apps (1/2).
 *
 * FR : Terminal (le vrai shell du noyau dans une fenêtre), Fichiers (le VFS
 *      du noyau), Éditeur (ramfs via VFS), Calculatrice, Moniteur système
 *      (/proc réel du noyau). Icônes vectorielles dessinées par gfx.
 * EN : Terminal (the real kernel shell in a window), Files (kernel VFS),
 *      Editor (ramfs through VFS), Calculator, System Monitor (real /proc).
 */
#include "desktop.h"
#include "freax/vfs.h"
#include "freax/shell.h"
#include "freax/printk.h"
#include "freax/hal.h"
#include "freax/sched.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static u32 TXT   = RGBA(235, 238, 245, 255);
static u32 DIM   = RGBA(150, 158, 175, 255);
static u32 ROW_A = RGBA(255, 255, 255, 14);

/* ================================================================== */
/* Icônes vectorielles / vector icons (dessinées dans s x s)           */
/* ================================================================== */

void icon_terminal(struct gfx *g, int x, int y, int s, u32 accent)
{
    gfx_round_f(g, x, y, s, s, 3, RGBA(12, 16, 26, 235));
    gfx_round_o(g, x, y, s, s, 3, RGBA(255, 255, 255, 60));
    gfx_text(g, x + s / 5, y + s / 4 - 3, ">_", accent);
}

void icon_files(struct gfx *g, int x, int y, int s, u32 accent)
{
    gfx_round_f(g, x, y + s / 4, s, s * 3 / 4, 3, RGBA(20, 26, 40, 235));
    gfx_round_f(g, x, y + s / 8, s / 2, s / 4, 2, RGBA(20, 26, 40, 235));
    gfx_round_o(g, x, y + s / 4, s, s * 3 / 4, 3, RGBA(255, 255, 255, 60));
    gfx_hline(g, x + s / 8, x + s - s / 8, y + s / 2, accent);
}

void icon_editor(struct gfx *g, int x, int y, int s, u32 accent)
{
    gfx_round_f(g, x + s / 8, y, s * 3 / 4, s, 2, RGBA(240, 243, 250, 240));
    for (int i = 0; i < 4; i++)
        gfx_hline(g, x + s / 4, x + s - s / 4, y + s / 5 + i * s / 5,
                  RGBA(30, 36, 52, 255));
    gfx_hline(g, x + s / 4, x + s / 2, y + s * 4 / 5, accent);
}

void icon_calc(struct gfx *g, int x, int y, int s, u32 accent)
{
    gfx_round_f(g, x + s / 8, y, s * 3 / 4, s, 3, RGBA(16, 20, 32, 235));
    gfx_round_o(g, x + s / 8, y, s * 3 / 4, s, 3, RGBA(255, 255, 255, 60));
    gfx_rect_f(g, x + s / 4, y + s / 8, s / 2, s / 5,
               RGBA(180, 230, 200, 220));
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++)
            gfx_px(g, x + s / 4 + c * s / 6 + 2, y + s / 2 + r * s / 5 + 2,
                   accent);
}

void icon_monitor(struct gfx *g, int x, int y, int s, u32 accent)
{
    int bh[] = { 3, 5, 4, 7 };
    for (int i = 0; i < 4; i++) {
        int bw = s / 6;
        gfx_rect_f(g, x + s / 8 + i * bw, y + s - 4 - bh[i] * s / 8, bw - 2,
                   bh[i] * s / 8, i == 3 ? accent : RGBA(120, 130, 155, 220));
    }
    gfx_hline(g, x + s / 8, x + s - s / 8, y + s - 3, RGBA(255, 255, 255, 90));
}

/* ================================================================== */
/* Terminal : le vrai shell freaxsh dans une fenêtre / real shell      */
/* ================================================================== */

#define TERM_BUF 8192
#define TERM_W   68          /* colonnes / columns (9 px par char) */
#define TERM_ROWS 18

struct term_state {
    char out[TERM_BUF];
    int  olen;
    char input[TERM_W + 2];
    int  ilen;
    char hist[8][TERM_W + 2];
    int  nhist, hidx;
};

static void term_append(struct term_state *t, const char *s)
{
    int slen = (int)strlen(s);
    if (t->olen + slen >= TERM_BUF - 2) {
        /* garde la fin / keep the tail */
        int keep = TERM_BUF / 2;
        memmove(t->out, t->out + t->olen - keep, (u32)keep);
        t->olen = keep;
        t->out[t->olen] = '\0';
    }
    memcpy(t->out + t->olen, s, (u32)slen);
    t->olen += slen;
    t->out[t->olen] = '\0';
}

void term_open(struct dwin *w)
{
    struct term_state *t = win_state_alloc(w, sizeof *t);
    if (!t)
        return;
    t->olen = 0;
    t->out[0] = '\0';
    snprintf(t->out, TERM_BUF,
             "freaxsh — %s\n%s\n",
             tr("le vrai shell du noyau FreaxOS",
                "the real FreaxOS kernel shell"),
             tr("Tapez 'help' pour les commandes. / Type 'help'.",
                "Type 'help' for commands."));
    t->olen = (int)strlen(t->out);
    t->ilen = 0;
    t->input[0] = '\0';
    t->nhist = 0;
    t->hidx = 0;
}

void term_draw(struct dwin *w, struct gfx *g, int x, int y,
                      int cw, int ch)
{
    struct term_state *t = win_state(w);
    if (!t)
        return;
    gfx_rect_f(g, x - 4, y - 2, cw + 8, ch + 6, RGBA(8, 10, 18, 220));
    /* dernières lignes qui tiennent / last fitting lines */
    char tmp[TERM_BUF];
    snprintf(tmp, TERM_BUF, "%s", t->out);
    int rows = ch / 16;
    int total = 1;
    for (char *p = tmp; *p; p++)
        if (*p == '\n')
            total++;
    char *lines[256];
    int nl = 0;
    lines[nl++] = tmp;
    for (char *p = tmp; *p && nl < 256; p++)
        if (*p == '\n') {
            *p = '\0';
            lines[nl++] = p + 1;
        }
    int start = nl - rows - 1;
    if (start < 0)
        start = 0;
    for (int i = start; i < nl - 1 && i - start < rows; i++) {
        int ly = y + (i - start) * 16;
        gfx_text(g, x, ly, lines[i], TXT);
    }
    /* ligne d'entrée / input line */
    int iy = y + ch - 20;
    char prompt[16] = "freax:/> ";
    gfx_text(g, x, iy, prompt, theme_get()->accent);
    gfx_text(g, x + gfx_text_w(prompt, 1), iy, t->input, TXT);
    gfx_text(g, x + gfx_text_w(prompt, 1) + gfx_text_w(t->input, 1), iy, "_",
             TXT);
}

int term_key(struct dwin *w, int key, const char *text)
{
    struct term_state *t = win_state(w);
    if (!t)
        return 0;
    if (key == KEY_RETURN) {
        term_append(t, "\nfreax:/> ");
        term_append(t, t->input);
        term_append(t, "\n");
        if (t->ilen > 0) {
            snprintf(t->hist[t->nhist % 8], TERM_W + 2, "%s", t->input);
            t->nhist++;
            t->hidx = t->nhist;
        }
        /* capture console -> exécution réelle du shell / capture -> exec */
        term_capture_begin(t->out + t->olen, TERM_BUF - t->olen - 1);
        char line[TERM_W + 2];
        snprintf(line, sizeof line, "%s", t->input);
        shell_exec_line(line);
        /* récupère la capture / collect capture */
        t->olen = (int)strlen(t->out);
        term_capture_begin(NULL, 0);
        term_append(t, t->out[t->olen - 1] == '\n' ? "" : "\n");
        t->ilen = 0;
        t->input[0] = '\0';
        return 1;
    }
    if (key == KEY_BACKSPACE) {
        if (t->ilen > 0)
            t->input[--t->ilen] = '\0';
        return 1;
    }
    if (key == KEY_UP) {
        if (t->nhist > 0) {
            t->hidx--;
            if (t->hidx < 0)
                t->hidx = 0;
            snprintf(t->input, sizeof t->input, "%s", t->hist[t->hidx % 8]);
            t->ilen = (int)strlen(t->input);
        }
        return 1;
    }
    if (key == KEY_DOWN) {
        if (t->nhist > 0) {
            t->hidx++;
            if (t->hidx > t->nhist)
                t->hidx = t->nhist;
            if (t->hidx == t->nhist) {
                t->input[0] = '\0';
                t->ilen = 0;
            } else {
                snprintf(t->input, sizeof t->input, "%s", t->hist[t->hidx % 8]);
                t->ilen = (int)strlen(t->input);
            }
        }
        return 1;
    }
    if (text && text[0] >= 32 && text[0] < 127 && t->ilen < TERM_W) {
        t->input[t->ilen++] = text[0];
        t->input[t->ilen] = '\0';
        return 1;
    }
    return 0;
}

/* ================================================================== */
/* Fichiers : navigateur du VFS noyau / kernel VFS browser             */
/* ================================================================== */

struct files_state {
    char path[224];
    char names[40][36];
    int  types[40];
    int  n;
    int  sel;
    int  scroll;
    int  refresh;
};

static int files_collect(const char *name, int type, void *ctx)
{
    struct files_state *f = ctx;
    if (f->n >= 40)
        return 0;
    snprintf(f->names[f->n], 36, "%s", name);
    f->types[f->n] = type;
    f->n++;
    return 1;
}

static void files_refresh(struct files_state *f)
{
    f->n = 0;
    struct vnode *d = vfs_resolve(f->path);
    if (d)
        vfs_list(d, files_collect, f);
    f->refresh = 0;
}

/* cible d'ouverture demandée par Fichiers / open target from Files */
char g_editor_open_path[224];

void files_open(struct dwin *w)
{
    struct files_state *f = win_state_alloc(w, sizeof *f);
    if (!f)
        return;
    snprintf(f->path, sizeof f->path, "/");
    f->sel = 0;
    f->scroll = 0;
    f->refresh = 1;
    files_refresh(f);
}

void files_draw(struct dwin *w, struct gfx *g, int x, int y,
                       int cw, int ch)
{
    struct files_state *f = win_state(w);
    if (!f)
        return;
    if (f->refresh)
        files_refresh(f);
    /* barre de chemin / path bar */
    gfx_round_f(g, x, y, cw, 26, 6, RGBA(255, 255, 255, 24));
    gfx_text(g, x + 10, y + 5, f->path, theme_get()->accent);
    int rows = (ch - 40) / 22;
    for (int i = f->scroll; i < f->n && i - f->scroll < rows; i++) {
        int ry = y + 34 + (i - f->scroll) * 22;
        if (i == f->sel)
            gfx_round_f(g, x, ry - 3, cw, 20, 4, ROW_A);
        int isdir = f->types[i] == VNODE_DIR;
        if (isdir) {
            icon_files(g, x + 4, ry, 14, theme_get()->accent);
            gfx_text(g, x + 26, ry, f->names[i], TXT);
        } else {
            icon_editor(g, x + 4, ry, 14, DIM);
            gfx_text(g, x + 26, ry, f->names[i], DIM);
        }
    }
    gfx_text(g, x, y + ch - 16,
             tr("clic : ouvrir — dossier remonte via ..",
                "click: open — '..' goes up"),
             RGBA(120, 128, 145, 255));
}

int files_pointer(struct dwin *w, int mx, int my, int btn,
                         int evt, int cw, int ch)
{
    (void)mx;
    (void)cw;
    struct files_state *f = win_state(w);
    if (!f)
        return 0;
    if (evt == 2) {                              /* molette / wheel */
        f->scroll += btn > 0 ? 1 : -1;
        int maxs = f->n - (ch - 40) / 22;
        if (f->scroll > maxs) f->scroll = maxs;
        if (f->scroll < 0) f->scroll = 0;
        return 1;
    }
    if (evt != 1 || btn != 1)
        return 0;
    int rows = (ch - 40) / 22;
    int idx = f->scroll + (my - 31) / 22;
    if (my < 31 || idx < 0 || idx >= f->n || idx - f->scroll >= rows)
        return 0;
    f->sel = idx;
    if (f->types[idx] == VNODE_DIR) {
        if (!strcmp(f->names[idx], "..")) {
            char *sl = strrchr(f->path, '/');
            if (sl && sl != f->path)
                *sl = '\0';
            else
                snprintf(f->path, sizeof f->path, "/");
        } else if (strcmp(f->path, "/")) {
            char np[448];
            snprintf(np, sizeof np, "%s/%s", f->path, f->names[idx]);
            snprintf(f->path, sizeof f->path, "%.*s", (int)sizeof(f->path) - 1, np);
        } else {
            char np[224];
            snprintf(np, sizeof np, "/%s", f->names[idx]);
            snprintf(f->path, sizeof f->path, "%.*s", (int)sizeof(f->path) - 1, np);
        }
        f->scroll = 0;
        f->sel = 0;
        f->refresh = 1;
        return 1;
    }
    /* fichier : ouvre dans l'éditeur / file: open in editor */
    {
        char np[448];
        snprintf(np, sizeof np, "%s/%s", f->path, f->names[idx]);
        snprintf(g_editor_open_path, sizeof g_editor_open_path, "%.*s", (int)sizeof(g_editor_open_path) - 1, np);
    }
    wm_open_app("editor");
    return 1;
}

/* ================================================================== */
/* Éditeur : fichiers ramfs via VFS / ramfs files through VFS          */
/* ================================================================== */

struct edit_state {
    char path[224];
    char text[4096];
    int  len;
    int  dirty;
    char status[256];
};

static void editor_load(struct edit_state *e, const char *path)
{
    snprintf(e->path, sizeof e->path, "%s", path);
    e->text[0] = '\0';
    e->len = 0;
    struct vnode *v = vfs_resolve(path);
    if (v && v->type == VNODE_FILE) {
        u32 pos = 0;
        int n = vfs_read(v, e->text, sizeof e->text - 1, &pos);
        if (n > 0)
            e->len = n;
        e->text[e->len] = '\0';
        snprintf(e->status, sizeof e->status, "%s", path);
    } else {
        snprintf(e->status, sizeof e->status,
                 tr("nouveau fichier / new file", "new file"));
    }
    e->dirty = 0;
}

static void editor_save(struct edit_state *e)
{
    struct vnode *v = vfs_resolve(e->path);
    if (!v) {
        vfs_create(e->path);
        v = vfs_resolve(e->path);
    }
    if (!v) {
        snprintf(e->status, sizeof e->status,
                 tr("echec / failed", "failed"));
        return;
    }
    vfs_truncate(v);
    vfs_write(v, e->text, (u32)e->len, 0);
    snprintf(e->status, sizeof e->status,
             tr("enregistré", "saved"));
    e->dirty = 0;
}

void editor_open(struct dwin *w)
{
    struct edit_state *e = win_state_alloc(w, sizeof *e);
    if (!e)
        return;
    editor_load(e, g_editor_open_path[0] ? g_editor_open_path
                                         : "/home/notes.txt");
    g_editor_open_path[0] = '\0';
}

void editor_draw(struct dwin *w, struct gfx *g, int x, int y,
                        int cw, int ch)
{
    struct edit_state *e = win_state(w);
    if (!e)
        return;
    gfx_rect_f(g, x - 4, y - 2, cw + 8, ch - 26, RGBA(10, 13, 22, 210));
    /* texte sans wrap, tronqué par colonnes / text clipped per line */
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", e->text);
    int rows = (ch - 26) / 16;
    int cols = cw / 9;
    char *lines[256];
    int nl = 0;
    lines[nl++] = tmp;
    for (char *p = tmp; *p && nl < 256; p++)
        if (*p == '\n') {
            *p = '\0';
            lines[nl++] = p + 1;
        }
    for (int i = 0; i < nl && i < rows; i++) {
        char cell[112];
        int cl = (int)strlen(lines[i]);
        if (cl > cols)
            cl = cols;
        memcpy(cell, lines[i], (u32)cl);
        cell[cl] = '\0';
        gfx_text(g, x, y + i * 16, cell, TXT);
    }
    /* barre de boutons / button bar */
    int by = y + ch - 22;
    gfx_round_f(g, x, by, 110, 22, 6, RGBA(255, 255, 255, 26));
    gfx_text(g, x + 12, by + 4,
             e->dirty ? tr("Enregistrer *", "Save *") : tr("Enregistrer",
                                                           "Save"),
             theme_get()->accent);
    gfx_round_f(g, x + 120, by, 110, 22, 6, RGBA(255, 255, 255, 26));
    gfx_text(g, x + 132, by + 4, tr("Effacer", "Clear"), DIM);
    gfx_text(g, x + 244, by + 4, e->status, RGBA(120, 128, 145, 255));
}

int editor_pointer(struct dwin *w, int mx, int my, int btn,
                          int evt, int cw, int ch)
{
    (void)cw;
    struct edit_state *e = win_state(w);
    if (!e || evt != 1 || btn != 1)
        return 0;
    int by = ch - 22;
    if (my < by || my > by + 22)
        return 0;
    if (mx >= 0 && mx < 110) {
        editor_save(e);
        snprintf(e->status + strlen(e->status),
                 sizeof e->status - strlen(e->status), " — %s", e->path);
        return 1;
    }
    if (mx >= 120 && mx < 230) {
        e->len = 0;
        e->text[0] = '\0';
        e->dirty = 1;
        snprintf(e->status, sizeof e->status,
                 tr("vide / cleared", "cleared"));
        return 1;
    }
    return 0;
}

int editor_key(struct dwin *w, int key, const char *text)
{
    struct edit_state *e = win_state(w);
    if (!e)
        return 0;
    if (key == KEY_BACKSPACE) {
        if (e->len > 0)
            e->text[--e->len] = '\0';
        e->dirty = 1;
        return 1;
    }
    if (key == KEY_RETURN) {
        if (e->len < (int)sizeof e->text - 2) {
            e->text[e->len++] = '\n';
            e->text[e->len] = '\0';
            e->dirty = 1;
        }
        return 1;
    }
    if (text && text[0] >= 32 && e->len < (int)sizeof e->text - 2) {
        e->text[e->len++] = text[0];
        e->text[e->len] = '\0';
        e->dirty = 1;
        return 1;
    }
    return 0;
}

/* ================================================================== */
/* Calculatrice / Calculator                                           */
/* ================================================================== */

struct calc_state {
    char disp[24];
    long acc;
    char op;
    int  fresh;
};

void calc_open(struct dwin *w)
{
    struct calc_state *c = win_state_alloc(w, sizeof *c);
    if (!c)
        return;
    snprintf(c->disp, sizeof c->disp, "0");
    c->acc = 0;
    c->op = 0;
    c->fresh = 1;
}

static void calc_press(struct calc_state *c, const char *k)
{
    if (!strcmp(k, "C")) {
        snprintf(c->disp, sizeof c->disp, "0");
        c->acc = 0;
        c->op = 0;
        c->fresh = 1;
        return;
    }
    if (!strcmp(k, "+") || !strcmp(k, "-") || !strcmp(k, "*") ||
        !strcmp(k, "/")) {
        c->acc = strtol(c->disp, NULL, 10);
        c->op = k[0];
        c->fresh = 1;
        return;
    }
    if (!strcmp(k, "=")) {
        long v = strtol(c->disp, NULL, 10);
        char r[24];
        if (c->op == '+') snprintf(r, sizeof r, "%ld", c->acc + v);
        else if (c->op == '/') {
            if (v)
                snprintf(r, sizeof r, "%ld", c->acc / v);
            else
                snprintf(r, sizeof r, "%s", tr("erreur", "error"));
        }
        else
            snprintf(r, sizeof r, "%s", c->disp);
        snprintf(c->disp, sizeof c->disp, "%s", r);
        c->op = 0;
        c->fresh = 1;
        return;
    }
    /* chiffre / digit */
    if (c->fresh || !strcmp(c->disp, "0")) {
        snprintf(c->disp, sizeof c->disp, "%s", k);
        c->fresh = 0;
    } else if (strlen(c->disp) < 12) {
        snprintf(c->disp + strlen(c->disp),
                 sizeof c->disp - strlen(c->disp), "%s", k);
    }
}

static const char *CALC_KEYS[5][4] = {
    { "C", "(", ")", "/" },
    { "7", "8", "9", "*" },
    { "4", "5", "6", "-" },
    { "1", "2", "3", "+" },
    { "0", "00", ".", "=" },
};

void calc_draw(struct dwin *w, struct gfx *g, int x, int y,
               int cw, int ch)
{
    (void)ch;
    struct calc_state *c = win_state(w);
    if (!c)
        return;
    gfx_round_f(g, x, y, cw, 44, 8, RGBA(8, 10, 18, 230));
    char d[24];
    snprintf(d, sizeof d, "%s", c->disp);
    int dw = gfx_text_w(d, 2);
    gfx_text_scale(g, x + cw - dw - 16, y + 8, d, TXT, 2);
    for (int r = 0; r < 5; r++)
        for (int cidx = 0; cidx < 4; cidx++) {
            int bx = x + cidx * (cw / 4), by = y + 56 + r * 52;
            int bw = cw / 4 - 8;
            int is_op = r == 0 || cidx == 3;
            int is_eq = !strcmp(CALC_KEYS[r][cidx], "=");
            u32 bg = is_eq ? theme_get()->accent
                           : RGBA(is_op ? 60 : 30, is_op ? 70 : 34,
                                  is_op ? 95 : 48, 220);
            gfx_round_f(g, bx, by, bw, 44, 10, bg);
            gfx_round_o(g, bx, by, bw, 44, 10, RGBA(255, 255, 255, 40));
            int tw = gfx_text_w(CALC_KEYS[r][cidx], 1);
            gfx_text(g, bx + (bw - tw) / 2, by + 14, CALC_KEYS[r][cidx],
                     is_eq ? RGBA(10, 12, 20, 255) : TXT);
        }
}

int calc_pointer(struct dwin *w, int mx, int my, int btn,
                 int evt, int cw, int ch)
{
    (void)ch;
    struct calc_state *c = win_state(w);
    if (!c || evt != 1 || btn != 1)
        return 0;
    if (my < 56)
        return 0;
    int r = (my - 56) / 52;
    int cidx = mx / (cw / 4);
    if (r < 0 || r > 4 || cidx < 0 || cidx > 3)
        return 0;
    if (r == 4 && cidx == 2)
        return 0;                                /* "." ignoré / ignored */
    if (r == 0 && cidx == 1)
        return 0;                                /* "(" ignoré / ignored */
    if (r == 0 && cidx == 2)
        return 0;                                /* ")" ignoré / ignored */
    if (r == 4 && cidx == 1)
        return 0;                                /* "00" ignoré / ignored */
    calc_press(c, CALC_KEYS[r][cidx]);
    return 1;
}

int calc_key(struct dwin *w, int key, const char *text)
{
    struct calc_state *c = win_state(w);
    if (!c)
        return 0;
    if (key == KEY_RETURN) { calc_press(c, "="); return 1; }
    if (key == KEY_ESCAPE) { calc_press(c, "C"); return 1; }
    if (!text || !text[0])
        return 0;
    char k[2] = { text[0], 0 };
    if (strchr("0123456789+-*/", k[0])) {
        calc_press(c, k);
        return 1;
    }
    return 0;
}

/* ================================================================== */
/* Moniteur système / System monitor (real /proc)                      */
/* ================================================================== */

struct mon_state {
    u32  last;
    char mem[400];
    char tasks[600];
    char up[64];
    char log[500];
};

static void proc_copy(const char *path, char *out, int cap)
{
    struct vnode *v = vfs_resolve(path);
    out[0] = '\0';
    if (!v)
        return;
    u32 pos = 0;
    int n = vfs_read(v, out, cap - 1, &pos);
    if (n < 0)
        n = 0;
    out[n] = '\0';
}

static void mon_refresh(struct mon_state *m)
{
    proc_copy("/proc/meminfo", m->mem, (int)sizeof m->mem);
    proc_copy("/proc/tasks", m->tasks, (int)sizeof m->tasks);
    proc_copy("/proc/uptime", m->up, (int)sizeof m->up);
    char ring[400];
    int n = klog_read(ring, (int)sizeof ring - 1);
    if (n < 0)
        n = 0;
    ring[n] = '\0';
    /* fin du journal / tail of the log */
    char *tail = ring;
    int len = n;
    if (len > 200) {
        tail = ring + len - 200;
        while (*tail && *tail != '\n')
            tail++;
        if (*tail)
            tail++;
    }
    snprintf(m->log, sizeof m->log, "%s", tail);
    m->last = g_hal->ticks();
}

void mon_open(struct dwin *w)
{
    struct mon_state *m = win_state_alloc(w, sizeof *m);
    if (!m)
        return;
    memset(m, 0, sizeof *m);
    mon_refresh(m);
}

void mon_draw(struct dwin *w, struct gfx *g, int x, int y,
                     int cw, int ch)
{
    struct mon_state *m = win_state(w);
    if (!m)
        return;
    if (g_hal->ticks() - m->last >= 100)
        mon_refresh(m);
    int used = 0, total = 1;
    char *p = strstr(m->mem, "HeapUsed");
    char *q = strstr(m->mem, "HeapTotal");
    if (p) sscanf(p, "HeapUsed : %d", &used);
    if (q) sscanf(q, "HeapTotal : %d", &total);
    if (total < 1) total = 1;
    gfx_text(g, x, y, tr("Memoire du noyau / kernel heap", "Kernel heap"),
             DIM);
    gfx_round_f(g, x, y + 20, cw, 12, 6, RGBA(255, 255, 255, 40));
    int bw = cw * used / total;
    if (bw > 0)
        gfx_round_f(g, x, y + 20, bw, 12, 6, theme_get()->accent);
    char line[80];
    snprintf(line, sizeof line, "%d / %d octets (%d%%)", used, total,
             used * 100 / total);
    gfx_text(g, x, y + 38, line, TXT);
    /* tâches / tasks */
    int ty = y + 66;
    gfx_text(g, x, ty, tr("Taches / tasks", "Tasks"), DIM);
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", m->tasks);
    char *lines[16];
    int nl = 0;
    lines[nl++] = tmp;
    for (char *lp = tmp; *lp && nl < 16; lp++)
        if (*lp == '\n') {
            *lp = '\0';
            lines[nl++] = lp + 1;
        }
    for (int i = 0; i < nl && i < 7; i++)
        gfx_text(g, x + 8, ty + 18 + i * 15, lines[i], TXT);
    /* journal / log tail */
    int ly = y + ch - 84;
    gfx_text(g, x, ly, tr("Journal noyau / kernel log", "Kernel log"), DIM);
    char ltmp[500];
    snprintf(ltmp, sizeof ltmp, "%s", m->log);
    char *llines[4];
    int lnl = 0;
    llines[lnl++] = ltmp;
    for (char *lp = ltmp; *lp && lnl < 4; lp++)
        if (*lp == '\n') {
            *lp = '\0';
            llines[lnl++] = lp + 1;
        }
    for (int i = 0; i < lnl && i < 3; i++)
        gfx_text(g, x + 8, ly + 18 + i * 15, llines[i],
                 RGBA(140, 200, 170, 255));
}

/* enregistrement : voir la table des apps dans desktop/apps2.c
   (registration: see the app table in desktop/apps2.c) */
