"""Checks what Transport's platform video decoder reports for the test clips (run by CI on every OS)."""
import json
import sys

colors = json.load(open(sys.argv[1]))
portrait = json.load(open(sys.argv[2]))
errors = []


def check(ok, message):
    print(("ok    " if ok else "FAIL  ") + message)
    if not ok:
        errors.append(message)


def near(a, b, tol):
    return a is not None and abs(a - b) <= tol


c = colors
check(c.get("backend") and c.get("probed"), "colors.mp4 opens")
check(c.get("hasVideo") and c.get("hasAudio"), "colors.mp4 has picture and sound")
check(near(c.get("duration"), 3.0, 0.15), f"duration 3 s (got {c.get('duration')})")
check(c.get("width") == 320 and c.get("height") == 180, f"size 320x180 (got {c.get('width')}x{c.get('height')})")
check(near(c.get("fps"), 25.0, 0.5), f"25 fps (got {c.get('fps')})")
check(c.get("rotation") == 0, f"no rotation (got {c.get('rotation')})")
check(c.get("videoCodec") == "H.264", f"codec H.264 (got {c.get('videoCodec')})")
check(near(c.get("sampleRate"), 48000, 1), f"48 kHz (got {c.get('sampleRate')})")
for i, level in enumerate(c.get("rmsDb", [])):
    check(near(level, -28.1, 1.5), f"sine level read #{i} -28 dB (got {level:.2f})")
check(len(c.get("rmsDb", [])) == 3, "sound readable with seeking")

frames = c.get("frames", [])
check(len(frames) == 4, "4 frames decoded")
for f in frames:
    t, pts, rgb = f.get("t"), f.get("pts"), f.get("rgb")
    if pts is None or rgb is None:
        check(False, f"frame at {t}s decoded")
        continue
    check(t - 0.042 <= pts <= t + 0.002, f"seek {t}s lands on the frame on screen (pts {pts:.3f})")
    expected = (255, 0, 0) if pts < 0.999 else (0, 255, 0) if pts < 1.999 else (0, 0, 255)
    check(all(abs(a - b) < 45 for a, b in zip(rgb, expected)), f"frame at {pts:.2f}s has the right colour {[round(x) for x in rgb]} vs {expected}")

ticks = c.get("syncTicks", 0)
check(ticks > 100, f"real-time sync ran ({ticks} ticks)")
check(c.get("syncLateTicks", 99) <= max(2, ticks * 0.03), f"picture in time with the clock (late {c.get('syncLateTicks')} of {ticks}, worst lag {c.get('syncWorstLag', 0):.3f}s)")
settle = c.get("syncSeekSettle", [])
check(len(settle) == 4 and all(0 <= s < 0.3 for s in settle), f"jumps settle quickly {settle}")

p = portrait
check(p.get("hasVideo") and not p.get("hasAudio"), "portrait clip: picture, no sound")
check(p.get("rotation") == 270, f"portrait clip rotation 270 cw (got {p.get('rotation')})")
check(near(p.get("audioLength"), 2.0, 0.1), f"silent clock for a video without sound (got {p.get('audioLength')})")

print()
print(f"{len(errors)} problem(s)" if errors else "all media checks passed")
sys.exit(1 if errors else 0)
