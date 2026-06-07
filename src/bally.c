#include "pd_api.h"
#include "physics.h"
#include "wind.h"
#include "balloon.h"
#include "camera.h"
#include "tower.h"
#include "decor.h"
#include "enemies.h"
#include "audio.h"
#include "nav.h"
#include "tutorial.h"
#include "music.h"
#include <math.h>
#include <string.h>
#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)
#include <stdio.h>
#endif

#define SCREEN_W 400
#define SCREEN_H 240
#define CELL_W   48
#define CELL_H   60
#define WORLD_SCREENS 3
#define WORLD_W   (WORLD_SCREENS * SCREEN_W)  /* 1200 */
#define WORLD_H   300                          /* taller than screen -> show 80% (240/300) */
#define CAM_MARGIN 120
#define TERRAIN_BLIT_Y 60                      /* near-terrain image top in world (road@image206 -> world 266) */
#define CLOUD_SPACING 300   /* world-px between streaks within an echelon */
#define CLOUD_DASHES  4      /* dashes per streak */
#define CLOUD_DASH_LEN 8
#define CLOUD_DASH_PITCH 14

/* All Y values below are WORLD-y (a 300px-tall world); the vertical camera (g_camy)
 * maps world-y -> screen-y = world_y - g_camy. */
#define TOWER_X_LAUNCH 80.0f
#define TOWER_X_LAND   240.0f
#define TOWER_BODY_H   40        /* dotted cylinder+base sprite height (mockup6 art) */
#define TOWER_BASE_Y   283       /* body bottom = vertical MIDDLE of the pavement band */
#define TOWER_PAD_Y    208.0f    /* balloon-center docked/landed: basket on the saddle cap */
#define TOWER_APEX_Y   40.0f     /* balloon-center at full extension (to the stratosphere) */
#define GROUND_Y       250.0f    /* balloon-center crash height */
#define LAND_X_TOL     14.0f
#define LAND_Y_TOL     10.0f
#define LAND_VMAX      2.6f    /* free-sink terminal is ~2.0 px/frame; allow a normal descent to land */

/* --- M4 enemies / explosion / health --- */
#define MAX_HEALTH    100
#define GUN_COOLDOWN  150        /* frames between shots (~5s @30fps) */
#define MISSILE_VY    -1.3f      /* slow rise ("reverse invader") */
#define HIT_R         12.0f      /* balloon-core collision radius (world px) */
#define MISSILE_DMG   34         /* 3 hits = game over */
#define GUN_W         26
#define GUN_H         22
#define MISSILE_W     8
#define MISSILE_H     14
#define EXPL_CELL     76         /* explosion-table cell (bake R*2+8) */
#define EXPL_FRAMES   7
#define EXPL_DIV      3          /* frames held per explosion cell */

#define TERRAIN_PARALLAX 0.9f   /* background scrolls slower than foreground (depth) */

static PlaydateAPI* pd = NULL;
static LCDBitmapTable* g_balloon = NULL;
static LCDBitmap*      g_terrain_far  = NULL;
static LCDBitmap*      g_terrain_near = NULL;
static LCDBitmap*      g_moon    = NULL;
static LCDBitmap*      g_tower_body = NULL;   /* dotted cylinder + base (fixed) */
static LCDBitmap*      g_tower_cap  = NULL;   /* saddle/deck the balloon rests on */
static float           g_moon_x = 300.0f;   /* moon sky position (slow autonomous drift) */
static LCDFont*        g_font    = NULL;
static Star            g_stars[STAR_COUNT];

static PhysicsConfig g_pc;
static WindField     g_wind;
static VerticalState g_v;
static float         g_wx = 60.0f;  /* balloon center, WORLD x (wraps mod WORLD_W) */
static Camera        g_cam;
static float         g_camy = 0.0f;  /* vertical camera (world->screen = y - g_camy) */
/* per-plane horizontal parallax offsets, integrated from the camera's per-frame
 * delta so they never jump when the camera-x wraps the world seam. */
static float         g_prev_camx = 0.0f;
static float         g_par_far = 0.0f, g_par_near = 0.0f, g_par_stars = 0.0f, g_par_moon = 0.0f;
static float         g_vx = 0.0f;
static float         g_frame_dcrank = 0.0f;   /* crank delta read once per frame in update() */
static float         g_t  = 0.0f;   /* gust time */
static int           g_flame = 0;
static int           g_flame_div = 0;
static int           g_dither = 0;
static int           g_dither_div = 0;
static float g_cloud[WIND_MAX_ECHELONS]; /* per-echelon world-space drift (advances by wind) */

typedef enum { GS_SPLASH, GS_MENU, GS_ABOUT, GS_TUTORIAL,
               GS_PRELAUNCH, GS_SCROLL, GS_EXTEND, GS_HOLD,
               GS_LAUNCHED, GS_CENTERING, GS_FLIGHT, GS_CATCH, GS_WIN, GS_CRASH } GameState;
static GameState g_state = GS_PRELAUNCH;
/* --- M8 title screens --- */
#define SPLASH_FRAMES 75      /* ~2.5s at 30fps before auto-advancing to the menu */
#define MENU_SLOT_X   296     /* device-px top-left of the 2x balloon on the menu (matches render) */
#define MENU_SLOT_Y   110
static LCDBitmap* g_title_splash      = NULL;
static LCDBitmap* g_title_menu_start  = NULL;
static LCDBitmap* g_title_menu_about  = NULL;
static LCDBitmap* g_title_about       = NULL;
static int        g_menu_sel  = 0;    /* 0 = Start Game, 1 = About */
static int        g_splash_t  = 0;    /* splash frame counter */
static int        g_menu_sway = 0;    /* menu balloon sway counter */
static const int  MENU_SWAY[8] = { 2, 1, 0, 1, 2, 3, 4, 3 };  /* basket-drag dx index ping-pong */
static LCDBitmap*      g_gun_img     = NULL;
static LCDBitmap*      g_missile_img = NULL;
static LCDBitmapTable* g_expl        = NULL;
static Gun     g_guns[MAX_GUNS];
static int     g_gun_count = 0;
static Missile g_missiles[MAX_MISSILES];
static int     g_health     = MAX_HEALTH;
static int     g_expl_frame = -1;   /* -1 = not exploding; 0..EXPL_FRAMES-1 = playing */
static int     g_expl_div   = 0;
static Tower     g_tower;
static float     g_hold_x = 80.0f;     /* camera target: screen-x to hold the balloon at */
static int       g_seq = 0;            /* sequence / countdown timer (frames) */
static float     g_release_wx = 0.0f;  /* balloon world-x at launch release */

