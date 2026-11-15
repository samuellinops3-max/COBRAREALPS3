/*
 * COBRA ARENA - jogo estilo "minhoca neon" para PS3 homebrew
 * PSL1GHT + SDL 1.2, 1280x720, tudo desenhado em codigo (sem imagens/fontes).
 * Comeca direto na partida (sem menu).
 *
 * Controles:
 *   Analogico esquerdo OU setinhas (D-Pad) = direcao da cobra
 *   X / R2 / R1 / Quadrado                 = Boost (acelera gastando tamanho)
 *   (PC: setas/WASD ou mouse, ESPACO ou clique = boost)
 *
 * A cobra SEMPRE anda pra frente sozinha, entao nunca fica "travada".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL/SDL.h>

#if defined(__powerpc64__) || defined(__PPU__) || defined(__CELLOS_LV2__)
#define PS3_BUILD 1
#include <sysutil/sysutil.h>
#endif

#define SW 1280
#define SH 720
#define ARENA_R 3200.0f
#define MAXSEG 800
#define NSNAKE 24
#define MAXFOOD 5000
#define NFOOD_TARGET 2400
#define TW 70
#define TH 120
#define PI_F 3.14159265f
#define MAXMASS 2300.0f

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static int g_running = 1;

#ifdef PS3_BUILD
static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param;
    (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) g_running = 0;
}
#endif

static SDL_Surface *screen = NULL;
static Uint32 *pix = NULL;
static int pitch4 = SW;
static int rsh = 16, gsh = 8, bsh = 0;

static float camx = 0, camy = 0, camsc = 1.0f;
static float gtime = 0;
static float g_pdead = 0;
static int g_deadscore = 0;

static Uint32 bgtile[TH][TW];

/* ------------------------------------------------------------------ */
/* Aleatorio                                                           */
/* ------------------------------------------------------------------ */
static unsigned int rs = 2463534242u;
static unsigned int rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}
static float frand(void) { return (float)(rnd() & 0xFFFFFF) / 16777216.0f; }

/* ------------------------------------------------------------------ */
/* Cores / primitivas de desenho                                       */
/* ------------------------------------------------------------------ */
static inline Uint32 RGB(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((Uint32)r << rsh) | ((Uint32)g << gsh) | ((Uint32)b << bsh);
}

static inline Uint32 blendpx(Uint32 d, int r, int g, int b, int a)
{
    int dr = (d >> rsh) & 255;
    int dg = (d >> gsh) & 255;
    int db = (d >> bsh) & 255;
    dr += ((r - dr) * a) / 256;
    dg += ((g - dg) * a) / 256;
    db += ((b - db) * a) / 256;
    return ((Uint32)dr << rsh) | ((Uint32)dg << gsh) | ((Uint32)db << bsh);
}

static void fill_rect(int x, int y, int w, int h, Uint32 c)
{
    int x1 = x + w, y1 = y + h, i, j;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SW) x1 = SW;
    if (y1 > SH) y1 = SH;
    for (j = y; j < y1; j++) {
        Uint32 *p = pix + j * pitch4;
        for (i = x; i < x1; i++) p[i] = c;
    }
}

static void fill_circle(int cx, int cy, int r, Uint32 c)
{
    int y0, y1, y, rr;
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= SW || cy + r < 0 || cy - r >= SH) return;
    y0 = cy - r; y1 = cy + r;
    if (y0 < 0) y0 = 0;
    if (y1 >= SH) y1 = SH - 1;
    rr = r * r;
    for (y = y0; y <= y1; y++) {
        int dy = y - cy;
        int w = (int)sqrtf((float)(rr - dy * dy));
        int x0 = cx - w, x1 = cx + w, x;
        Uint32 *p = pix + y * pitch4;
        if (x0 < 0) x0 = 0;
        if (x1 >= SW) x1 = SW - 1;
        for (x = x0; x <= x1; x++) p[x] = c;
    }
}

static void blend_circle(int cx, int cy, int r, int cr, int cg, int cb, int a)
{
    int y0, y1, y, rr;
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= SW || cy + r < 0 || cy - r >= SH) return;
    y0 = cy - r; y1 = cy + r;
    if (y0 < 0) y0 = 0;
    if (y1 >= SH) y1 = SH - 1;
    rr = r * r;
    for (y = y0; y <= y1; y++) {
        int dy = y - cy;
        int w = (int)sqrtf((float)(rr - dy * dy));
        int x0 = cx - w, x1 = cx + w, x;
        Uint32 *p = pix + y * pitch4;
        if (x0 < 0) x0 = 0;
        if (x1 >= SW) x1 = SW - 1;
        for (x = x0; x <= x1; x++) p[x] = blendpx(p[x], cr, cg, cb, a);
    }
}

/* ------------------------------------------------------------------ */
/* Fonte 5x7 (caracteres 32..90)                                       */
/* ------------------------------------------------------------------ */
static const unsigned char font[59][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* espaco */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x0E,0x15,0x14,0x0E,0x05,0x15,0x0E}, /* $ */
    {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, /* % */
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* : */
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}  /* Z */
};

