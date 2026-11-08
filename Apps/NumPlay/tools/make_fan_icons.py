# Draws our own icons for the fan games (Apps/NumPlay/fangames.json): a rounded tile in the
# game's colour with its initials, a light diagonal sheen and a small "fan" ribbon.
# None of the original games' art is used. Output: Apps/NumPlay/icons/fan-<file>.png (55 x 56).
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
S = 8                      # supersampling
W, H = 55, 56

def font(size):
    for f in ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()

def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

def icon(mono, color):
    c = rgb(color)
    big = Image.new("RGB", (W * S, H * S), (255, 255, 255))
    tile = Image.new("RGB", big.size, c)
    d = ImageDraw.Draw(tile)
    # soft lighter top-left half for depth
    light = tuple(min(255, int(v + (255 - v) * 0.22)) for v in c)
    d.polygon([(0, 0), (W * S, 0), (0, H * S)], fill=light)
    # initials, white with a dark shadow so light colours stay readable
    f = font(int((24 if len(mono) > 1 else 30) * S))
    box = d.textbbox((0, 0), mono, font=f)
    x = (W * S - (box[2] - box[0])) / 2 - box[0]
    y = (H * S - (box[3] - box[1])) / 2 - box[1] - 2 * S
    d.text((x + S, y + S), mono, font=f, fill=(0, 0, 0))
    d.text((x, y), mono, font=f, fill=(255, 255, 255))
    # "FAN" ribbon along the bottom
    d.rectangle([0, (H - 12) * S, W * S, H * S], fill=(0, 0, 0))
    fs = font(8 * S)
    b = d.textbbox((0, 0), "FAN GAME", font=fs)
    d.text(((W * S - (b[2] - b[0])) / 2 - b[0], (H - 11) * S - b[1] + S // 2), "FAN GAME", font=fs, fill=(255, 220, 90))
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=7 * S, fill=255)
    big.paste(tile, (0, 0), mask)
    return big.resize((W, H), Image.LANCZOS)

if __name__ == "__main__":
    games = json.load(open(os.path.join(ROOT, "fangames.json"), encoding="utf-8"))["games"]
    out = os.path.join(ROOT, "icons")
    os.makedirs(out, exist_ok=True)
    for g in games:
        name = "fan-" + g["file"].replace(".nwa", "").lower() + ".png"
        icon(g["mono"], g["color"]).save(os.path.join(out, name))
    print(len(games), "icons")
    if len(sys.argv) > 1:   # contact sheet for checking
        sheet = Image.new("RGB", (8 * 60, ((len(games) + 7) // 8) * 60), (240, 240, 240))
        for i, g in enumerate(games):
            sheet.paste(Image.open(os.path.join(out, "fan-" + g["file"].replace(".nwa", "").lower() + ".png")), ((i % 8) * 60 + 2, (i // 8) * 60 + 2))
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(sys.argv[1])