/* --- tutorial scene --- */
#define TUT_GUN_Y    198
#define TUT_FIRE     30
#define TUT_GO_FLASH 24
static LCDBitmap* g_title_tutorial = NULL;
static LCDBitmap* g_tutorial_go    = NULL;
static PhysicsConfig g_tut_pc;
static float g_tut_x, g_tut_y, g_tut_vx, g_tut_vy, g_tut_heat;
/* Menu balloon: cranking lifts it (burner whoosh), releasing lets it sink with the
 * same thermal physics as flight. ground_y = MENU_SLOT_Y so it rests at — and never
 * drops below — its menu slot; it can only rise from there. */
static PhysicsConfig g_menu_pc;
static float g_menu_y, g_menu_vy, g_menu_heat;
static int   g_tut_gun_active; static float g_tut_gun_x; static int g_tut_fire_t;
static Missile g_tut_missiles[MAX_MISSILES];
static int   g_tut_flash;
static int   g_tut_sway;
static float g_tut_pad_x; static int g_tut_lifted;
static float g_tut_cloud;
static int   g_tut_burner;   /* 1 while cranking in the tutorial (flame sprite) */
static int   g_b_count, g_b_timer, g_reset_flash;

#define LAUNCH_RIGHT_X (SCREEN_W - 60)  /* hold tower/balloon near the right edge at launch */
#define EXTEND_RATE    0.012f
#define RETRACT_RATE   0.010f
#define HOLD_FRAMES    120              /* ~4s at 30fps: 3-2-1-READY */
#define CENTER_DX      100.0f           /* drift ~1/4 screen before rubber-centering */
#define CENTER_T       450              /* ...or 15s at 30fps, whichever first */
#define CENTER_X       200              /* screen center */
#define LAND_GRAB_TOL  24.0f           /* pad can grab within half a balloon width */

static void init_wind(void) {
    /* Faithful to the browser prototype (assets_work/build_echelon3.py): canonical
     * BASE prefix + the same gust formulas below. 5 echelons sized to the 240px
     * screen with a 60px balloon (ech_height = 0.7*60 = 42): centers 30..198, all
     * on-screen so no echelon influences the balloon invisibly. */
    float base[]   = { 2.6f, 0.0f, -3.2f, 1.4f, 0.0f };
    int n = (int)(sizeof(base)/sizeof(base[0]));
    g_wind.count = n;
    g_wind.ech_height = 0.7f * (float)CELL_H; /* 42 */
    g_wind.wind_ease = 0.045f;
    float top = 30.0f;
    for (int i = 0; i < n; i++) {
        g_wind.center[i] = top + i * g_wind.ech_height;
        g_wind.base[i]   = base[i];
        g_wind.f1[i] = 0.006f + 0.004f * ((i*7)%5)/5.0f;
        g_wind.p1[i] = i * 1.7f;
        g_wind.f2[i] = 0.013f + 0.006f * ((i*3)%4)/4.0f;
        g_wind.p2[i] = i * 2.3f + 1.0f;
    }
}

static void spawn_guns(void) {
    /* M4 sample layout (per-level config arrives in M6): two stationary guns on the
     * pavement + one slow patrolling gun, timers staggered so they don't all fire
     * on the same frame. World is 1200px wide; guns sit between the launch/land area. */
    memset(g_guns, 0, sizeof(g_guns));
    g_gun_count = 0;
    Gun* a = &g_guns[g_gun_count++]; a->x = 520.0f; a->cooldown = GUN_COOLDOWN; a->timer = 0;
    Gun* b = &g_guns[g_gun_count++]; b->x = 880.0f; b->cooldown = GUN_COOLDOWN; b->timer = GUN_COOLDOWN/2;
    Gun* c = &g_guns[g_gun_count++];
    c->x = 700.0f; c->moving = 1; c->vx = 0.7f; c->lo = 660.0f; c->hi = 760.0f;
    c->cooldown = GUN_COOLDOWN; c->timer = GUN_COOLDOWN/3;
}

static int  tutorial_done_flag(void) { FileStat st; return pd->file->stat("tutorial.done", &st) == 0; }
static void tutorial_write_flag(void) {
    SDFile* f = pd->file->open("tutorial.done", kFileWrite);
    if (f) { pd->file->write(f, "1", 1); pd->file->close(f); }
}
static void tutorial_clear_flag(void) { pd->file->unlink("tutorial.done", 0); }

static void enter_tutorial(void) {
    g_tut_pc = physics_default_config();
    g_tut_pc.top_y = 14.0f; g_tut_pc.ground_y = 190.0f;
    g_tut_x = TUT_START_X; g_tut_y = 112.0f;
    g_tut_vx = 0.0f; g_tut_vy = 0.0f; g_tut_heat = 0.4f;
    g_tut_gun_active = 0; g_tut_gun_x = -40.0f; g_tut_fire_t = 0; g_tut_flash = 0; g_tut_sway = 0;
    g_tut_pad_x = 40.0f; g_tut_lifted = 0; g_tut_cloud = 0.0f;
    for (int i = 0; i < MAX_MISSILES; i++) g_tut_missiles[i].alive = 0;
    g_state = GS_TUTORIAL;
}

static void reset_game(void) {
    g_state = GS_PRELAUNCH;
    g_tower.x = TOWER_X_LAUNCH; g_tower.x_launch = TOWER_X_LAUNCH; g_tower.x_land = TOWER_X_LAND;
    g_tower.ext = 0.0f; g_tower.phase = TOWER_DOCKED;
    g_tower.pad_y = TOWER_PAD_Y; g_tower.apex_y = TOWER_APEX_Y;
    g_tower.ext_rate = 0.04f; g_tower.ret_rate = 0.03f;
    g_wx = TOWER_X_LAUNCH;                 /* balloon parked on the retracted tower */
    g_v.y = TOWER_PAD_Y; g_v.vy = 0.0f; g_v.heat = 0.5f;
    g_vx = 0.0f; g_t = 0.0f; g_seq = 0; g_hold_x = 80.0f; g_release_wx = TOWER_X_LAUNCH;
    /* prelaunch: tower parked near the left of the view */
    g_cam.x = camera_world_wrap(TOWER_X_LAUNCH - 80.0f, (float)WORLD_W);
    g_prev_camx = g_cam.x;
    g_health = MAX_HEALTH;
    g_expl_frame = -1; g_expl_div = 0;
    for (int i = 0; i < MAX_MISSILES; i++) g_missiles[i].alive = 0;
    spawn_guns();
}