static void blend_rect(int x, int y, int w, int h, int r, int g, int b, int a)
{
    int x1 = x + w, y1 = y + h, i, j;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SW) x1 = SW;
    if (y1 > SH) y1 = SH;
    for (j = y; j < y1; j++) {
        Uint32 *p = pix + j * pitch4;
        for (i = x; i < x1; i++) p[i] = blendpx(p[i], r, g, b, a);
    }
}

static void draw_text_raw(int x, int y, const char *s, int sc, Uint32 color)
{
    for (; *s; s++) {
        int ch = (unsigned char)*s, row, colm;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) { x += 6 * sc; continue; }
        for (row = 0; row < 7; row++) {
            unsigned char bits = font[ch - 32][row];
            for (colm = 0; colm < 5; colm++) {
                if (bits & (0x10 >> colm))
                    fill_rect(x + colm * sc, y + row * sc, sc, sc, color);
            }
        }
        x += 6 * sc;
    }
}

static void draw_text(int x, int y, const char *s, int sc, Uint32 color)
{
    draw_text_raw(x + sc, y + sc, s, sc, RGB(0, 0, 0));
    draw_text_raw(x, y, s, sc, color);
}

static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }

/* ------------------------------------------------------------------ */
/* Skins (cores das cobras)                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    int pc, len, smooth;
    int c[8][3];
} Skin;

#define NSKINS 12
static const Skin skins[NSKINS] = {
    {1, 1, 0, {{255, 80, 90}}},
    {1, 1, 0, {{90, 205, 215}}},
    {1, 1, 0, {{225, 45, 215}}},
    {1, 1, 0, {{240, 165, 60}}},
    {7, 5, 1, {{255, 60, 60}, {255, 160, 40}, {250, 230, 50}, {70, 220, 80},
               {50, 210, 230}, {70, 110, 255}, {200, 70, 240}}},
    {3, 6, 0, {{60, 200, 70}, {235, 195, 40}, {40, 80, 210}}},
    {1, 1, 0, {{235, 235, 240}}},
    {1, 1, 0, {{80, 235, 110}}},
    {1, 1, 0, {{170, 85, 235}}},
    {2, 4, 0, {{60, 120, 255}, {240, 240, 255}}},
    {2, 4, 0, {{235, 55, 55}, {245, 245, 245}}},
    {1, 1, 0, {{250, 225, 65}}}
};

static void skin_color(int skin, int i, int *r, int *g, int *b)
{
    const Skin *s = &skins[skin];
    int k, k2, f;
    if (s->pc == 1) { *r = s->c[0][0]; *g = s->c[0][1]; *b = s->c[0][2]; return; }
    k = (i / s->len) % s->pc;
    if (!s->smooth) { *r = s->c[k][0]; *g = s->c[k][1]; *b = s->c[k][2]; return; }
    k2 = (k + 1) % s->pc;
    f = (i % s->len) * 256 / s->len;
    *r = s->c[k][0] + ((s->c[k2][0] - s->c[k][0]) * f) / 256;
    *g = s->c[k][1] + ((s->c[k2][1] - s->c[k][1]) * f) / 256;
    *b = s->c[k][2] + ((s->c[k2][2] - s->c[k][2]) * f) / 256;
}

/* ------------------------------------------------------------------ */
/* Cobras e comida                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    int alive, skin, n, boost, aggr;
    float mass, ang, tang, spd, dropt, aitimer, respawn, inv;
    const char *name;
    float px[MAXSEG], py[MAXSEG];
} Snake;

typedef struct {
    float x, y, val, ph;
    int act;
    unsigned char r, g, b;
} Food;

static Snake sn[NSNAKE];
static Food food[MAXFOOD];
static int fidx = 0;
static int nact = 0;

#define NNAMES 24
static const char *names[NNAMES] = {
    "YOU", "AEON", "DELTA", "MAYA", "KIKO", "ZEUS", "LUNA", "BOLT",
    "NOVA", "RAIO", "TITAN", "ONIX", "PIXEL", "TURBO", "MIKA", "FOGO",
    "GELO", "NEON", "SOMBRA", "VIPER", "COBRA", "JADE", "ORBIT", "NINJA"
};

static const int fcol[12][3] = {
    {255, 60, 60}, {255, 150, 40}, {255, 235, 60}, {120, 255, 80},
    {60, 255, 170}, {60, 230, 255}, {80, 140, 255}, {170, 90, 255},
    {255, 80, 230}, {255, 255, 255}, {255, 110, 150}, {160, 255, 255}
};

static float seg_radius(int n)
{
    float r = 14.0f + (float)n * 0.03f;
    if (r > 34.0f) r = 34.0f;
    return r;
}

static int segs_for(float mass)
{
    int n = 10 + (int)(mass * 0.33f);
    if (n > MAXSEG - 1) n = MAXSEG - 1;
    return n;
}

static float norm_ang(float a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static float food_rad(float val)
{
    float r = 4.0f + val * 1.6f;
    if (r > 13.0f) r = 13.0f;
    return r;
}

static void add_food(float x, float y, float val, int r, int g, int b)
{
    int tries = 0;
    Food *f;
    while (tries < MAXFOOD && food[fidx].act) {
        fidx = (fidx + 1) % MAXFOOD;
        tries++;
    }
    f = &food[fidx];
    f->x = x; f->y = y; f->val = val;
    f->ph = frand() * 6.28f;
    f->r = (unsigned char)r; f->g = (unsigned char)g; f->b = (unsigned char)b;
    f->act = 1;
    fidx = (fidx + 1) % MAXFOOD;
}

static void spawn_ambient_food(void)
{
    float a = frand() * 2.0f * PI_F;
    float d = sqrtf(frand()) * (ARENA_R - 80.0f);
    float cx = cosf(a) * d, cy = sinf(a) * d;
    int c = (int)(rnd() % 12);
    int cnt = 1 + (int)(rnd() % 5), k;
    for (k = 0; k < cnt; k++) {
        float val = 0.8f + frand() * 1.7f;
        float ox = (frand() - 0.5f) * 70.0f, oy = (frand() - 0.5f) * 70.0f;
        if (frand() < 0.05f) val = 4.0f + frand() * 2.0f;
        add_food(cx + ox, cy + oy, val, fcol[c][0], fcol[c][1], fcol[c][2]);
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 0, y = 0, sp;
    int tries, k;
    for (tries = 0; tries < 12; tries++) {
        float a = frand() * 2.0f * PI_F;
        float d = sqrtf(frand()) * ARENA_R * 0.85f;
        x = cosf(a) * d;
        y = sinf(a) * d;
        if (i != 0 && sn[0].alive) {
            float dx = x - sn[0].px[0], dy = y - sn[0].py[0];
            if (dx * dx + dy * dy > 700.0f * 700.0f) break;
        } else break;
    }
    s->mass = (i == 0) ? 30.0f : 30.0f + frand() * 220.0f;
    s->n = segs_for(s->mass);
    s->ang = frand() * 2.0f * PI_F;
    s->tang = s->ang;
    s->spd = 170.0f;
    s->boost = 0;
    s->dropt = 0;
    s->aitimer = frand() * 0.2f;
    s->inv = 2.0f;
    s->skin = (int)(rnd() % NSKINS);
    s->aggr = (i % 5 == 1);
    s->name = names[i % NNAMES];
    sp = seg_radius(s->n) * 0.6f;
    for (k = 0; k < s->n; k++) {
        s->px[k] = x - cosf(s->ang) * sp * (float)k;
        s->py[k] = y - sinf(s->ang) * sp * (float)k;
    }
    s->alive = 1;
    if (i == 0) { camx = x; camy = y; }
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    int n = s->n, cnt, k;
    float r = seg_radius(n), val;
    cnt = n / 2;
    if (cnt < 4) cnt = 4;
    val = s->mass * 0.6f / (float)cnt;
    if (val < 1.0f) val = 1.0f;
    if (val > 6.0f) val = 6.0f;
    for (k = 0; k < n; k += 2) {
        int cr, cg, cb;
        float x = s->px[k] + (frand() - 0.5f) * r;
        float y = s->py[k] + (frand() - 0.5f) * r;
        skin_color(s->skin, k, &cr, &cg, &cb);
        add_food(x, y, val, cr, cg, cb);
    }
    s->alive = 0;
    s->respawn = 2.0f + frand() * 3.0f;
    if (i == 0) {
        g_pdead = 2.8f;
        g_deadscore = (int)s->mass;
    }
}

/* ------------------------------------------------------------------ */
/* Entrada (controle PS3 + teclado/mouse no PC)                        */
/* ------------------------------------------------------------------ */
static SDL_Joystick *joys[4];
static int njoys = 0;
static float joy_retry = 0;
static float in_ang = 0;
static int in_has = 0;
static int in_boost = 0;

