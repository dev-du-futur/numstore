"""Draws the Numstore icon: a calculator with a download arrow on an indigo-violet tile.
python scripts/make_icon.py  ->  build/icon.png (512), build/icon.ico, src/ui/logo.png"""
from pathlib import Path
from PIL import Image, ImageDraw

S = 4  # supersampling
N = 512 * S
here = Path(__file__).resolve().parent.parent
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))

# gradient tile
grad = Image.new("RGBA", (N, N))
gd = ImageDraw.Draw(grad)
a, b = (91, 124, 255), (124, 92, 255)
for y in range(N):
    t = y / N
    gd.line([(0, y), (N, y)], fill=tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,))
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, N - 1, N - 1], radius=112 * S, fill=255)
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)
w = lambda v: int(v * S)
# calculator body
d.rounded_rectangle([w(150), w(92), w(362), w(420)], radius=w(34), fill=(255, 255, 255, 255))
# screen
d.rounded_rectangle([w(178), w(122), w(334), w(206)], radius=w(14), fill=(48, 44, 120, 255))
# download arrow on the screen
cx = w(256)
d.line([(cx, w(136)), (cx, w(184))], fill=(255, 255, 255, 255), width=w(13))
d.polygon([(cx - w(24), w(168)), (cx + w(24), w(168)), (cx, w(196))], fill=(255, 255, 255, 255))
# keys
for r in range(3):
    for c in range(3):
        x0, y0 = w(182 + c * 54), w(234 + r * 54)
        col = (124, 92, 255, 255) if (r, c) == (2, 2) else (214, 216, 245, 255)
        d.rounded_rectangle([x0, y0, x0 + w(40), y0 + w(38)], radius=w(10), fill=col)

out = img.resize((512, 512), Image.LANCZOS)
(here / "build").mkdir(exist_ok=True)
out.save(here / "build" / "icon.png")
out.save(here / "build" / "icon.ico", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
out.resize((96, 96), Image.LANCZOS).save(here / "src" / "ui" / "logo.png")
print("ok")