static void draw_tower(void) {
    if (!g_tower_body) return;
    int sx = (int)camera_screen_x(&g_cam, g_tower.x);
    if (sx < -24 || sx > SCREEN_W + 24) return;
    int cam = (int)g_camy;
    int body_top = TOWER_BASE_Y - TOWER_BODY_H;       /* world; cylinder top */
    int cap_y    = (int)tower_cup_y(&g_tower) + 27;   /* saddle/cap top ~ basket bottom (world) */
    /* polished rod telescopes between the saddle cap and the cylinder top:
     * ~hidden when retracted (cap just above body), long when extended to apex. */
    for (int y = cap_y; y < body_top; y++) {
        pd->graphics->fillRect(sx - 2, y - cam, 1, 1, kColorWhite);
        pd->graphics->fillRect(sx + 1, y - cam, 1, 1, kColorWhite);
    }
    for (int jy = cap_y + 12; jy < body_top - 2; jy += 16)   /* telescoping joint rings */
        pd->graphics->drawLine(sx - 3, jy - cam, sx + 3, jy - cam, 1, kColorWhite);
    /* mockup6 dotted cylinder+base (fixed, mid-pavement) + saddle cap (at the cup) */
    pd->graphics->setDrawMode(kDrawModeCopy);
    pd->graphics->drawBitmap(g_tower_body, sx - 15, body_top - cam, kBitmapUnflipped);
    pd->graphics->drawBitmap(g_tower_cap,  sx - 15, cap_y - cam,   kBitmapUnflipped);
}

static void hud_panel(const char* msg, int cy) {
    int w = pd->graphics->getTextWidth(g_font, msg, strlen(msg), kASCIIEncoding, 0);
    int pw = w + 18, ph = 22, px = 200 - pw/2, py = cy - ph/2;
    pd->graphics->fillRect(px, py, pw, ph, kColorBlack);
    pd->graphics->drawRect(px, py, pw, ph, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);   /* white glyphs on the black panel */
    pd->graphics->drawText(msg, strlen(msg), kASCIIEncoding, 200 - w/2, cy - 8);
    pd->graphics->setDrawMode(kDrawModeCopy);
}

static void blit_plane(LCDBitmap* img, float offset, int blit_y) {
    /* draw a 1200px-wide, transparent-sky terrain plane wrapped across the world,
     * at horizontal `offset` (a jump-free integrated parallax accumulator) and blit_y. */
    if (!img) return;
    int base = -(int)offset;
    pd->graphics->drawBitmap(img, base, blit_y, kBitmapUnflipped);
    pd->graphics->drawBitmap(img, base + WORLD_W, blit_y, kBitmapUnflipped);
    pd->graphics->drawBitmap(img, base - WORLD_W, blit_y, kBitmapUnflipped);
}

/* Top-left HUD chip: an arrow pointing at the landing tower while flying, or a
 * small "descend" mark (filled down-triangle) when lined up over the pad.
 * Replaces the old FPS counter. Pure direction math lives in nav.c. */
#define ARROW_CX     14      /* chip center (screen px) */
#define ARROW_CY     14
#define ARROW_R      9       /* shaft half-length / arrow reach */
#define ARROW_CENTER_TOL 16.0f
static void draw_landing_arrow(void) {
    if (!(g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT))
        return;
    float tx = g_tower.x, ty = tower_cup_y(&g_tower);
    if (nav_is_centered(g_wx, tx, (float)WORLD_W, ARROW_CENTER_TOL)) {
        /* "descend" mark: a small filled down-triangle in the chip */
        pd->graphics->fillTriangle(ARROW_CX - 5, ARROW_CY - 4,
                                   ARROW_CX + 5, ARROW_CY - 4,
                                   ARROW_CX,     ARROW_CY + 6, kColorWhite);
        return;
    }
    float a  = nav_angle_to(g_wx, g_v.y, tx, ty, (float)WORLD_W);
    float ca = cosf(a), sa = sinf(a);
    int tipx  = ARROW_CX + (int)(ARROW_R * ca);
    int tipy  = ARROW_CY + (int)(ARROW_R * sa);
    int tailx = ARROW_CX - (int)(ARROW_R * ca);
    int taily = ARROW_CY - (int)(ARROW_R * sa);
    pd->graphics->drawLine(tailx, taily, tipx, tipy, 2, kColorWhite);   /* shaft */
    float spread = 0.5f;     /* arrowhead half-angle (radians): barbs splay back from the tip */
    int b1x = tipx - (int)(6.0f * cosf(a - spread));
    int b1y = tipy - (int)(6.0f * sinf(a - spread));
    int b2x = tipx - (int)(6.0f * cosf(a + spread));
    int b2y = tipy - (int)(6.0f * sinf(a + spread));
    pd->graphics->fillTriangle(tipx, tipy, b1x, b1y, b2x, b2y, kColorWhite); /* head */
}

