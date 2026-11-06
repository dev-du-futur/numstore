# Draws src/icon.png (55 x 56): a short stack of slabs in the game's colours on its sky, a block sliding in.
import colorsys, sys
from PIL import Image, ImageDraw

S = 4  # supersample
W, H = 55 * S, 56 * S
img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)
top, bot = (44, 115, 206), (161, 204, 241)
for y in range(H):
    t = y / (H - 1)
    d.line([(0, y), (W, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
# a cloud
for cx, cy, r in [(12, 40, 7), (20, 37, 9), (29, 41, 7), (44, 14, 5), (50, 12, 6)]:
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=(236, 243, 250))

def lin(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def srgb(v): v = max(0, min(1, v)); return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
def shade(rgb, k): return tuple(int(255 * srgb(lin(c) * k / 3.14159)) for c in rgb)
def colour(i):
    return colorsys.hls_to_rgb(((i * 8) % 360) / 360, 0.5, 0.7)

def slab(cx, cy, w, dpt, h, rgb):
    # isometric: x goes right-down, z goes left-down
    ax, ay = 1.0 * w * S, 0.5 * w * S
    bx, by = -1.0 * dpt * S, 0.5 * dpt * S
    p0 = (cx * S, cy * S)
    p1 = (p0[0] + ax, p0[1] + ay)
    p2 = (p1[0] + bx, p1[1] + by)
    p3 = (p0[0] + bx, p0[1] + by)
    hh = h * S
    d.polygon([p3, p2, (p2[0], p2[1] + hh), (p3[0], p3[1] + hh)], fill=shade(rgb, 0.6))           # +z face
    d.polygon([p2, p1, (p1[0], p1[1] + hh), (p2[0], p2[1] + hh)], fill=shade(rgb, 0.6 + 0.6 * 0.447))  # +x face
    d.polygon([p0, p1, p2, p3], fill=shade(rgb, 0.6 + 0.6 * 0.894))                                  # top

# the tower: base reaching the bottom, then slabs
slab(27, 44, 13, 13, 30, colour(0))
for i in range(1, 7):
    w = 13 - (i // 3)
    slab(27 + (i % 2) * 0.5, 44 - i * 5, w, w, 5, colour(i * 4))
slab(11, 3, 11, 11, 5, colour(28))   # sliding in
img = img.resize((55, 56), Image.LANCZOS).convert("RGB")
# rounded corners on white, like the calculator's own icons
from PIL import ImageDraw
mask = Image.new("L", (55 * 8, 56 * 8), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, 55 * 8 - 1, 56 * 8 - 1], radius=56, fill=255)
img = Image.composite(img, Image.new("RGB", (55, 56), (255, 255, 255)), mask.resize((55, 56), Image.LANCZOS))
img.save(sys.argv[1] if len(sys.argv) > 1 else "src/icon.png")
