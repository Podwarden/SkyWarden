#ifndef BALLY_TOWER_H
#define BALLY_TOWER_H

/* Launch tower / landing pad. Pure; depends only on camera (wrap math) + math.h.
 * Launch sequence: DOCKED --launch()--> EXTENDING (cup rises to apex) -->
 * RETRACTING (cup lowers to pad AND tower drives x_launch -> x_land) --> READY. */
typedef enum { TOWER_DOCKED, TOWER_EXTENDING, TOWER_RETRACTING, TOWER_READY } TowerPhase;

typedef struct {
    float x;          /* current world-X of the tower */
    float x_launch;   /* world-X at launch */
    float x_land;     /* world-X it drives to while retracting */
    float ext;        /* extension 0..1 (0 = collapsed pad, 1 = full apex) */
    TowerPhase phase;
    float pad_y;      /* balloon-center y when collapsed (cup at landing height) */
    float apex_y;     /* balloon-center y at full extension */
    float ext_rate;   /* ext gained per frame while extending */
    float ret_rate;   /* ext lost per frame while retracting */
} Tower;

typedef enum { LAND_NONE, LAND_OK, LAND_CRASH } LandResult;

/* balloon-center y while slaved to the cup: lerp(pad_y, apex_y, ext) */
float tower_cup_y(const Tower* t);

/* Begin launch (only from DOCKED). */
void  tower_launch(Tower* t);

/* Advance one frame. Returns 1 on the frame it becomes READY, else 0. */
int   tower_step(Tower* t, float world_w);

/* When READY, classify a touchdown attempt at balloon center (bx world, by),
 * velocities (vx,vy). Over the pad within x_tol AND at/below pad within y_tol:
 * slow => LAND_OK, too fast => LAND_CRASH. Otherwise LAND_NONE. */
LandResult tower_landing_check(const Tower* t, float bx, float by, float vx, float vy,
                               float world_w, float x_tol, float y_tol, float vmax);

#endif