static void open_joys(void)
{
    int n = SDL_NumJoysticks(), i;
    if (n > 4) n = 4;
    for (i = 0; i < n; i++) {
        if (!joys[i]) joys[i] = SDL_JoystickOpen(i);
    }
    njoys = 0;
    for (i = 0; i < 4; i++) if (joys[i]) njoys++;
}

static void poll_input(float dt)
{
    int i, dx = 0, dy = 0;
    float best = 0.0f, bax = 0, bay = 0;
    in_has = 0;
    in_boost = 0;

    joy_retry -= dt;
    if (njoys == 0 && joy_retry <= 0.0f) {
        joy_retry = 2.0f;
        open_joys();
    }
    SDL_JoystickUpdate();

    for (i = 0; i < 4; i++) {
        SDL_Joystick *J = joys[i];
        int na, nh, st;
        if (!J) continue;
        na = SDL_JoystickNumAxes(J);
        for (st = 0; st + 1 < na && st < 4; st += 2) {
            float ax = (float)SDL_JoystickGetAxis(J, st) / 32768.0f;
            float ay = (float)SDL_JoystickGetAxis(J, st + 1) / 32768.0f;
            float m = ax * ax + ay * ay;
            if (m > best) { best = m; bax = ax; bay = ay; }
        }
#ifdef PS3_BUILD
        if (SDL_JoystickGetButton(J, 4)) dy -= 1;   /* cima */
        if (SDL_JoystickGetButton(J, 5)) dx += 1;   /* direita */
        if (SDL_JoystickGetButton(J, 6)) dy += 1;   /* baixo */
        if (SDL_JoystickGetButton(J, 7)) dx -= 1;   /* esquerda */
#endif
        nh = SDL_JoystickNumHats(J);
        if (nh > 0) {
            Uint8 h = SDL_JoystickGetHat(J, 0);
            if (h & SDL_HAT_UP) dy -= 1;
            if (h & SDL_HAT_DOWN) dy += 1;
            if (h & SDL_HAT_LEFT) dx -= 1;
            if (h & SDL_HAT_RIGHT) dx += 1;
        }
        if (SDL_JoystickGetButton(J, 14) || SDL_JoystickGetButton(J, 0) ||
            SDL_JoystickGetButton(J, 9) || SDL_JoystickGetButton(J, 11) ||
            SDL_JoystickGetButton(J, 15))
            in_boost = 1;
    }

    /* analogico tem prioridade (deadzone 0.15) */
    if (best > 0.15f * 0.15f) {
        in_ang = atan2f(bay, bax);
        in_has = 1;
    } else if (dx != 0 || dy != 0) {
        in_ang = atan2f((float)dy, (float)dx);
        in_has = 1;
    }

    /* teclado / mouse (PC) */
    {
        Uint8 *k = SDL_GetKeyState(NULL);
        static int lmx = -1, lmy = -1;
        int mx = 0, my = 0, kx = 0, ky = 0;
        Uint8 mb;
        if (k) {
            if (k[SDLK_LEFT] || k[SDLK_a]) kx -= 1;
            if (k[SDLK_RIGHT] || k[SDLK_d]) kx += 1;
            if (k[SDLK_UP] || k[SDLK_w]) ky -= 1;
            if (k[SDLK_DOWN] || k[SDLK_s]) ky += 1;
            if (k[SDLK_SPACE]) in_boost = 1;
        }
        if (!in_has && (kx != 0 || ky != 0)) {
            in_ang = atan2f((float)ky, (float)kx);
            in_has = 1;
        }
        mb = SDL_GetMouseState(&mx, &my);
        if (!in_has && lmx >= 0 && (mx != lmx || my != lmy)) {
            float ddx = (float)(mx - SW / 2), ddy = (float)(my - SH / 2);
            if (ddx * ddx + ddy * ddy > 400.0f) {
                in_ang = atan2f(ddy, ddx);
                in_has = 1;
            }
        }
        lmx = mx; lmy = my;
        if (mb & SDL_BUTTON(1)) in_boost = 1;
    }
}