static void render(void) {
    if (g_state == GS_SPLASH) {
        pd->graphics->clear(kColorBlack);
        if (g_title_splash) pd->graphics->drawBitmap(g_title_splash, 0, 0, kBitmapUnflipped);
        return;
    }
    if (g_state == GS_ABOUT) {
        pd->graphics->clear(kColorBlack);
        if (g_title_about) pd->graphics->drawBitmap(g_title_about, 0, 0, kBitmapUnflipped);
        return;
    }
    if (g_state == GS_MENU) {
        pd->graphics->clear(kColorBlack);
        LCDBitmap* mbg = (g_menu_sel == 0) ? g_title_menu_start : g_title_menu_about;
        if (mbg) pd->graphics->drawBitmap(mbg, 0, 0, kBitmapUnflipped);
        if (g_balloon) {
            int dxi = MENU_SWAY[(g_menu_sway / 6) % 8];
            int base_idx = balloon_frame_index(dxi, 1, g_flame); /* burner on */
            int idx = g_dither * 25 + base_idx;
            LCDBitmap* frame = pd->graphics->getTableBitmap(g_balloon, idx);
            if (frame) {
                pd->graphics->setDrawMode(kDrawModeCopy);
                pd->graphics->drawScaledBitmap(frame, MENU_SLOT_X, (int)g_menu_y, 2.0f, 2.0f);
            }
        }
        if (g_reset_flash > 0) hud_panel("tutorial reset", 200);
        return;
    }
    if (g_state == GS_TUTORIAL) {
        pd->graphics->clear(kColorBlack);
        if (g_title_tutorial) pd->graphics->drawBitmap(g_title_tutorial, 0, 0, kBitmapUnflipped);
        pd->graphics->setDrawMode(kDrawModeCopy);
        /* drifting wind-streaks confined to the windy band (y 64-94), game style */
        {
            static const int CLOUD_YS[2] = { 72, 88 };
            for (int i = 0; i < 2; i++) {
                int cy = CLOUD_YS[i];
                float base = g_tut_cloud + (float)(i * 85);      /* per-row phase offset */
                int spc = 170;
                for (int k = -1; k <= SCREEN_W / spc + 1; k++) {
                    int hx = (int)(fmodf(base + (float)(k * spc), (float)(SCREEN_W + 80))) - 40;
                    if (hx < -70 || hx > SCREEN_W + 10) continue;
                    float length = 44.0f; int n = (int)(length / 6); if (n < 2) n = 2;
                    for (int d = 0; d < n; d++) {
                        float tt = (float)d / (n - 1);
                        int x = (int)(hx - (tt * length));        /* tail trails left, drifts right */
                        int y = cy + (int)(2 * sinf(tt * 4 + hx * 0.02f));
                        int th = (tt > 0.18f && tt < 0.82f) ? 2 : 1;
                        if (d % 2 == 0) pd->graphics->fillRect(x, y, 5, th, kColorWhite);
                    }
                    pd->graphics->fillRect(hx + 5, cy - 2, 3, 1, kColorWhite);
                    pd->graphics->fillRect(hx + 11, cy + 1, 2, 1, kColorWhite);
                }
            }
        }
        /* launch pad the balloon starts on; slides off-screen right after liftoff
         * (mirrors draw_tower(): body 40px tall at sx-15, cap at sx-15) */
        if (g_tut_pad_x < 420.0f && g_tower_body && g_tower_cap) {
            int psx = (int)g_tut_pad_x;
            pd->graphics->drawBitmap(g_tower_body, psx - 15, 232 - TOWER_BODY_H, kBitmapUnflipped); /* foot at ~232 */
            pd->graphics->drawBitmap(g_tower_cap,  psx - 15, 168,                kBitmapUnflipped); /* cap top at 168 */
        }
        if (g_tut_gun_active && g_gun_img)
            pd->graphics->drawBitmap(g_gun_img, (int)g_tut_gun_x, TUT_GUN_Y - 22, kBitmapUnflipped);
        if (g_missile_img) for (int m = 0; m < MAX_MISSILES; m++) if (g_tut_missiles[m].alive)
            pd->graphics->drawBitmap(g_missile_img, (int)g_tut_missiles[m].x - 4,
                                     (int)g_tut_missiles[m].y - 7, kBitmapUnflipped);
        if (g_balloon) {
            int dxi = MENU_SWAY[(g_tut_sway / 6) % 8];
            int idx = g_dither * 25 + balloon_frame_index(dxi, g_tut_burner, g_flame);
            LCDBitmap* frame = pd->graphics->getTableBitmap(g_balloon, idx);
            if (frame) pd->graphics->drawBitmap(frame, (int)g_tut_x, (int)g_tut_y, kBitmapUnflipped);
        }
        if (g_tut_flash > 0 && g_tutorial_go)
            pd->graphics->drawBitmap(g_tutorial_go, (int)TUT_PLAQUE_X0, (int)TUT_PLAQUE_Y0, kBitmapUnflipped);
        return;
    }
    /* vertical camera: follow the balloon's world-y, show a 240px window of the
     * 300px world (=80%), clamped so it never over-scrolls top/bottom. */
    g_camy = g_v.y - SCREEN_H * 0.5f;
    if (g_camy < 0.0f) g_camy = 0.0f;
    else if (g_camy > (float)(WORLD_H - SCREEN_H)) g_camy = (float)(WORLD_H - SCREEN_H);
    /* integrate the camera's per-frame delta into parallax offsets so they never
     * jump when camera-x wraps the world seam. */
    float dcx = camera_wrap_delta(g_cam.x - g_prev_camx, (float)WORLD_W);
    g_prev_camx = g_cam.x;
    g_par_far  = camera_world_wrap(g_par_far  + dcx * 0.50f, (float)WORLD_W);
    g_par_near = camera_world_wrap(g_par_near + dcx * 0.85f, (float)WORLD_W);
    g_par_stars += dcx * 0.12f; while (g_par_stars < 0) g_par_stars += SCREEN_W; while (g_par_stars >= SCREEN_W) g_par_stars -= SCREEN_W;
    g_par_moon  += dcx * 0.04f; while (g_par_moon  < 0) g_par_moon  += SCREEN_W; while (g_par_moon  >= SCREEN_W) g_par_moon  -= SCREEN_W;
    pd->graphics->clear(kColorBlack);
    /* two terrain parallax planes: far (slow, less vertical) behind near (fast) */
    blit_plane(g_terrain_far,  g_par_far,  (int)(TERRAIN_BLIT_Y - g_camy * 0.9f));
    blit_plane(g_terrain_near, g_par_near, (int)(TERRAIN_BLIT_Y - g_camy));
    /* moon: slow autonomous drift + faint H/V parallax (most distant plane) */
    if (g_moon) {
        int my = 40 - (int)(g_camy * 0.15f);
        int mx = (int)(g_moon_x - g_par_moon);
        mx = ((mx % SCREEN_W) + SCREEN_W) % SCREEN_W;
        pd->graphics->drawBitmap(g_moon, mx, my, kBitmapUnflipped);
        if (mx > SCREEN_W - 34) pd->graphics->drawBitmap(g_moon, mx - SCREEN_W, my, kBitmapUnflipped);
    }
    /* twinkling stars on a distant plane (faint H/V parallax) */
    for (int i=0;i<STAR_COUNT;i++){
        int x=(((g_stars[i].x - (int)g_par_stars) % SCREEN_W) + SCREEN_W) % SCREEN_W;
        int y=g_stars[i].y - (int)(g_camy*0.3f), p=decor_twinkle_phase(&g_stars[i], g_t);
        if(p==0) pd->graphics->fillRect(x,y,1,1,kColorWhite);
        else if(p==1){pd->graphics->drawLine(x,y-2,x,y+2,1,kColorWhite);pd->graphics->drawLine(x-2,y,x+2,y,1,kColorWhite);}
        else if(p==2){pd->graphics->drawLine(x,y-3,x,y+3,1,kColorWhite);pd->graphics->drawLine(x-3,y,x+3,y,1,kColorWhite);
                      pd->graphics->fillRect(x-2,y-2,1,1,kColorWhite);pd->graphics->fillRect(x+2,y+2,1,1,kColorWhite);}
        else {pd->graphics->drawLine(x,y-4,x,y-1,1,kColorWhite);pd->graphics->drawLine(x,y+1,x,y+4,1,kColorWhite);
              pd->graphics->drawLine(x-4,y,x-1,y,1,kColorWhite);pd->graphics->drawLine(x+1,y,x+4,y,1,kColorWhite);}
    }
    /* tapered/bending wind-stream streaks per clouded echelon */
    for (int i=0;i<g_wind.count;i++){
        if (g_wind.base[i]==0.0f) continue;
        int cy=(int)g_wind.center[i] - (int)g_camy; if(cy<6||cy>232) continue;
        int dir=(g_wind.base[i]>=0.0f)?1:-1; float strength=fabsf(g_wind.base[i])/3.2f; if(strength>1.2f)strength=1.2f;
        int spc=300, nstreak=WORLD_W/spc+1;
        for(int k=0;k<nstreak;k++){
            float wx=camera_world_wrap((float)(k*spc)+g_cloud[i], WORLD_W);
            int hx=(int)camera_screen_x(&g_cam,wx);
            if(hx<-70||hx>SCREEN_W+10) continue;
            float length=40+strength*40; int n=(int)(length/6); if(n<2)n=2;
            for(int d=0;d<n;d++){
                float tt=(float)d/(n-1);
                int x=(int)(hx-dir*(tt*length));
                int y=cy+(int)(3*sinf(tt*4+hx*0.02f));
                int th=(tt>0.18f&&tt<0.82f)?2:1;
                if(d%2==0) pd->graphics->fillRect(x,y,5,th,kColorWhite);
            }
            pd->graphics->fillRect(hx+dir*5, cy-2, 3,1, kColorWhite);
            pd->graphics->fillRect(hx+dir*11, cy+1, 2,1, kColorWhite);
        }
    }
    draw_tower();
    /* AA guns on the pavement (world-X via camera; bottom seated at TOWER_BASE_Y) */
    if (g_gun_img) for (int i = 0; i < g_gun_count; i++) {
        int sx = (int)camera_screen_x(&g_cam, g_guns[i].x);
        if (sx < -GUN_W || sx > SCREEN_W + GUN_W) continue;
        pd->graphics->setDrawMode(kDrawModeCopy);
        pd->graphics->drawBitmap(g_gun_img, sx - GUN_W/2,
                                 (int)(TOWER_BASE_Y - GUN_H - g_camy), kBitmapUnflipped);
    }
    /* rising missiles */
    if (g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT || g_expl_frame >= 0) {
        if (g_missile_img) for (int m = 0; m < MAX_MISSILES; m++) if (g_missiles[m].alive) {
            int sx = (int)camera_screen_x(&g_cam, g_missiles[m].x);
            if (sx < -MISSILE_W || sx > SCREEN_W + MISSILE_W) continue;
            pd->graphics->setDrawMode(kDrawModeCopy);
            pd->graphics->drawBitmap(g_missile_img, sx - MISSILE_W/2,
                                     (int)(g_missiles[m].y - g_camy) - MISSILE_H/2, kBitmapUnflipped);
        }
    }
    /* balloon — or the explosion in its place while it plays */
    if (g_expl_frame >= 0 && g_expl) {
        LCDBitmap* ef = pd->graphics->getTableBitmap(g_expl, g_expl_frame);
        if (ef) {
            int sx = (int)camera_screen_x(&g_cam, g_wx);
            pd->graphics->setDrawMode(kDrawModeCopy);
            pd->graphics->drawBitmap(ef, sx - EXPL_CELL/2,
                                     (int)(g_v.y - g_camy) - EXPL_CELL/2, kBitmapUnflipped);
        }
    } else if (g_balloon) {
        int burner_vis = (g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT) &&
            (!pd->system->isCrankDocked()) && (fabsf(g_frame_dcrank) > 2.0f);
        int dxi = balloon_basket_index(g_vx);
        int base_idx = balloon_frame_index(dxi, burner_vis, g_flame); /* 0..24 */
        int idx = g_dither * 25 + base_idx;                           /* phase block; 0..74 */
        LCDBitmap* frame = pd->graphics->getTableBitmap(g_balloon, idx);
        if (frame) {
            int sx = (int)camera_screen_x(&g_cam, g_wx);
            pd->graphics->setDrawMode(kDrawModeCopy);
            pd->graphics->drawBitmap(frame, sx - CELL_W/2, (int)(g_v.y - g_camy) - CELL_H/2, kBitmapUnflipped);
        }
    }
    /* HUD prompts */
    if (g_state == GS_PRELAUNCH)
        hud_panel("Press A to launch", 112);
    else if (g_state == GS_HOLD)
        hud_panel(g_seq < 30 ? "3" : g_seq < 60 ? "2" : g_seq < 90 ? "1" : "READY!", 96);
    else if (g_state == GS_WIN)
        hud_panel("LANDED!  (A) again", 112);
    else if (g_state == GS_CRASH)
        hud_panel("CRASH!  (A) retry", 112);
    else if (g_state == GS_FLIGHT && pd->system->isCrankDocked())
        hud_panel("Paused - undock crank", 112);
    /* health bar (top-right), shown once airborne */
    if (g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT || g_expl_frame >= 0) {
        int bw = 100, bx = SCREEN_W - bw - 10, by = 6;
        pd->graphics->fillRect(bx - 1, by - 1, bw + 2, 8, kColorBlack);
        pd->graphics->drawRect(bx - 1, by - 1, bw + 2, 8, kColorWhite);
        int hw = g_health < 0 ? 0 : (g_health > MAX_HEALTH ? MAX_HEALTH : g_health);
        pd->graphics->fillRect(bx, by, hw, 6, kColorWhite);
    }
    draw_landing_arrow();
}

