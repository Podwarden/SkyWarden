#ifndef BALLY_NAV_H
#define BALLY_NAV_H

/* Navigation helpers for the landing-site arrow. Pure; no Playdate deps.
 * World wraps horizontally (width world_w); screen y is down-positive. */

/* Angle (radians) from the balloon (bx,by) to the tower (tx,ty), using the
 * wrap-aware shortest horizontal delta. atan2f(dy, dx) convention:
 *   tower to the right  -> ~0
 *   directly below      -> ~+pi/2   (dy>0 means tower below; y is down)
 *   tower to the left   -> ~+/-pi */
float nav_angle_to(float bx, float by, float tx, float ty, float world_w);

/* 1 if the balloon is horizontally within tol of the tower (wrap-aware), else 0. */
int   nav_is_centered(float bx, float tx, float world_w, float tol);

#endif