/* ------------------------------------------------------------------ */
/* IA dos bots                                                         */
/* ------------------------------------------------------------------ */
static void ai_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->px[0], hy = s->py[0];
    float r = seg_radius(s->n);
    float ca = cosf(s->ang), sa = sinf(s->ang);
    float bestd = 450.0f * 450.0f, want = s->ang;
    int bf = -1, f, j, k, p, threat = 0, chasing = 0;
    float tx = 0, ty = 0, dc;

    s->aitimer = 0.08f + frand() * 0.08f;

    /* procura comida proxima (prefere as grandes) */
    for (f = 0; f < MAXFOOD; f++) {
        float dx, dy, d;
        if (!food[f].act) continue;
        dx = food[f].x - hx; dy = food[f].y - hy;
        if (dx > 450.0f || dx < -450.0f || dy > 450.0f || dy < -450.0f) continue;
        d = dx * dx + dy * dy - food[f].val * 400.0f;
        if (d < bestd) { bestd = d; bf = f; }
    }
    if (bf >= 0) want = atan2f(food[bf].y - hy, food[bf].x - hx);
    else want = s->ang + (frand() - 0.5f) * 1.2f;

    /* bots agressivos tentam cortar o caminho do jogador */
    if (s->aggr && sn[0].alive && s->mass > sn[0].mass * 0.8f) {
        float dx = sn[0].px[0] - hx, dy = sn[0].py[0] - hy;
        float d2 = dx * dx + dy * dy;
        if (d2 < 700.0f * 700.0f) {
            float ix = sn[0].px[0] + cosf(sn[0].ang) * (sqrtf(d2) * 0.5f + 120.0f);
            float iy = sn[0].py[0] + sinf(sn[0].ang) * (sqrtf(d2) * 0.5f + 120.0f);
            want = atan2f(iy - hy, ix - hx);
            chasing = 1;
        }
    }

    /* parede */
    dc = sqrtf(hx * hx + hy * hy);
    if (dc > ARENA_R - 400.0f) want = atan2f(-hy, -hx);

    /* desvio de corpos (2 pontos a frente) */
    for (p = 0; p < 2 && !threat; p++) {
        float L = (p == 0) ? (80.0f + r * 3.0f) : (40.0f + r * 1.5f);
        float ax = hx + ca * L, ay = hy + sa * L;
        float th = 45.0f + r * 1.5f;
        for (j = 0; j < NSNAKE && !threat; j++) {
            Snake *o = &sn[j];
            float rj, lim, lim2;
            if (!o->alive) continue;
            if (j == i) {
                for (k = 12; k < o->n; k += 3) {
                    float dx = o->px[k] - ax, dy = o->py[k] - ay;
                    if (dx * dx + dy * dy < (r * 2.2f) * (r * 2.2f)) {
                        threat = 1; tx = o->px[k]; ty = o->py[k]; break;
                    }
                }
                continue;
            }
            rj = seg_radius(o->n);
            lim = th + rj; lim2 = lim * lim;
            {
                float dxh = o->px[0] - hx, dyh = o->py[0] - hy;
                float reach = (float)o->n * rj * 0.7f + 200.0f;
                if (dxh > reach || dxh < -reach || dyh > reach || dyh < -reach) continue;
            }
            for (k = 0; k < o->n; k += 2) {
                float dx = o->px[k] - ax, dy = o->py[k] - ay;
                if (dx * dx + dy * dy < lim2) {
                    threat = 1; tx = o->px[k]; ty = o->py[k]; break;
                }
            }
        }
    }
    if (threat) {
        float cross = ca * (ty - hy) - sa * (tx - hx);
        want = s->ang + (cross > 0.0f ? -1.4f : 1.4f);
        s->boost = 0;
    } else {
        if (chasing && s->mass > 100.0f) s->boost = 1;
        else s->boost = (s->mass > 100.0f && frand() < 0.06f) ? 1 : 0;
    }
    s->tang = norm_ang(want);
}

