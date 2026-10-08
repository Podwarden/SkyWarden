#ifndef SKYWARDEN_TUTORIAL_H
#define SKYWARDEN_TUTORIAL_H

/* Tutorial-scene geometry in screen pixels (400x240). Must match the mockup
 * renderer (assets_work/tutorial_mockup/render_tutorial.py). Pure; no Playdate. */
#define TUT_WINDY_Y0    64.0f
#define TUT_WINDY_Y1    94.0f
#define TUT_WIND_SPEED  1.3f
#define TUT_START_X     16.0f
#define TUT_PLAQUE_X0  306.0f
#define TUT_PLAQUE_Y0   14.0f
#define TUT_PLAQUE_X1  384.0f
#define TUT_PLAQUE_Y1   40.0f

/* Rightward wind at screen-y `y`: TUT_WIND_SPEED inside [TUT_WINDY_Y0,Y1], else 0. */
float tutorial_wind_target(float y);
/* 1 if point (bx,by) (balloon center, screen px) is within the plaque rect. */
int tutorial_reached_plaque(float bx, float by);

#endif
