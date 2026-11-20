/*
 * FreaxOS 2026 - desktop/html.c
 * Mini moteur HTML / Tiny HTML engine.
 *
 * FR : parse le sous-ensemble HTML, découpe en mots, place chaque mot (atome)
 *      sur une grille de mise en page, puis dessine. Les liens sont indexés
 *      pour la zone cliquable. Pensé pour les pages internes du ramfs /www.
 * EN : parses the HTML subset, splits into words, lays out each word (atom),
 *      then draws. Links are indexed for hit-testing. Meant for internal
 *      ramfs /www pages.
 */
#include "html.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define FL_BOLD 1
#define FL_ITAL 2
#define FL_HR   4
#define FL_PRE  8

struct parser {
    const char *src;
    int pos;
    /* état style courant / current style state */
    int size, bold, italic, in_pre, in_li, link, skip;
    int br;                                  /* saut de bloc / block break */
};

/* entités / entities */
static void entity(const char *s, int *len, char *out, int cap)
{
    /* s pointe sur '&' ; retourne le remplacement / returns replacement */
    (void)len;
    if (strncmp(s, "&amp;", 5) == 0)      { snprintf(out, cap, "&"); return; }
    if (strncmp(s, "&lt;", 4) == 0)       { snprintf(out, cap, "<"); return; }
    if (strncmp(s, "&gt;", 4) == 0)       { snprintf(out, cap, ">"); return; }
    if (strncmp(s, "&quot;", 6) == 0)     { snprintf(out, cap, "\""); return; }
    if (strncmp(s, "&nbsp;", 6) == 0)     { snprintf(out, cap, " "); return; }
    snprintf(out, cap, "&");
}

static int lower_cmp(const char *tag, const char *name)
{
    while (*name) {
        if (tolower((unsigned char)*tag) != tolower((unsigned char)*name))
            return 0;
        tag++; name++;
    }
    return 1;
}

/* lit le nom du tag et attr href si <a / read tag name + href attr for <a */
static void parse_tag(struct parser *p, const char *tag)
{
    if (tag[0] == '/') {                       /* fermeture / closing */
        if (lower_cmp(tag + 1, "a"))  p->link = -1;
        if (lower_cmp(tag + 1, "pre")) p->in_pre = 0;
        return;
    }
    if (lower_cmp(tag, "h1")) p->size = 3, p->bold = 1, p->br = 1;
    else if (lower_cmp(tag, "/h1")) p->size = 1, p->bold = 0, p->br = 1;
    else if (lower_cmp(tag, "h2")) p->size = 2, p->bold = 1, p->br = 1;
    else if (lower_cmp(tag, "/h2")) p->size = 1, p->bold = 0, p->br = 1;
    else if (lower_cmp(tag, "h3")) p->size = 2, p->bold = 1, p->br = 1;
    else if (lower_cmp(tag, "/h3")) p->size = 1, p->bold = 0, p->br = 1;
    else if (lower_cmp(tag, "p") || lower_cmp(tag, "p/")) p->br = 1;
    else if (lower_cmp(tag, "/p")) p->br = 1;
    else if (lower_cmp(tag, "br") || lower_cmp(tag, "br/")) p->br = 1;
    else if (lower_cmp(tag, "b"))  p->bold = 1;
    else if (lower_cmp(tag, "/b")) p->bold = 0;
    else if (lower_cmp(tag, "strong")) p->bold = 1;
    else if (lower_cmp(tag, "/strong")) p->bold = 0;
    else if (lower_cmp(tag, "i"))  p->italic = 1;
    else if (lower_cmp(tag, "/i")) p->italic = 0;
    else if (lower_cmp(tag, "pre")) p->in_pre = 1;
    else if (lower_cmp(tag, "li")) p->in_li = 1, p->br = 1;
    else if (lower_cmp(tag, "/li")) p->in_li = 0, p->br = 1;
    else if (lower_cmp(tag, "hr") || lower_cmp(tag, "hr/"))
        p->skip = 2;                            /* rappel barre / hr marker */
    /* <a href : extraction faite dans html_open / extraction done in html_open */
}

/* moteur / engine */
struct html_doc *html_open(const char *src, int width)
{
    struct html_doc *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->width = width;
    struct parser p = { src, 0, 1, 0, 0, 0, 0, -1, 0, 0 };
    int cx = 0, cy = 0;
    int pending_li = 0;