/* ------------------------------------------------------------------ */
/* Atualizacao do jogo                                                 */
/* ------------------------------------------------------------------ */
static int order[NSNAKE];
static int nord = 0;

static void update(float dt)
{
    int i, k, f;
    int dead[NSNAKE];

    gtime += dt;
    poll_input(dt);

    for (i = 0; i < NSNAKE; i++) {
        Snake *s = &sn[i];
        float tr, target, r, sp, mag, mag2, eat;
        int canboost, nn;

        if (!s->alive) {
            s->respawn -= dt;
            if (s->respawn <= 0.0f) {
                if (i == 0) {
                    if (g_pdead <= 0.0f) spawn_snake(0);
                } else spawn_snake(i);
            }
            continue;
        }
        if (s->inv > 0.0f) s->inv -= dt;

        /* controle */
        if (i == 0) {
            if (in_has) s->tang = in_ang;
            s->boost = in_boost;
        } else {
            s->aitimer -= dt;
            if (s->aitimer <= 0.0f) ai_think(i);
        }

        /* giro */
        tr = 4.6f - (float)s->n * 0.004f;
        if (tr < 1.9f) tr = 1.9f;
        {
            float diff = norm_ang(s->tang - s->ang);
            float mx = tr * dt;
            if (diff > mx) diff = mx;
            if (diff < -mx) diff = -mx;
            s->ang = norm_ang(s->ang + diff);
        }

        /* boost */
        canboost = (s->boost && s->mass > 20.0f);
        target = canboost ? 330.0f : 170.0f;
        {
            float t = dt * 8.0f;
            if (t > 1.0f) t = 1.0f;
            s->spd += (target - s->spd) * t;
        }
        if (canboost) {
            s->mass -= 10.0f * dt;
            s->dropt += dt;
            if (s->dropt > 0.1f) {
                int cr, cg, cb;
                s->dropt = 0.0f;
                skin_color(s->skin, s->n - 1, &cr, &cg, &cb);
                add_food(s->px[s->n - 1], s->py[s->n - 1], 1.0f, cr, cg, cb);
            }
        }

        /* movimento da cabeca */
        s->px[0] += cosf(s->ang) * s->spd * dt;
        s->py[0] += sinf(s->ang) * s->spd * dt;

        /* crescimento / encolhimento */
        nn = segs_for(s->mass);
        if (nn > s->n) {
            for (k = s->n; k < nn; k++) {
                s->px[k] = s->px[s->n - 1];
                s->py[k] = s->py[s->n - 1];
            }
        }
        s->n = nn;

        /* corpo segue a cabeca */
        r = seg_radius(s->n);
        sp = r * 0.6f;
        for (k = 1; k < s->n; k++) {
            float dx = s->px[k - 1] - s->px[k];
            float dy = s->py[k - 1] - s->py[k];
            float d = sqrtf(dx * dx + dy * dy);
            if (d > sp) {
                float m = (d - sp) / d;
                s->px[k] += dx * m;
                s->py[k] += dy * m;
            }
        }

        /* comer (com ima) */
        mag = r + 70.0f;
        mag2 = mag * mag;
        eat = r * 0.9f + 6.0f;
        for (f = 0; f < MAXFOOD; f++) {
            Food *fd = &food[f];
            float dx, dy, d2, d, step;
            if (!fd->act) continue;
            dx = fd->x - s->px[0];
            dy = fd->y - s->py[0];
            if (dx > mag || dx < -mag || dy > mag || dy < -mag) continue;
            d2 = dx * dx + dy * dy;
            if (d2 >= mag2) continue;
            d = sqrtf(d2) + 0.001f;
            step = 450.0f * dt;
            if (d - step < eat) {
                fd->act = 0;
                s->mass += fd->val;
                if (s->mass > MAXMASS) s->mass = MAXMASS;
            } else {
                fd->x -= dx / d * step;
                fd->y -= dy / d * step;
            }
        }
    }

    /* colisoes */
    for (i = 0; i < NSNAKE; i++) dead[i] = 0;
    for (i = 0; i < NSNAKE; i++) {
        Snake *s = &sn[i];
        float hx, hy, ri;
        int j;
        if (!s->alive || s->inv > 0.0f) continue;
        hx = s->px[0]; hy = s->py[0];
        ri = seg_radius(s->n);
        if (hx * hx + hy * hy > ARENA_R * ARENA_R) { dead[i] = 1; continue; }
        for (j = 0; j < NSNAKE && !dead[i]; j++) {
            Snake *o = &sn[j];
            float rj, reach, lim2, dxh, dyh;
            if (j == i || !o->alive) continue;
            rj = seg_radius(o->n);
            reach = (float)o->n * rj * 0.7f + 120.0f;
            dxh = o->px[0] - hx; dyh = o->py[0] - hy;
            if (dxh > reach || dxh < -reach || dyh > reach || dyh < -reach) continue;
            lim2 = (ri * 0.55f + rj * 0.75f);
            lim2 *= lim2;
            for (k = 0; k < o->n; k++) {
                float dx = o->px[k] - hx, dy = o->py[k] - hy;
                if (dx * dx + dy * dy < lim2) { dead[i] = 1; break; }
            }
        }
    }
    for (i = 0; i < NSNAKE; i++) if (dead[i]) kill_snake(i);

    /* tempo da tela de morte do jogador */
    if (g_pdead > 0.0f) {
        g_pdead -= dt;
        if (g_pdead <= 0.0f && !sn[0].alive) {
            g_pdead = 0.0f;
            spawn_snake(0);
        }
    }

    /* comida ambiente */
    nact = 0;
    for (f = 0; f < MAXFOOD; f++) if (food[f].act) nact++;
    if (nact < NFOOD_TARGET) {
        for (k = 0; k < 3; k++) spawn_ambient_food();
    }

    /* camera */
    if (sn[0].alive) {
        float t = dt * 8.0f, tsc;
        if (t > 1.0f) t = 1.0f;
        camx += (sn[0].px[0] - camx) * t;
        camy += (sn[0].py[0] - camy) * t;
        tsc = 1.15f / (1.0f + (float)sn[0].n * 0.0025f);
        if (tsc > 1.0f) tsc = 1.0f;
        if (tsc < 0.45f) tsc = 0.45f;
        camsc += (tsc - camsc) * (dt * 2.0f > 1.0f ? 1.0f : dt * 2.0f);
    }

    /* ranking */
    nord = 0;
    for (i = 0; i < NSNAKE; i++) {
        if (sn[i].alive) {
            int p = nord++;
            while (p > 0 && sn[order[p - 1]].mass < sn[i].mass) {
                order[p] = order[p - 1];
                p--;
            }
            order[p] = i;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Desenho                                                             */
/* ------------------------------------------------------------------ */
static void build_bg(void)
{
    int x, y, i, j, l;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            float d1 = 1e9f, d2 = 1e9f;
            for (l = 0; l < 2; l++) {
                for (j = -1; j <= 2; j++) {
                    for (i = -1; i <= 1; i++) {
                        float cx = (float)(i * TW) + (l ? (float)TW * 0.5f : 0.0f);
                        float cy = (float)(j * TH) + (l ? (float)TH * 0.5f : 0.0f);
                        float dx = (float)x - cx, dy = (float)y - cy;
                        float d = sqrtf(dx * dx + dy * dy);
                        if (d < d1) { d2 = d1; d1 = d; }
                        else if (d < d2) d2 = d;
                    }
                }
            }
            {
                float edge = d2 - d1;
                float face = 40.0f - d1 * 0.22f;
                float lum;
                if (edge < 4.0f) lum = 12.0f + (face - 12.0f) * (edge / 4.0f);
                else lum = face;
                bgtile[y][x] = RGB((int)lum, (int)lum, (int)(lum + 6.0f));
            }
        }
    }
}

static void draw_bg(void)
{
    int y, offx, offy;
    float R = ARENA_R;
    Uint32 dark = RGB(9, 6, 10), red = RGB(190, 30, 50);

    offx = (int)floorf(camx * camsc);
    offy = (int)floorf(camy * camsc);
    offx %= TW; if (offx < 0) offx += TW;
    offy %= TH; if (offy < 0) offy += TH;

    for (y = 0; y < SH; y++) {
        Uint32 *row = pix + y * pitch4;
        int ty = (y + offy) % TH;
        int x = 0, tx = offx;
        while (x < SW) {
            int ch = TW - tx;
            if (ch > SW - x) ch = SW - x;
            memcpy(row + x, &bgtile[ty][tx], (size_t)ch * 4);
            x += ch;
            tx = 0;
        }
    }

    for (y = 0; y < SH; y++) {
        float wy = ((float)y - (float)SH * 0.5f) / camsc + camy;
        float ay = fabsf(wy);
        float hx;
        int xl, xr;
        if (ay >= R) {
            fill_rect(0, y, SW, 1, (ay < R + 6.0f / camsc) ? red : dark);
            continue;
        }
        hx = sqrtf(R * R - wy * wy);
        xl = (int)((-hx - camx) * camsc + (float)SW * 0.5f);
        xr = (int)((hx - camx) * camsc + (float)SW * 0.5f);
        if (xl > 0) fill_rect(0, y, xl, 1, dark);
        if (xr < SW) fill_rect(xr, y, SW - xr, 1, dark);
        fill_rect(xl - 5, y, 5, 1, red);
        fill_rect(xr, y, 5, 1, red);
    }
}

static void draw_foods(void)
{
    int f;
    for (f = 0; f < MAXFOOD; f++) {
        Food *fd = &food[f];
        float pulse;
        int sx, sy, rad;
        if (!fd->act) continue;
        sx = (int)((fd->x - camx) * camsc + (float)SW * 0.5f);
        sy = (int)((fd->y - camy) * camsc + (float)SH * 0.5f);
        if (sx < -20 || sx > SW + 20 || sy < -20 || sy > SH + 20) continue;
        pulse = 1.0f + 0.15f * sinf(gtime * 4.0f + fd->ph);
        rad = (int)(food_rad(fd->val) * camsc * 1.3f * pulse + 0.5f);
        if (rad < 2) rad = 2;
        blend_circle(sx, sy, rad * 2, fd->r, fd->g, fd->b, 45);
        fill_circle(sx, sy, rad, RGB(fd->r, fd->g, fd->b));
        if (rad >= 3)
            fill_circle(sx - rad / 4, sy - rad / 4, rad / 2,
                        RGB(fd->r + 80, fd->g + 80, fd->b + 80));
    }
}

static void draw_snake(int si)
{
    Snake *s = &sn[si];
    int n = s->n, i;
    float r = seg_radius(n);
    float rad_s = r * camsc;
    int cr, cg, cb;

    /* brilho neon (mais forte no boost) */
    {
        int boosting = (s->boost && s->mass > 20.0f);
        int stp = boosting ? 2 : 3;
        for (i = n - 1; i >= 0; i -= stp) {
            int sx = (int)((s->px[i] - camx) * camsc + (float)SW * 0.5f);
            int sy = (int)((s->py[i] - camy) * camsc + (float)SH * 0.5f);
            int hr = (int)(rad_s * (boosting ? 2.0f : 1.7f));
            if (sx < -hr || sx > SW + hr || sy < -hr || sy > SH + hr) continue;
            skin_color(s->skin, i, &cr, &cg, &cb);
            blend_circle(sx, sy, hr, cr, cg, cb, boosting ? 55 : 28);
        }
    }

    for (i = n - 1; i >= 0; i--) {
        float f = 1.0f;
        int sx, sy, rad;
        sx = (int)((s->px[i] - camx) * camsc + (float)SW * 0.5f);
        sy = (int)((s->py[i] - camy) * camsc + (float)SH * 0.5f);
        if (i > n - 8) f = 0.6f + 0.4f * (float)(n - 1 - i) / 8.0f;
        rad = (int)(rad_s * f + 0.5f);
        if (rad < 2) rad = 2;
        if (sx < -rad || sx > SW + rad || sy < -rad || sy > SH + rad) continue;
        skin_color(s->skin, i, &cr, &cg, &cb);
        if (rad < 5) {
            fill_circle(sx, sy, rad, RGB(cr, cg, cb));
        } else {
            fill_circle(sx, sy, rad, RGB(cr * 55 / 100, cg * 55 / 100, cb * 55 / 100));
            fill_circle(sx, sy, rad * 78 / 100, RGB(cr, cg, cb));
            fill_circle(sx - rad / 5, sy - rad / 5, rad * 35 / 100,
                        RGB(cr + 70, cg + 70, cb + 70));
        }
    }

    /* olhos */
    {
        float ca = cosf(s->ang), sa = sinf(s->ang);
        float hx = (s->px[0] - camx) * camsc + (float)SW * 0.5f;
        float hy = (s->py[0] - camy) * camsc + (float)SH * 0.5f;
        float er = rad_s * 0.42f, fwd = rad_s * 0.35f, side = rad_s * 0.55f;
        int sg;
        if (er < 2.0f) er = 2.0f;
        for (sg = -1; sg <= 1; sg += 2) {
            float ex = hx + ca * fwd - sa * side * (float)sg;
            float ey = hy + sa * fwd + ca * side * (float)sg;
            fill_circle((int)ex, (int)ey, (int)er, RGB(255, 255, 255));
            fill_circle((int)(ex + ca * er * 0.35f), (int)(ey + sa * er * 0.35f),
                        (int)(er * 0.55f + 0.5f), RGB(0, 0, 0));
        }
        if (si != 0) {
            int tw = text_w(s->name, 1);
            draw_text((int)hx - tw / 2, (int)(hy + rad_s + 8.0f), s->name, 1, RGB(200, 200, 210));
        }
    }
}

static void draw_hud(void)
{
    char buf[64];
    int i, rank = 0;

    /* placar */
    blend_rect(SW - 262, 6, 254, 246, 0, 0, 0, 90);
    draw_text(SW - 250, 14, "LEADERBOARD", 2, RGB(230, 230, 240));
    for (i = 0; i < nord && i < 10; i++) {
        int idx = order[i];
        Uint32 col = (idx == 0) ? RGB(255, 235, 80) : RGB(200, 200, 210);
        snprintf(buf, sizeof(buf), "%d. %s", i + 1, sn[idx].name);
        draw_text(SW - 250, 42 + i * 20, buf, 2, col);
        snprintf(buf, sizeof(buf), "%d", (int)sn[idx].mass);
        draw_text(SW - 20 - text_w(buf, 2), 42 + i * 20, buf, 2, col);
    }

    /* comprimento / ranking */
    for (i = 0; i < nord; i++) if (order[i] == 0) rank = i + 1;
    if (sn[0].alive) {
        snprintf(buf, sizeof(buf), "YOUR LENGTH: %d", (int)sn[0].mass);
        draw_text(14, SH - 56, buf, 2, RGB(240, 240, 250));
        snprintf(buf, sizeof(buf), "YOUR RANK: %d OF %d", rank, nord);
        draw_text(14, SH - 32, buf, 2, RGB(240, 240, 250));
    }

    /* minimapa */
    {
        int mcx = SW - 70, mcy = SH - 70, mr = 48, a;
        blend_circle(mcx, mcy, mr, 0, 0, 0, 120);
        for (a = 0; a < 120; a++) {
            float t = (float)a * (2.0f * PI_F / 120.0f);
            fill_rect(mcx + (int)(cosf(t) * (float)mr) - 1,
                      mcy + (int)(sinf(t) * (float)mr) - 1, 2, 2, RGB(150, 150, 160));
        }
        if (sn[0].alive) {
            int dx = mcx + (int)(sn[0].px[0] / ARENA_R * (float)mr);
            int dy = mcy + (int)(sn[0].py[0] / ARENA_R * (float)mr);
            fill_circle(dx, dy, 3, RGB(255, 255, 255));
        }
    }

    /* tela de morte (reinicia sozinho) */
    if (!sn[0].alive && g_pdead > 0.0f) {
        const char *t1 = "YOU DIED";
        snprintf(buf, sizeof(buf), "YOUR LENGTH: %d", g_deadscore);
        draw_text(SW / 2 - text_w(t1, 6) / 2, SH / 2 - 60, t1, 6, RGB(255, 90, 90));
        draw_text(SW / 2 - text_w(buf, 3) / 2, SH / 2 + 10, buf, 3, RGB(240, 240, 250));
    }
}

static void render(void)
{
    int i;
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    pix = (Uint32 *)screen->pixels;
    pitch4 = screen->pitch / 4;

    draw_bg();
    draw_foods();
    for (i = 1; i < NSNAKE; i++) if (sn[i].alive) draw_snake(i);
    if (sn[0].alive) draw_snake(0);
    draw_hud();

    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    Uint32 last, now;
    int i;
    (void)argc;
    (void)argv;

#ifdef PS3_BUILD
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

    screen = SDL_SetVideoMode(SW, SH, 32, SDL_SWSURFACE);
    if (!screen) { SDL_Quit(); return 1; }
    SDL_ShowCursor(SDL_DISABLE);

    rsh = screen->format->Rshift;
    gsh = screen->format->Gshift;
    bsh = screen->format->Bshift;

    rs ^= (unsigned int)SDL_GetTicks() * 2654435761u;
    if (rs == 0) rs = 2463534242u;

    memset(sn, 0, sizeof(sn));
    memset(food, 0, sizeof(food));

    build_bg();
    open_joys();

    for (i = 0; i < NFOOD_TARGET / 3; i++) spawn_ambient_food();
    for (i = 0; i < NSNAKE; i++) spawn_snake(i);

    last = SDL_GetTicks();
    while (g_running) {
        SDL_Event e;
        float dt;

#ifdef PS3_BUILD
        sysUtilCheckCallback();
#endif
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) g_running = 0;
        }

        now = SDL_GetTicks();
        dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;

        update(dt);
        render();
        SDL_Flip(screen);
    }

    for (i = 0; i < 4; i++) if (joys[i]) SDL_JoystickClose(joys[i]);
    SDL_Quit();
    return 0;
}
