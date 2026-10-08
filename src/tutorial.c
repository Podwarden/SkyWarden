#include "tutorial.h"

float tutorial_wind_target(float y) {
    return (y >= TUT_WINDY_Y0 && y <= TUT_WINDY_Y1) ? TUT_WIND_SPEED : 0.0f;
}
int tutorial_reached_plaque(float bx, float by) {
    return bx >= TUT_PLAQUE_X0 && bx <= TUT_PLAQUE_X1 &&
           by >= TUT_PLAQUE_Y0 && by <= TUT_PLAQUE_Y1;
}
