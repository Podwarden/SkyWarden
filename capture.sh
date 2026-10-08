#!/bin/bash
# Programmatic gameplay capture. Builds the BALLY_SHOT harness, runs the Playdate
# Simulator headless to render a deterministic ~27s playthrough (every frame ->
# .raw + a sample-accurate gameplay.wav), then encodes:
#   media/gameplay.mp4  (H.264 + AAC, with sound)
#   media/gameplay.gif  (palette-optimized, silent)
#   media/shot_*.png    (key stills, 3x)
# The normal playable build is restored on exit. Requires the Playdate SDK,
# ffmpeg, and python3 (numpy + Pillow). Run: ./capture.sh
set -e
cd "$(dirname "$0")"
SIM="$HOME/Developer/PlaydateSDK/bin/Playdate Simulator.app/Contents/MacOS/Playdate Simulator"
OUT="$(pwd)/.capture"; FRAMES="$OUT/frames"; MEDIA="media"; FPS=30; SCALE=800:480
export SKYWARDEN_CAP_DIR="$OUT/"               # harness writes here (no hardcoded paths)

restore() { rm -rf build; make >/tmp/cap_restore.log 2>&1 || true; }
trap restore EXIT

# Kill any stale simulator and wait for it to actually exit (avoids a stale "done").
killall -9 "Playdate Simulator" 2>/dev/null || true
for i in $(seq 1 25); do pgrep -f "Playdate Simulator" >/dev/null || break; sleep 0.2; done
rm -rf "$OUT"; mkdir -p "$FRAMES" "$MEDIA"

echo "[1/5] building harness..."
rm -rf build && make CLANGFLAGS="-g -DBALLY_SHOT" >/tmp/cap_build.log 2>&1

echo "[2/5] running simulator (headless capture)..."
"$SIM" "$(pwd)/SkyWarden.pdx" >/tmp/cap_sim.log 2>&1 &
PID=$!
for i in $(seq 1 600); do [ -f "$OUT/done" ] && break; sleep 0.2; done   # harness exits at done
sleep 0.5
kill -9 $PID 2>/dev/null || true; killall -9 "Playdate Simulator" 2>/dev/null || true
[ -f "$OUT/done" ] || { echo "CAPTURE FAILED (no done sentinel)"; tail -25 /tmp/cap_sim.log; exit 1; }
echo "      captured $(ls "$FRAMES"/*.raw | wc -l | tr -d ' ') frames"

echo "[3/5] frames -> png..."
python3 - "$FRAMES" <<'PY'
import sys, os, glob
import numpy as np
from PIL import Image
ROW = 52; F = sys.argv[1]                      # 1bpp, 52-byte rows, 400x240, bit=1 -> white
raws = sorted(glob.glob(os.path.join(F, "*.raw")))
for r in raws:
    d = np.frombuffer(open(r, "rb").read(), dtype=np.uint8)
    rows = d[:240 * ROW].reshape(240, ROW)[:, :50]
    Image.fromarray(np.unpackbits(rows, axis=1) * 255).save(os.path.splitext(r)[0] + ".png")
print("      %d pngs" % len(raws))
PY

echo "[4/5] encoding mp4 (with sound) + gif..."
ffmpeg -y -framerate $FPS -start_number 0 -i "$FRAMES/%04d.png" -i "$OUT/gameplay.wav" \
  -c:v libx264 -pix_fmt yuv420p -crf 18 -vf "scale=$SCALE:flags=neighbor" \
  -c:a aac -b:a 160k -shortest "$MEDIA/gameplay.mp4" >/tmp/cap_mp4.log 2>&1
ffmpeg -y -i "$FRAMES/%04d.png" -vf "fps=$FPS,scale=$SCALE:flags=neighbor,palettegen=stats_mode=diff" \
  /tmp/cap_pal.png >/tmp/cap_gif.log 2>&1
ffmpeg -y -framerate $FPS -start_number 0 -i "$FRAMES/%04d.png" -i /tmp/cap_pal.png \
  -lavfi "fps=$FPS,scale=$SCALE:flags=neighbor[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3" \
  "$MEDIA/gameplay.gif" >>/tmp/cap_gif.log 2>&1

echo "[5/5] stills (3x): menu / launch / flight / landing..."
python3 - "$FRAMES" "$MEDIA" <<'PY'
import sys, os, glob
from PIL import Image
F, M = sys.argv[1], sys.argv[2]
pngs = sorted(glob.glob(os.path.join(F, "*.png"))); n = len(pngs)
picks = {"shot_menu": 40, "shot_launch": 230, "shot_flight": 725, "shot_landing": n-30}
for nm, i in picks.items():
    i = max(0, min(n-1, i)); im = Image.open(pngs[i]).convert("L")
    im.resize((im.width*3, im.height*3), Image.NEAREST).save(os.path.join(M, nm+".png"))
PY

echo "DONE:"; ls -la "$MEDIA"; echo "(playable build restored on exit)"