    while (p.src[p.pos]) {
        char c = p.src[p.pos];

        if (c == '<') {                          /* tag */
            char tag[96];
            int n = 0;
            p.pos++;
            while (p.src[p.pos] && p.src[p.pos] != '>' && n < 95)
                tag[n++] = p.src[p.pos++];
            tag[n] = '\0';
            if (p.src[p.pos] == '>')
                p.pos++;
            /* <a href="..."> : extraire la cible / extract the target */
            if ((tag[0] == 'a' || tag[0] == 'A') &&
                (tag[1] == ' ' || tag[1] == '\0')) {
                char *h = strstr(tag, "href");
                if (h && d->nlink < HTML_MAX_LINKS) {
                    h += 4;
                    while (*h == ' ' || *h == '=')
                        h++;
                    if (*h == '"')
                        h++;
                    snprintf(d->href[d->nlink], 88, "%s", h);
                    char *q = strchr(d->href[d->nlink], '"');
                    if (q)
                        *q = '\0';
                    p.link = d->nlink++;
                }
            }
            parse_tag(&p, tag);
            if (p.br) {
                if (cx > 0) {
                    cx = 0;
                    cy += 16 * p.size + (p.size > 1 ? 6 : 4);
                }
                p.br = 0;
            }
            if (p.skip == 2 && d->natom < HTML_MAX_ATOMS) {
                d->flags[d->natom] = FL_HR;
                d->x[d->natom] = 0;
                d->y[d->natom] = (short)cy;
                d->size[d->natom] = 1;
                d->link[d->natom] = -1;
                d->natom++;
                cy += 18;
                p.skip = 0;
            }
            continue;
        }

        if (!p.in_pre && (c == '\n' || c == '\r')) {
            if (c == '\r' && p.src[p.pos + 1] == '\n')
                p.pos++;
            cx = 0;
            cy += 16 * p.size + (p.size > 1 ? 6 : 4);
            p.pos++;
            continue;
        }
        if (p.in_pre && c == '\n') {
            cx = 0;
            cy += 16;
            p.pos++;
            continue;
        }
        if (!p.in_pre && c == ' ') {
            p.pos++;
            continue;                            /* mots séparés / words split */
        }
        if (!p.in_pre && c == '\t') { p.pos++; continue; }

        /* lire un mot / read one word (avec entités) */
        char word[56];
        int n = 0;
        while (p.src[p.pos] && n < 55) {
            char ch = p.src[p.pos];
            if (ch == '<') {
                if (p.src[p.pos + 1] == '/') break;
                if (p.in_pre) break;
                /* dans le texte normal, un '<' non-tag finit le mot */
                break;
            }
            if (!p.in_pre && (ch == ' ' || ch == '\n' || ch == '\r' ||
                              ch == '\t'))
                break;
            if (ch == '&') {
                char ent[8];
                entity(p.src + p.pos, NULL, ent, sizeof ent);
                int el = (int)strlen(ent);
                for (int i = 0; i < el && n < 54; i++)
                    word[n++] = ent[i];
                /* sauter l'entité source / skip source entity */
                const char *semi = strchr(p.src + p.pos, ';');
                if (semi && semi - (p.src + p.pos) < 8)
                    p.pos = (int)(semi - p.src) + 1;
                else
                    p.pos++;
                continue;
            }
            word[n++] = p.src[p.pos++];
        }
        word[n] = '\0';
        if (n == 0) {
            if (p.src[p.pos])
                p.pos++;
            continue;
        }

        if (pending_li) {
            pending_li = 0;
        }
        if (p.in_li && d->natom < HTML_MAX_ATOMS - 1 && cx == 0) {
            /* puce / bullet */
            d->text[d->natom][0] = 0;
            snprintf(d->text[d->natom], 56, "%s", "\x07");
            d->x[d->natom] = 0;
            d->y[d->natom] = (short)cy;
            d->size[d->natom] = (u8)p.size;
            d->flags[d->natom] = 0;
            d->link[d->natom] = -1;
            d->natom++;
            cx = 20;
        }

        if (d->natom >= HTML_MAX_ATOMS - 1) {
            d->truncated = 1;
            break;
        }

        int wsc = p.size >= 3 ? 2 : 1;           /* titres en échelle 2 */
        int wpx = gfx_text_w(word, wsc);
        if (!p.in_pre && cx + wpx > width - 8 && cx > 0) {
            cx = 0;
            cy += 16 * p.size + (p.size > 1 ? 6 : 4);
        }
        snprintf(d->text[d->natom], 56, "%s", word);
        d->x[d->natom] = (short)cx;
        d->y[d->natom] = (short)cy;
        d->size[d->natom] = (u8)p.size;
        d->flags[d->natom] = (u8)((p.bold ? FL_BOLD : 0) |
                                  (p.italic ? FL_ITAL : 0) |
                                  (p.in_pre ? FL_PRE : 0));
        d->link[d->natom] = (short)p.link;
        d->natom++;
        cx += wpx + (p.in_pre ? 0 : 6);
    }