#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)
/* Debug screenshot harness: render specific game states and dump the raw 1-bit
 * framebuffer (LCD_ROWSIZE*240 bytes) to host files, so the actual emulator
 * output can be compared to the Python mock. Built with: make UDEFS=-DBALLY_SHOT */
static void dump_raw(const char* path) {
    uint8_t* fb = pd->graphics->getFrame();
    FILE* f = fopen(path, "wb");
    if (!f) { pd->system->logToConsole("shot: fopen failed %s", path); return; }
    fwrite(fb, 1, LCD_ROWSIZE * 240, f); fclose(f);
    pd->system->logToConsole("shot: wrote %s", path);
}
#define SHOT_DIR "/Users/ip/projects/playdate-bally/bally/assets_work/out_c/"
static int g_shotn = 0;
static int shot_update(void) {
    g_shotn++;
    if (g_shotn == 3) {                 /* prelaunch state */
        reset_game(); render();
        dump_raw(SHOT_DIR "prelaunch.raw");
    } else if (g_shotn == 6) {          /* mid-flight (free, centered) over terrain */
        g_state = GS_FLIGHT; g_wx = 150.0f; g_vx = 1.2f; g_v.y = 110.0f; g_v.heat = 0.7f; g_flame = 1;
        g_cam.x = camera_world_wrap(g_wx - SCREEN_W * 0.5f, (float)WORLD_W);
        spawn_guns();
        g_guns[0].x = g_wx + 40.0f;                                  /* a gun just ahead, on screen */
        g_missiles[0].x = g_wx + 40.0f; g_missiles[0].y = 170.0f; g_missiles[0].alive = 1;
        g_health = 66;                                               /* partial bar to verify the HUD */
        render();
        dump_raw(SHOT_DIR "flight.raw");
    } else if (g_shotn == 9) {          /* launch: tower extended to apex (rod telescoped out) */
        reset_game(); g_state = GS_HOLD; g_tower.ext = 1.0f; g_seq = 45;
        g_v.y = tower_cup_y(&g_tower); g_wx = g_tower.x;
        g_cam.x = camera_world_wrap(g_tower.x - LAUNCH_RIGHT_X, (float)WORLD_W);
        render();
        dump_raw(SHOT_DIR "extended.raw");
    }
    return 1;
}
#endif

