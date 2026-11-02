"""Generates the file-type icons (one per extension): a document with the type in big letters and the
Transport logo small in the corner. Writes resources/doctypes/<ext>.icns (macOS), <ext>.ico (Windows) and _overview.png (light and dark preview).
The lists here must match TRANSPORT_AUDIO_TYPES / TRANSPORT_VIDEO_TYPES in CMakeLists.txt.

    python3 tools/make_doc_icons.py

Needs Pillow and the Inter font (InterDisplay-Bold.otf / Inter-Bold.otf); the generated files are committed,
so building Transport doesn't need either.
"""
import os
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "resources", "doctypes")
LOGO = Image.open(os.path.join(ROOT, "resources", "icon.png")).convert("RGBA")

AUDIO = ["mp3", "wav", "flac", "aif", "aiff", "ogg", "m4a", "aac", "wma", "opus"]
VIDEO = ["mp4", "m4v", "mov", "mkv", "webm", "avi", "wmv", "flv", "mpg", "mpeg", "ts", "m2ts", "3gp", "ogv", "asf"]

FONT_PATHS = [
    "/usr/share/fonts/opentype/inter/InterDisplay-Black.otf",
    "/usr/share/fonts/opentype/inter/Inter-Black.otf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]
FONT = next(p for p in FONT_PATHS if os.path.exists(p))

PAGE = (38, 38, 38, 255)        # the player's dark surfaces
FOLD = (70, 70, 70, 255)
TEXT = (255, 255, 255, 255)
ACCENT = (232, 163, 58, 255)    # theme::accent
VIDEO_ACCENT = (232, 163, 58, 255)
DIM = (140, 140, 140, 255)


import math


def rounded_polygon(corners):
    """corners: [(x, y, radius)] clockwise; returns a polygon with the corners rounded."""
    pts = []
    n = len(corners)
    for i, (x, y, r) in enumerate(corners):
        if r <= 0:
            pts.append((x, y))
            continue
        px, py, _ = corners[i - 1]
        nx, ny, _ = corners[(i + 1) % n]
        # unit vectors to the neighbours
        a = (px - x, py - y); la = math.hypot(*a); a = (a[0] / la, a[1] / la)
        b = (nx - x, ny - y); lb = math.hypot(*b); b = (b[0] / lb, b[1] / lb)
        p0 = (x + a[0] * r, y + a[1] * r)
        p2 = (x + b[0] * r, y + b[1] * r)
        for k in range(13):   # quadratic curve through the corner
            t = k / 12
            pts.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * x + t * t * p2[0],
                        (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * y + t * t * p2[1]))
    return pts


def rim_layer(size):
    return Image.new("RGBA", (size, size), (0, 0, 0, 0))


RIM = (255, 255, 255, 100)   # light rim: barely visible on light backgrounds, separates shapes on dark ones


def fit_font(text, max_width, max_height):
    size = int(max_height)
    while size > 6:
        f = ImageFont.truetype(FONT, size)
        l, t, r, b = f.getbbox(text)
        if r - l <= max_width and b - t <= max_height:
            return f
        size -= 2
    return ImageFont.truetype(FONT, 6)


