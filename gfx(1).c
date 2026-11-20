/*
 * FreaxOS 2026 - desktop/gfx.c
 * Rendu 2D logiciel / Software 2D renderer.
 *
 * FR : toutes les primitives dessinent avec clipping. Le texte décode l'UTF-8
 *      court (1 ou 2 octets) pour les accents. Le flou est un box blur séparable.
 * EN : all primitives clip to the framebuffer. Text decodes short UTF-8
 *      (1 or 2 bytes) for accents. Blur is a separable box blur.
 */
#include "gfx.h"

/* polices générées / generated font */
int font_lookup(int cp, const unsigned short **rows);

/* sqrt entier, défini plus bas / integer sqrt, defined below */
int sqrt_int(int v);
static u32 blend(u32 dst, u32 src);
static u32 blend(u32 dst, u32 src)
{
    u32 a = GFX_A(src);
    if (a == 255)
        return src | 0xFF000000u;
    if (a == 0)
        return dst;
    u32 dr = GFX_R(dst), dg = GFX_G(dst), db = GFX_B(dst);
    u32 sr = GFX_R(src), sg = GFX_G(src), sb = GFX_B(src);
    u32 r = (sr * a + dr * (255 - a)) / 255;
    u32 gg = (sg * a + dg * (255 - a)) / 255;
    u32 b = (sb * a + db * (255 - a)) / 255;
    return RGBA((u8)r, (u8)gg, (u8)b, 255);
}

static int g_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void gfx_init(struct gfx *g, u32 *fb, int w, int h)
{
    g->fb = fb;
    g->w = w;
    g->h = h;
}

static inline void put_raw(struct gfx *g, int x, int y, u32 c)
{
    g->fb[(u32)y * (u32)g->w + (u32)x] = c;
}

void gfx_px(struct gfx *g, int x, int y, u32 c)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h)
        return;
    put_raw(g, x, y, GFX_A(c) < 255 ? blend(gfx_get(g, x, y), c) : c);
}

u32 gfx_get(struct gfx *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h)
        return 0;
    return g->fb[(u32)y * (u32)g->w + (u32)x];
}

void gfx_fill(struct gfx *g, u32 c)
{
    u32 n = (u32)g->w * (u32)g->h;
    for (u32 i = 0; i < n; i++)
        g->fb[i] = c;
}

void gfx_rect_f(struct gfx *g, int x, int y, int w, int h, u32 c)
{
    int x0 = g_clamp(x, 0, g->w - 1), x1 = g_clamp(x + w - 1, -1, g->w - 1);
    int y0 = g_clamp(y, 0, g->h - 1), y1 = g_clamp(y + h - 1, -1, g->h - 1);
    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++)
            put_raw(g, xx, yy, c);
}

void gfx_rect_o(struct gfx *g, int x, int y, int w, int h, u32 c)
{
    gfx_hline(g, x, x + w - 1, y, c);
    gfx_hline(g, x, x + w - 1, y + h - 1, c);
    gfx_vline(g, x, y, y + h - 1, c);
    gfx_vline(g, x + w - 1, y, y + h - 1, c);
}

void gfx_hline(struct gfx *g, int x0, int x1, int y, u32 c)
{
    if (y < 0 || y >= g->h)
        return;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    x0 = g_clamp(x0, 0, g->w - 1);
    x1 = g_clamp(x1, 0, g->w - 1);
    for (int x = x0; x <= x1; x++)
        put_raw(g, x, y, GFX_A(c) < 255 ? blend(gfx_get(g, x, y), c) : c);
}

void gfx_vline(struct gfx *g, int x, int y0, int y1, u32 c)
{
    if (x < 0 || x >= g->w)
        return;
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    y0 = g_clamp(y0, 0, g->h - 1);
    y1 = g_clamp(y1, 0, g->h - 1);
    for (int y = y0; y <= y1; y++)
        put_raw(g, x, y, GFX_A(c) < 255 ? blend(gfx_get(g, x, y), c) : c);
}

void gfx_line(struct gfx *g, int x0, int y0, int x1, int y1, u32 c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        gfx_px(g, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle_f(struct gfx *g, int cx, int cy, int r, u32 c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)(r * r - dy * dy);
        if (dx < 0)
            continue;
        dx = (int)sqrt_int(dx);
        gfx_hline(g, cx - dx, cx + dx, cy + dy, c);
    }
}