/* landing / crash test for the flying states. Returns 1 if it ended the flight.
 * On a good landing the pad nudges (<=half a balloon) to seat the balloon centered. */
static int check_land_crash(void) {
    float dx = camera_wrap_delta(g_wx - g_tower.x, (float)WORLD_W);
    LandResult lr = tower_landing_check(&g_tower, g_wx, g_v.y, g_vx, g_v.vy,
                                        WORLD_W, LAND_GRAB_TOL, LAND_Y_TOL, LAND_VMAX);
    (void)dx;
    if (lr == LAND_OK) { g_state = GS_CATCH; return 1; }   /* smooth catch animates in GS_CATCH */
    if (lr == LAND_CRASH || g_v.y >= GROUND_Y) { g_state = GS_CRASH; return 1; }
    return 0;
}

/* Fire guns, advance missiles, test balloon collision. On the first hit (while not
 * already exploding) cull the missile, subtract health, and start the explosion. */
static void tick_enemies(void) {
    for (int i = 0; i < g_gun_count; i++) {
        if (gun_step(&g_guns[i], (float)WORLD_W)) {
            for (int m = 0; m < MAX_MISSILES; m++) if (!g_missiles[m].alive) {
                g_missiles[m].x = g_guns[i].x;
                g_missiles[m].y = (float)(TOWER_BASE_Y - GUN_H);  /* from the muzzle */
                g_missiles[m].vy = MISSILE_VY; g_missiles[m].alive = 1;
                break;
            }
            audio_gun_fire();
        }
    }
    for (int m = 0; m < MAX_MISSILES; m++) if (g_missiles[m].alive) {
        missile_step(&g_missiles[m], 0.0f);   /* cull once risen above the world top (y<0) */
        if (g_expl_frame < 0 &&
            missile_hits(g_missiles[m].x, g_missiles[m].y, g_wx, g_v.y, HIT_R, (float)WORLD_W)) {
            g_missiles[m].alive = 0;
            g_health -= MISSILE_DMG;
            g_expl_frame = 0; g_expl_div = 0;   /* begin balloon explosion */
            audio_explosion();
        }
    }
}

/* Advance the explosion. On the final frame: health<=0 -> GS_CRASH (game over);
 * otherwise resume flight. */
static void tick_explosion(void) {
    if (g_expl_frame < 0) return;
    if (++g_expl_div >= EXPL_DIV) {
        g_expl_div = 0;
        if (++g_expl_frame >= EXPL_FRAMES) {
            g_expl_frame = -1;
            if (g_health <= 0) g_state = GS_CRASH;
        }
    }
}

/* one step of free flight: vertical burner physics + gusty wind + cloud drift +
 * sprite animation cadences. Camera is set by the caller per game state. */
static void fly_step(int burner_on) {
    physics_vertical_step(&g_pc, &g_v, burner_on);
    g_vx = wind_step_vx(&g_wind, g_vx, g_v.y, g_t);
    g_wx = camera_world_wrap(g_wx + g_vx, WORLD_W);
    for (int i = 0; i < g_wind.count; i++)
        g_cloud[i] = camera_world_wrap(g_cloud[i] + wind_eff(&g_wind, i, g_t), WORLD_W);
    g_t += 1.0f;
    if (++g_flame_div >= 4) { g_flame_div = 0; g_flame = (g_flame + 1) & 3; }
    if (++g_dither_div >= 6) { g_dither_div = 0; g_dither = (g_dither + 1) % 3; }
}

