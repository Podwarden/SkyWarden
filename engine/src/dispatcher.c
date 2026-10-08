#include "engine.h"
#include <string.h>

static int has_extension_pt3(const char* path) {
    if (!path) return 0;
    size_t n = 0; while (path[n]) n++;
    if (n < 4) return 0;
    return (path[n-4] == '.' &&
            (path[n-3] == 'p' || path[n-3] == 'P') &&
            (path[n-2] == 't' || path[n-2] == 'T') &&
             path[n-1] == '3');
}

EngineFormat engine_detect_format(const uint8_t* buf, size_t len, const char* path) {
    if (!buf || len < 2) return ENG_FMT_UNKNOWN;
    /* PSG: 4-byte magic, check first */
    if (len >= 4 && memcmp(buf, "PSG\x1a", 4) == 0) return ENG_FMT_PSG;
    /* AY: 4-byte magic "ZXAY" */
    if (len >= 4 && memcmp(buf, "ZXAY", 4) == 0) return ENG_FMT_AY;
    /* YM: raw magic — must be checked BEFORE VTX because "YM3!" starts with "YM"
     * which would otherwise be misidentified as VTX "ym" magic. */
    if (len >= 4 &&
        (memcmp(buf, "YM2!", 4) == 0 || memcmp(buf, "YM3!", 4) == 0 ||
         memcmp(buf, "YM3b", 4) == 0 || memcmp(buf, "YM5!", 4) == 0 ||
         memcmp(buf, "YM6!", 4) == 0)) return ENG_FMT_YM;
    /* YM: LHA -lh5- wrapper (heuristic — any -lh5- LHA at top level is treated as YM) */
    if (len >= 22 && memcmp(buf + 2, "-lh5-", 5) == 0) return ENG_FMT_YM;
    /* VTX: 2-byte magic "ay" (AY-3-8910) or "ym" (YM2149) */
    if (memcmp(buf, "ay", 2) == 0 || memcmp(buf, "ym", 2) == 0) return ENG_FMT_VTX;
    /* Extension-based fallback for PT3 (no fixed magic). */
    if (has_extension_pt3(path)) return ENG_FMT_PT3;
    return ENG_FMT_UNKNOWN;
}