void gfx_circle_o(struct gfx *g, int cx, int cy, int r, u32 c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_px(g, cx + x, cy + y, c); gfx_px(g, cx - x, cy + y, c);
        gfx_px(g, cx + x, cy - y, c); gfx_px(g, cx - x, cy - y, c);
        gfx_px(g, cx + y, cy + x, c); gfx_px(g, cx - y, cy + x, c);
        gfx_px(g, cx + y, cy - x, c); gfx_px(g, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else        { x--; err += 2 * (y - x) + 1; }
    }
}

/* sqrt entier (pas de libm en freestanding) / integer sqrt */
int sqrt_int(int v)
{
    if (v <= 0)
        return 0;
    int r = v, nr;
    do {
        nr = (r + v / r) / 2;
        if (nr >= r)
            break;
        r = nr;
    } while (1);
    return r;
}

void gfx_blend_rect(struct gfx *g, int x, int y, int w, int h, u32 c)
{
    int x0 = g_clamp(x, 0, g->w - 1), x1 = g_clamp(x + w - 1, -1, g->w - 1);
    int y0 = g_clamp(y, 0, g->h - 1), y1 = g_clamp(y + h - 1, -1, g->h - 1);
    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++)
            put_raw(g, xx, yy, blend(gfx_get(g, xx, yy), c));
}

static void blend_span(struct gfx *g, int x, int y, int w, u32 c, int ry)
{
    int x0 = g_clamp(x, 0, g->w - 1), x1 = g_clamp(x + w - 1, -1, g->w - 1);
    for (int xx = x0; xx <= x1; xx++)
        put_raw(g, xx, y, blend(gfx_get(g, xx, y),
                                RGBA(GFX_R(c), GFX_G(c), GFX_B(c),
                                     (u8)g_clamp(GFX_A(c) - ry * 6, 0, 255))));
}

void gfx_round_f(struct gfx *g, int x, int y, int w, int h, int r, u32 c)
{
    if (r < 1) { gfx_blend_rect(g, x, y, w, h, c); return; }
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    for (int yy = 0; yy < h; yy++) {
        int ry = 0;
        if (yy < r)        ry = r - yy;
        else if (yy >= h - r) ry = yy - (h - r - 1);
        int inset = r - sqrt_int(r * r - ry * ry);
        blend_span(g, x + inset, y + yy, w - 2 * inset, c, ry);
    }
}

void gfx_round_o(struct gfx *g, int x, int y, int w, int h, int r, u32 c)
{
    if (r < 1) { gfx_rect_o(g, x, y, w, h, c); return; }
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    for (int i = 0; i < r; i++) {
        int d = r - sqrt_int(r * r - (r - i) * (r - i));
        int ry = r - i;
        /* bord haut/bas arrondi / rounded top & bottom edges */
        gfx_px(g, x + i + d, y + r - ry, c);
        gfx_px(g, x + w - 1 - i - d, y + r - ry, c);
        gfx_px(g, x + i + d, y + h - 1 - (r - ry), c);
        gfx_px(g, x + w - 1 - i - d, y + h - 1 - (r - ry), c);
    }
    gfx_hline(g, x + r, x + w - 1 - r, y, c);
    gfx_hline(g, x + r, x + w - 1 - r, y + h - 1, c);
    gfx_vline(g, x, y + r, y + h - 1 - r, c);
    gfx_vline(g, x + w - 1, y + r, y + h - 1 - r, c);
}

void gfx_blur(struct gfx *g, int x, int y, int w, int h, int radius,
              u32 *scratch)
{
    if (w <= 0 || h <= 0 || radius <= 0)
        return;
    x = g_clamp(x, 0, g->w);
    y = g_clamp(y, 0, g->h);
    if (x + w > g->w) w = g->w - x;
    if (y + h > g->h) h = g->h - y;
    if (w <= 0 || h <= 0)
        return;
    /* passe horizontale vers scratch, puis verticale retour / H then V */
    for (int yy = 0; yy < h; yy++) {
        u32 *row = scratch + (u32)yy * (u32)w;
        for (int xx = 0; xx < w; xx++) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = xx + k;
                if (sx < 0 || sx >= w)
                    continue;
                u32 c = g->fb[(u32)(y + yy) * (u32)g->w + (u32)(x + sx)];
                sr += GFX_R(c); sg += GFX_G(c); sb += GFX_B(c); n++;
            }
            row[xx] = RGBA((u8)(sr / n), (u8)(sg / n), (u8)(sb / n), 255);
        }
    }
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = yy + k;
                if (sy < 0 || sy >= h)
                    continue;
                u32 c = scratch[(u32)sy * (u32)w + (u32)xx];
                sr += GFX_R(c); sg += GFX_G(c); sb += GFX_B(c); n++;
            }
            g->fb[(u32)(y + yy) * (u32)g->w + (u32)(x + xx)] =
                RGBA((u8)(sr / n), (u8)(sg / n), (u8)(sb / n), 255);
        }
    }
}