static int update(void* ud) {
    (void)ud;
#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)
    return shot_update();
#endif
    /* Read the crank ONCE per frame. getCrankChange() returns the delta since
     * the last call, so a single read is shared by flight input, the burner
     * whoosh, the tutorial, the render burner sprite, and the music driver
     * below — any extra in-frame read would steal this delta from the others. */
    float dcrank = pd->system->getCrankChange();
    g_frame_dcrank = dcrank;

    /* Adaptive music runs in EVERY state (front-end, tutorial, flight). */
    float wind_i  = fabsf(g_vx) / 3.2f;     if (wind_i  > 1.0f) wind_i  = 1.0f;
    float crank_i = fabsf(dcrank) / 30.0f;  if (crank_i > 1.0f) crank_i = 1.0f;
    float intensity = wind_i * 0.6f + crank_i * 0.4f;
    if (intensity > 1.0f) intensity = 1.0f;
    int on_pad = (g_state == GS_PRELAUNCH) || (g_state == GS_TUTORIAL && !g_tut_lifted);
    int missiles = 0;
    for (int mi = 0; mi < MAX_MISSILES; mi++) if (g_missiles[mi].alive) { missiles = 1; break; }
    int mscene;
    if (g_state == GS_SPLASH || g_state == GS_MENU || g_state == GS_ABOUT) mscene = MUSIC_INTRO;
    else if (g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT ||
             g_state == GS_CATCH || g_state == GS_WIN || g_state == GS_CRASH) mscene = MUSIC_GAME;
    else mscene = MUSIC_SILENT;   /* tutorial + prelaunch/scroll/extend/hold */
    music_tick(mscene, intensity, on_pad, missiles);

    if (g_state == GS_SPLASH || g_state == GS_MENU || g_state == GS_ABOUT || g_state == GS_TUTORIAL) {
        PDButtons fp; pd->system->getButtonState(NULL, &fp, NULL);
        if (g_state == GS_SPLASH) {
            if (++g_splash_t >= SPLASH_FRAMES || (fp & kButtonA)) g_state = GS_MENU;
        } else if (g_state == GS_MENU) {
            if (fp & kButtonUp)   g_menu_sel = 0;
            if (fp & kButtonDown) g_menu_sel = 1;
            if (fp & kButtonA) {
                if (g_menu_sel == 0) { if (tutorial_done_flag()) reset_game(); else enter_tutorial(); }
                else { g_state = GS_ABOUT; }
            }
            if (g_b_timer > 0) g_b_timer--; else g_b_count = 0;
            if (fp & kButtonB) {
                if (g_b_count == 0) g_b_timer = 150;     /* 5s @30fps window */
                if (++g_b_count >= 10) { tutorial_clear_flag(); g_reset_flash = 60; g_b_count = 0; g_b_timer = 0; }
            }
            if (g_reset_flash > 0) g_reset_flash--;
            if (++g_flame_div >= 4) { g_flame_div = 0; g_flame = (g_flame + 1) & 3; }
            if (++g_dither_div >= 6) { g_dither_div = 0; g_dither = (g_dither + 1) % 3; }
            g_menu_sway++;
            /* Crank lifts the balloon (whoosh while cranking); release lets it sink
             * with the flight physics, settling back on its menu slot. */
            int mburner = (!pd->system->isCrankDocked()) && (fabsf(dcrank) > 2.0f);
            audio_burner(mburner, dcrank);
            VerticalState mvs = { g_menu_y, g_menu_vy, g_menu_heat };
            physics_vertical_step(&g_menu_pc, &mvs, mburner);
            g_menu_y = mvs.y; g_menu_vy = mvs.vy; g_menu_heat = mvs.heat;
        } else if (g_state == GS_ABOUT) {
            if (fp & (kButtonA | kButtonB)) g_state = GS_MENU;
        } else if (g_state == GS_TUTORIAL) {
            float tdc = dcrank;                          /* shared single per-frame crank read */
            int burner = (!pd->system->isCrankDocked()) && (fabsf(tdc) > 2.0f);
            g_tut_burner = burner;                       /* drives the flame sprite in render */
            audio_burner(burner, tdc);                   /* whoosh only while cranking */
            if (g_tut_flash > 0) {
                if (--g_tut_flash == 0) { tutorial_write_flag(); reset_game(); render(); return 1; }
            } else {
                VerticalState vs = { g_tut_y, g_tut_vy, g_tut_heat };
                physics_vertical_step(&g_tut_pc, &vs, burner);
                g_tut_y = vs.y; g_tut_vy = vs.vy; g_tut_heat = vs.heat;
                float target = tutorial_wind_target(g_tut_y);
                g_tut_vx += (target - g_tut_vx) * 0.05f;
                g_tut_x += g_tut_vx;
                if (g_tut_x < 6.0f) g_tut_x = 6.0f;
                if (g_tut_x > 360.0f) g_tut_x = 360.0f;
                if (!g_tut_lifted && g_tut_y > 112.0f) { g_tut_y = 112.0f; g_tut_vy = 0.0f; } /* rest on pad cap */
                if (!g_tut_lifted && g_tut_x > TUT_START_X + 12.0f) g_tut_lifted = 1;   /* wind pushed it off the pad */
                if (g_tut_lifted && g_tut_pad_x < 460.0f) g_tut_pad_x += (470.0f - g_tut_pad_x) * 0.03f + 0.4f; /* ease off-screen right */
                g_tut_cloud += 0.6f;   /* wind-streak drift */
                if (!g_tut_gun_active && g_tut_x > TUT_START_X + 8.0f) g_tut_gun_active = 1;
                if (g_tut_gun_active) {
                    if (g_tut_gun_x < 10.0f) g_tut_gun_x += 3.0f;
                    else if (++g_tut_fire_t >= TUT_FIRE) {
                        g_tut_fire_t = 0;
                        for (int m = 0; m < MAX_MISSILES; m++) if (!g_tut_missiles[m].alive) {
                            g_tut_missiles[m].x = g_tut_gun_x + GUN_W/2.0f - MISSILE_W/2.0f;
                            g_tut_missiles[m].y = (float)(TUT_GUN_Y - GUN_H);   /* muzzle (gun top) */
                            g_tut_missiles[m].vy = -1.3f; g_tut_missiles[m].alive = 1; break;
                        }
                    }
                    for (int m = 0; m < MAX_MISSILES; m++) if (g_tut_missiles[m].alive)
                        missile_step(&g_tut_missiles[m], -20.0f);
                }
                if (tutorial_reached_plaque(g_tut_x + CELL_W/2.0f, g_tut_y + 8.0f))
                    g_tut_flash = TUT_GO_FLASH;
                if (++g_flame_div >= 4) { g_flame_div = 0; g_flame = (g_flame + 1) & 3; }
                if (++g_dither_div >= 6) { g_dither_div = 0; g_dither = (g_dither + 1) % 3; }
                g_tut_sway++;
            }
        }
        render();
        return 1;
    }
    g_moon_x -= 0.03f; if (g_moon_x < 0.0f) g_moon_x += SCREEN_W;  /* slow moon drift */

    PDButtons pushed;
    pd->system->getButtonState(NULL, &pushed, NULL);
    int flying = (g_state == GS_LAUNCHED || g_state == GS_CENTERING || g_state == GS_FLIGHT);
    int burner_on = flying && (!pd->system->isCrankDocked()) && (fabsf(dcrank) > 2.0f);
    audio_burner(burner_on, dcrank);

    /* Crank docked pauses only free flight. */
    if (g_state == GS_FLIGHT && pd->system->isCrankDocked()) { render(); return 1; }

    /* While exploding, freeze the sim and play the animation to completion,
     * regardless of state (a hit can coincide with a landing/crash transition). */
    if (g_expl_frame >= 0) {
        tick_explosion();
        g_t += 1.0f;
        render();
        return 1;
    }

    switch (g_state) {
    case GS_SPLASH:
    case GS_MENU:
    case GS_ABOUT:
    case GS_TUTORIAL:
        break;                                     /* handled at the top of update() */
    case GS_PRELAUNCH:                              /* balloon parked on the retracted tower */
        g_v.y = tower_cup_y(&g_tower); g_wx = g_tower.x;
        if (pushed & kButtonA) g_state = GS_SCROLL;
        break;
    case GS_SCROLL: {                              /* pan the tower to the right edge */
        g_v.y = tower_cup_y(&g_tower); g_wx = g_tower.x;
        float target = camera_world_wrap(g_tower.x - LAUNCH_RIGHT_X, (float)WORLD_W);
        float d = camera_wrap_delta(target - g_cam.x, (float)WORLD_W);
        if (fabsf(d) < 2.0f) { g_cam.x = target; g_state = GS_EXTEND; }
        else g_cam.x = camera_world_wrap(g_cam.x + d * 0.12f, (float)WORLD_W);
        break;
    }
    case GS_EXTEND:                                /* rod extends, balloon rides to the stratosphere */
        g_tower.ext += EXTEND_RATE; if (g_tower.ext >= 1.0f) { g_tower.ext = 1.0f; g_state = GS_HOLD; g_seq = 0; }
        g_v.y = tower_cup_y(&g_tower); g_wx = g_tower.x;
        g_cam.x = camera_world_wrap(g_tower.x - LAUNCH_RIGHT_X, (float)WORLD_W);
        break;
    case GS_HOLD:                                  /* hold at apex: 3-2-1-READY */
        g_v.y = tower_cup_y(&g_tower); g_wx = g_tower.x;
        g_cam.x = camera_world_wrap(g_tower.x - LAUNCH_RIGHT_X, (float)WORLD_W);
        if (++g_seq >= HOLD_FRAMES) {              /* release into flight */
            g_v.vy = 0.0f; g_vx = 0.0f; g_release_wx = g_wx; g_seq = 0;
            g_hold_x = LAUNCH_RIGHT_X; g_tower.phase = TOWER_READY; g_state = GS_LAUNCHED;
        }
        break;
    case GS_LAUNCHED:                              /* free; held at right edge; rod retracts */
        fly_step(burner_on); tick_enemies();
        if (g_tower.ext > 0.0f) { g_tower.ext -= RETRACT_RATE; if (g_tower.ext < 0.0f) g_tower.ext = 0.0f; }
        g_cam.x = camera_world_wrap(g_wx - g_hold_x, (float)WORLD_W);
        if (check_land_crash()) break;
        if (++g_seq >= CENTER_T ||
            fabsf(camera_wrap_delta(g_wx - g_release_wx, (float)WORLD_W)) >= CENTER_DX)
            g_state = GS_CENTERING;
        break;
    case GS_CENTERING:                             /* rubber-band slide to center */
        fly_step(burner_on); tick_enemies();
        if (g_tower.ext > 0.0f) { g_tower.ext -= RETRACT_RATE; if (g_tower.ext < 0.0f) g_tower.ext = 0.0f; }
        g_hold_x += (CENTER_X - g_hold_x) * 0.04f;
        g_cam.x = camera_world_wrap(g_wx - g_hold_x, (float)WORLD_W);
        if (check_land_crash()) break;
        if (fabsf(g_hold_x - CENTER_X) < 2.0f) g_state = GS_FLIGHT;
        break;
    case GS_FLIGHT:
        fly_step(burner_on); tick_enemies();
        camera_follow(&g_cam, g_wx);
        check_land_crash();
        break;
    case GS_CATCH: {                               /* smooth, slow touchdown: pad eases under
                                                    * the balloon while it settles onto the saddle */
        float d = camera_wrap_delta(g_wx - g_tower.x, (float)WORLD_W);
        g_tower.x = camera_world_wrap(g_tower.x + d * 0.10f, (float)WORLD_W);
        g_v.y += (TOWER_PAD_Y - g_v.y) * 0.10f;
        g_v.vy *= 0.80f; g_vx *= 0.80f;
        camera_follow(&g_cam, g_wx);
        g_t += 1.0f;
        if (++g_flame_div >= 4) { g_flame_div = 0; g_flame = (g_flame + 1) & 3; }
        if (fabsf(d) < 0.6f && fabsf(g_v.y - TOWER_PAD_Y) < 0.6f) g_state = GS_WIN;
        break;
    }
    case GS_WIN:
    case GS_CRASH:
        if (pushed & kButtonA) g_state = GS_MENU;
        break;
    }

    render();
    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* p, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    if (event == kEventInit) {
        pd = p;
        const char* err = NULL;
        g_balloon = pd->graphics->loadBitmapTable("images/balloon", &err);
        if (!g_balloon) pd->system->logToConsole("balloon table load failed: %s", err ? err : "?");
        g_terrain_far = pd->graphics->loadBitmap("images/terrain-far", &err);
        if (!g_terrain_far) pd->system->logToConsole("terrain-far load failed: %s", err ? err : "?");
        g_terrain_near = pd->graphics->loadBitmap("images/terrain-near", &err);
        if (!g_terrain_near) pd->system->logToConsole("terrain-near load failed: %s", err ? err : "?");
        g_moon = pd->graphics->loadBitmap("images/moon", &err);
        if (!g_moon) pd->system->logToConsole("moon load failed: %s", err ? err : "?");
        g_tower_body = pd->graphics->loadBitmap("images/tower-body", &err);
        if (!g_tower_body) pd->system->logToConsole("tower-body load failed: %s", err ? err : "?");
        g_tower_cap = pd->graphics->loadBitmap("images/tower-cap", &err);
        if (!g_tower_cap) pd->system->logToConsole("tower-cap load failed: %s", err ? err : "?");
        g_gun_img = pd->graphics->loadBitmap("images/gun", &err);
        if (!g_gun_img) pd->system->logToConsole("gun load failed: %s", err ? err : "?");
        g_missile_img = pd->graphics->loadBitmap("images/missile", &err);
        if (!g_missile_img) pd->system->logToConsole("missile load failed: %s", err ? err : "?");
        g_expl = pd->graphics->loadBitmapTable("images/explosion", &err);
        if (!g_expl) pd->system->logToConsole("explosion table load failed: %s", err ? err : "?");
        g_title_splash = pd->graphics->loadBitmap("images/title-splash", &err);
        if (!g_title_splash) pd->system->logToConsole("title-splash load failed: %s", err ? err : "?");
        g_title_menu_start = pd->graphics->loadBitmap("images/title-menu-start", &err);
        if (!g_title_menu_start) pd->system->logToConsole("title-menu-start load failed: %s", err ? err : "?");
        g_title_menu_about = pd->graphics->loadBitmap("images/title-menu-about", &err);
        if (!g_title_menu_about) pd->system->logToConsole("title-menu-about load failed: %s", err ? err : "?");
        g_title_about = pd->graphics->loadBitmap("images/title-about", &err);
        if (!g_title_about) pd->system->logToConsole("title-about load failed: %s", err ? err : "?");
        g_title_tutorial = pd->graphics->loadBitmap("images/title-tutorial", &err);
        if (!g_title_tutorial) pd->system->logToConsole("title-tutorial load failed: %s", err ? err : "?");
        g_tutorial_go = pd->graphics->loadBitmap("images/tutorial-go", &err);
        if (!g_tutorial_go) pd->system->logToConsole("tutorial-go load failed: %s", err ? err : "?");
        decor_init_stars(g_stars);
        audio_init(pd);
        music_init(pd);
        g_pc = physics_default_config();
        g_pc.top_y = 34.0f; g_pc.ground_y = 239.0f;   /* world-y vertical flight range */
        g_menu_pc = physics_default_config();
        g_menu_pc.top_y = 20.0f; g_menu_pc.ground_y = (float)MENU_SLOT_Y;  /* rest = slot; rises only */
        g_menu_y = (float)MENU_SLOT_Y; g_menu_vy = 0.0f; g_menu_heat = 0.0f;
        init_wind();
        g_cam.world_w = (float)WORLD_W; g_cam.screen_w = (float)SCREEN_W;
        g_cam.margin = (float)CAM_MARGIN;
        g_font = pd->graphics->loadFont("/System/Fonts/Asheville-Sans-14-Bold.pft", NULL);
        pd->graphics->setFont(g_font);
        g_state = GS_SPLASH; g_splash_t = 0; g_menu_sel = 0;
        pd->display->setRefreshRate(30.0f);
        pd->system->setUpdateCallback(update, pd);
    }
    if (event == kEventPause) { audio_burner(0, 0); music_set_paused(1); }   /* silence in the system menu */
    if (event == kEventResume) music_set_paused(0);
    return 0;
}
