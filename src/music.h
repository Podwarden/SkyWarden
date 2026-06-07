#ifndef SKYWARDEN_MUSIC_H
#define SKYWARDEN_MUSIC_H
#include "pd_api.h"

/* Scene drives which track is audible (gradual cross-fade between them):
 *   MUSIC_INTRO  — front-end (Splash/Menu/About): Kidsoft 204 plays.
 *   MUSIC_SILENT — launch build-up (Prelaunch/Scroll/Extend/Hold) + Tutorial.
 *   MUSIC_GAME   — pad retracts onward (Launched..Crash): adaptive panimoja. */
typedef enum { MUSIC_INTRO, MUSIC_SILENT, MUSIC_GAME } MusicScene;

void music_init(PlaydateAPI* pd);                 /* load both tracks, start two faded-out sources */
void music_tick(int scene, float intensity, int on_pad, int missiles);  /* per-frame scene + adaptive */
void music_set_paused(int paused);                /* system-menu silence/restore (both sources) */
#endif