    d->height = cy + 24;
    return d;
}

void html_close(struct html_doc *d)
{
    free(d);
}

static void draw_atom(struct gfx *g, struct html_doc *d, int i, int bx, int by,
                      int view_h, int scroll_y, u32 accent)
{
    int ay = d->y[i] - scroll_y;
    if (ay + 20 < 0 || ay > view_h)
        return;
    int ax = d->x[i] + bx;
    if (d->flags[i] & FL_HR) {
        gfx_hline(g, bx + 4, bx + d->width - 4, by + ay + 8,
                  RGBA(255, 255, 255, 60));
        return;
    }
    if (d->text[i][0] == '\x07') {               /* puce / bullet */
        gfx_circle_f(g, ax + 4, by + ay + 8, 3, accent);
        return;
    }
    int size = d->size[i];
    int sc = size >= 3 ? 2 : 1;
    u32 col = d->link[i] >= 0 ? accent : RGBA(235, 238, 245, 255);
    if (d->flags[i] & FL_ITAL) {
        /* italique simulé : double trace / fake italic */
        gfx_text_scale(g, ax + 1, by + ay, d->text[i], col, sc);
        gfx_text_scale(g, ax, by + ay, d->text[i], col, sc);
        return;
    }
    gfx_text_scale(g, ax, by + ay, d->text[i], col, sc);
    if (d->flags[i] & FL_BOLD || size > 1)
        gfx_text_scale(g, ax + 1, by + ay, d->text[i], col, sc);
    if (d->link[i] >= 0)
        gfx_hline(g, ax, ax + gfx_text_w(d->text[i], sc) - 2,
                  by + ay + 15 * size, accent);
}

void html_draw(struct gfx *g, struct html_doc *d, int x, int y,
               int view_h, int scroll_y, u32 accent)
{
    for (int i = 0; i < d->natom; i++)
        draw_atom(g, d, i, x, y, view_h, scroll_y, accent);
    /* scrollbar minimale / minimal scrollbar */
    if (d->height > view_h) {
        int track = view_h - 8;
        int thumb = track * view_h / d->height;
        if (thumb < 24) thumb = 24;
        int tmax = track - thumb;
        int ty = 4 + tmax * scroll_y / (d->height - view_h);
        gfx_round_f(g, x + d->width - 6, y + ty, 4, thumb, 2,
                    RGBA(255, 255, 255, 70));
    }
}

static int atom_at(struct html_doc *d, int i, int px, int py)
{
    if (d->link[i] < 0 || d->flags[i] & FL_HR || d->text[i][0] == '\x07')
        return 0;
    int w = gfx_text_w(d->text[i], 1);
    int h = 16 * d->size[i];
    return px >= d->x[i] && px <= d->x[i] + w &&
           py >= d->y[i] && py <= d->y[i] + h;
}

int html_link_at(struct html_doc *d, int px, int py)
{
    for (int i = 0; i < d->natom; i++)
        if (atom_at(d, i, px, py))
            return d->link[i];
    return -1;
}

const char *html_link_href(struct html_doc *d, int i)
{
    if (i < 0 || i >= d->nlink)
        return NULL;
    return d->href[i];
}

int html_hit_link(struct html_doc *d, int px, int py, const char **href)
{
    int li = html_link_at(d, px, py);
    if (li < 0)
        return 0;
    if (href)
        *href = html_link_href(d, li);
    return 1;
}
