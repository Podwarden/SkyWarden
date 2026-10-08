#ifndef BALLY_PHYSICS_H
#define BALLY_PHYSICS_H

/* Vertical flight model: thermal-inertia burner vs gravity, with air density
 * (and thus lift) fading toward the stratosphere. Pure; no Playdate deps.
 * Units: pixels and per-update-step. y increases downward; smaller y = higher. */
typedef struct {
    float gravity;        /* constant downward pull (px/step^2) */
    float lift;           /* max upward pull at heat=1, density=1 */
    float v_drag;         /* velocity retention per step (0..1); inertia */
    float burn_rate;      /* heat gained per step while burner on */
    float cool_rate;      /* heat lost per step while burner off */
    float density_start;  /* altFrac (0..1) where air begins to thin */
    float density_floor;  /* min air density near the top */
    float top_y;          /* highest reachable y (smallest) */
    float ground_y;       /* lowest reachable y (largest) */
} PhysicsConfig;

typedef struct {
    float y;     /* vertical position (px) */
    float vy;    /* vertical velocity (px/step), + = downward */
    float heat;  /* thermal mass, 0..1 */
} VerticalState;

PhysicsConfig physics_default_config(void);
float physics_alt_frac(const PhysicsConfig* c, float y);       /* 0 ground .. 1 top */
float physics_air_density(const PhysicsConfig* c, float alt_frac);
void  physics_vertical_step(const PhysicsConfig* c, VerticalState* s, int burner_on);

#endif