static u32 lerp(u32 a, u32 b, int t, int tmax)
{
    if (tmax <= 0)
        return a;
    int k = t * 256 / tmax;
    u8 ar = GFX_R(a), ag = GFX_G(a), ab = GFX_B(a);
    u8 br = GFX_R(b), bg = GFX_G(b), bb = GFX_B(b);
    int r = ar + ((br - ar) * k) / 256;
    int g = ag + ((bg - ag) * k) / 256;
    int bl = ab + ((bb - ab) * k) / 256;
    return RGB((u8)r, (u8)g, (u8)bl);
}

void gfx_gradient_v(struct gfx *g, int x, int y, int w, int h,
                    u32 top, u32 bottom)
{
    for (int yy = 0; yy < h; yy++)
        gfx_hline(g, x, x + w - 1, y + yy, lerp(top, bottom, yy, h));
}

void gfx_gradient_diag(struct gfx *g, int x, int y, int w, int h,
                       u32 top, u32 bottom)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            int t = (xx + yy) * 256 / (w + h);
            int k = t;
            u8 ar = GFX_R(top), ag = GFX_G(top), ab = GFX_B(top);
            u8 br = GFX_R(bottom), bg = GFX_G(bottom), bb = GFX_B(bottom);
            u32 c = RGB((u8)(ar + ((br - ar) * k) / 256),
                        (u8)(ag + ((bg - ag) * k) / 256),
                        (u8)(ab + ((bb - ab) * k) / 256));
            gfx_px(g, x + xx, y + yy, c);
        }
}

/* décode UTF-8 court / decode short UTF-8 */
static int utf8_next(const char **p)
{
    unsigned char c = (unsigned char)*(*p)++;
    if (c < 0x80)
        return c;
    if ((c & 0xE0) == 0xC0 && **p) {
        int v = ((c & 0x1F) << 6) | (unsigned char)(*(*p)++ & 0x3F);
        return v;
    }
    if ((c & 0xF0) == 0xE0 && *(*p) && (*p)[1]) {
        const unsigned char *q = (const unsigned char *)*p;
        int v = ((c & 0x0F) << 12) | ((q[0] & 0x3F) << 6) | (q[1] & 0x3F);
        *p += 2;
        return v;
    }
    return '?';
}

static void draw_glyph(struct gfx *g, int x, int y, int cp, u32 c, int sc)
{
    const unsigned short *rows;
    if (!font_lookup(cp, &rows))
        return;
    for (int ry = 0; ry < 16; ry++) {
        u16 bits = rows[ry];
        for (int rx = 0; rx < 9; rx++) {
            if (bits & (1u << rx)) {
                for (int sy = 0; sy < sc; sy++)
                    for (int sx = 0; sx < sc; sx++)
                        gfx_px(g, x + rx * sc + sx, y + ry * sc + sy, c);
            }
        }
    }
}

void gfx_text(struct gfx *g, int x, int y, const char *s, u32 c)
{
    gfx_text_scale(g, x, y, s, c, 1);
}

void gfx_text_scale(struct gfx *g, int x, int y, const char *s, u32 c, int sc)
{
    int cx = x;
    while (*s) {
        if (*s == '\n') { s++; continue; }
        int cp = utf8_next(&s);
        draw_glyph(g, cx, y, cp, c, sc);
        cx += 9 * sc;
    }
}

int gfx_text_w(const char *s, int scale)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        if (*p == '\n') { p++; continue; }
        utf8_next(&p);
        n++;
    }
    return n * 9 * scale;
}

int gfx_text_h(int scale)
{
    return 16 * scale;
}