def render(ext, size, video):
    """Draws at 4x and scales down. Small sizes get a simpler, bolder layout."""
    S = size * 4
    small = size <= 32
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    w = S * (0.80 if small else 0.74)
    h = S * (0.96 if small else 0.92)
    x0 = (S - w) / 2 - (0 if small else S * 0.03)
    y0 = (S - h) / 2
    x1, y1 = x0 + w, y0 + h
    radius = S * (0.05 if small else 0.06)
    fold = w * (0.22 if small else 0.26)
    page = rounded_polygon([(x0, y0, radius), (x1 - fold, y0, radius * 0.3), (x1, y0 + fold, radius * 0.3),
                            (x1, y1, radius), (x0, y1, radius)])
    rim_w = max(4, int(S * 0.007))

    if not small:
        shadow = rim_layer(S)
        ImageDraw.Draw(shadow).polygon([(x, y + S * 0.012) for x, y in page], fill=(0, 0, 0, 90))
        img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(S * 0.012)))

    d = ImageDraw.Draw(img)
    d.polygon(page, fill=PAGE)
    d.polygon([(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], fill=FOLD)
    rim = rim_layer(S)
    ImageDraw.Draw(rim).line(page + [page[0]], fill=RIM, width=rim_w, joint="curve")
    img = Image.alpha_composite(img, rim)
    d = ImageDraw.Draw(img)

    label = ext.upper()
    cx = x0 + w / 2

    if not small:
        my = y0 + h * 0.28
        if video:
            t = w * 0.15
            d.polygon([(cx - t * 0.45, my - t * 0.6), (cx + t * 0.62, my), (cx - t * 0.45, my + t * 0.6)], fill=ACCENT)
        else:
            bars = [0.35, 0.7, 1.0, 0.55, 0.85, 0.4, 0.65]
            bw, gap = w * 0.045, w * 0.032
            total = len(bars) * bw + (len(bars) - 1) * gap
            bx = cx - total / 2
            for v in bars:
                bh = w * 0.22 * v
                d.rounded_rectangle((bx, my - bh / 2, bx + bw, my + bh / 2), bw / 2, fill=ACCENT)
                bx += bw + gap

    max_w = w * (0.90 if small else 0.84)
    max_h = h * (0.46 if small else 0.27) * (1.0 if len(label) <= 3 else 0.88)
    font = fit_font(label, max_w, max_h)
    l, t, r, b = font.getbbox(label)
    ty = y0 + h * (0.56 if small else 0.57)
    d.text((cx - (l + r) / 2, ty - (t + b) / 2), label, font=font, fill=TEXT)

    if not small:
        rw = w * 0.22
        ry = ty + (b - t) / 2 + h * 0.055
        d.rounded_rectangle((cx - rw / 2, ry, cx + rw / 2, ry + S * 0.013), S * 0.0065, fill=ACCENT)

        # the player's logo as a small stamp, bottom right, with a page-coloured ring and the light rim
        ls = int(S * 0.36)
        lx, ly = S - ls, S - ls
        inset, lr = ls * 0.0625, ls * 0.19            # the rounded square inside icon.png
        gap = S * 0.014
        ring_box = (lx + inset - gap, ly + inset - gap, lx + ls - inset + gap, ly + ls - inset + gap)
        d.rounded_rectangle(ring_box, lr + gap, fill=PAGE)
        rim = rim_layer(S)
        ImageDraw.Draw(rim).rounded_rectangle(ring_box, lr + gap, outline=RIM, width=rim_w)
        img = Image.alpha_composite(img, rim)
        img.alpha_composite(LOGO.resize((ls, ls), Image.LANCZOS), (lx, ly))
    else:
        rr = S * 0.075
        d.ellipse((x1 - rr * 2.7, y1 - rr * 2.7, x1 - rr * 0.7, y1 - rr * 0.7), fill=ACCENT)

    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    previews = {}
    for i, ext in enumerate(AUDIO + VIDEO):
        video = ext in VIDEO
        big = render(ext, 1024, video)
        previews[ext] = big.resize((150, 150), Image.LANCZOS)

        # macOS .icns (Pillow builds the size set from the 1024 image)
        big.save(os.path.join(OUT, f"{ext}.icns"))

        # Windows .ico with hand-tuned small sizes
        sizes = [16, 24, 32, 48, 64, 128, 256]
        frames = [render(ext, s, video) for s in sizes]
        frames[-1].save(os.path.join(OUT, f"{ext}.ico"), sizes=[(s, s) for s in sizes], append_images=frames[:-1])
        print("made", ext)

    # overview sheet for reviewing the design
    cols = 9
    cell = 180
    names = AUDIO + VIDEO
    rows = (len(names) + cols - 1) // cols
    half = rows * cell + 150
    sheet = Image.new("RGBA", (cols * cell, half * 2), (236, 236, 236, 255))
    ImageDraw.Draw(sheet).rectangle((0, half, cols * cell, half * 2), fill=(30, 30, 30, 255))
    for base in (0, half):
        for i, ext in enumerate(names):
            im = previews[ext]
            sheet.alpha_composite(im, ((i % cols) * cell + 15, base + (i // cols) * cell + 15))
        x = 20
        for s in [16, 24, 32, 48, 64, 128]:
            for ext, video in (("mp3", False), ("mp4", True)):
                sheet.alpha_composite(render(ext, s, video), (x, base + rows * cell + 10))
                x += s + 16
    sheet.save(os.path.join(OUT, "_overview.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
