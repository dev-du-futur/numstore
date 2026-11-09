# Rollmark's pictures, drawn here with Pillow at 4x and scaled down: the shop's item icons (20x20) and Glim, the
# orb shopkeeper (3 frames). Shapes and colours follow RNGidle's own CSS art (apps/web/src/ui/items/item-looks.ts,
# item-art.css; features/npcs/merchant-orb.css), redrawn for a 20-pixel screen. Used by make_assets.py.
import math
from PIL import Image, ImageDraw, ImageFilter

S = 4  # supersampling


def hexc(h, a=255):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4))


def canvas(w, h):
    im = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def done(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def vgrad(im, box, top, bottom, mask):
    """fills box with a vertical gradient, only where mask (L image, same size as im) is set"""
    x0, y0, x1, y1 = [int(v) for v in box]
    g = Image.new('RGBA', im.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    for y in range(y0, y1 + 1):
        gd.line([(x0, y), (x1, y)], fill=mix(top, bottom, (y - y0) / max(1, y1 - y0)))
    im.paste(g, (0, 0), mask)


GLASS = hexc('#dff4ff', 70)
GLASS_EDGE = hexc('#eaf6ff', 230)
CORK = hexc('#b07a45')


def bottle(shape, liq, deep, mark=None, sparkle=False, level=0.55):
    """a potion: glass outline, liquid up to level, a mark, sparkles for the rare ones"""
    w = h = 20
    im, d = canvas(w, h)
    m = Image.new('L', im.size, 0)
    md = ImageDraw.Draw(m)
    if shape == 'round':
        body = (3 * S, 6 * S, 17 * S, 19.5 * S)
        md.ellipse(body, fill=255)
        md.rectangle((8 * S, 2.5 * S, 12 * S, 8 * S), fill=255)
    elif shape == 'bottle':
        body = (4 * S, 7 * S, 16 * S, 19.5 * S)
        md.rounded_rectangle(body, 2.5 * S, fill=255)
        md.polygon([(4 * S, 9 * S), (8 * S, 5 * S), (12 * S, 5 * S), (16 * S, 9 * S)], fill=255)
        md.rectangle((8 * S, 2.5 * S, 12 * S, 6 * S), fill=255)
    else:  # flask: a cone
        body = (2.5 * S, 7 * S, 17.5 * S, 19.5 * S)
        md.polygon([(7.5 * S, 7 * S), (12.5 * S, 7 * S), (17.5 * S, 19.5 * S), (2.5 * S, 19.5 * S)], fill=255)
        md.rectangle((7.5 * S, 2.5 * S, 12.5 * S, 8 * S), fill=255)
    # glass, then liquid below the level
    im.paste(Image.new('RGBA', im.size, GLASS), (0, 0), m)
    top = body[1] + (body[3] - body[1]) * (1 - level)
    lm = Image.new('L', im.size, 0)
    ImageDraw.Draw(lm).rectangle((0, top, w * S, h * S), fill=255)
    from PIL import ImageChops
    lm = ImageChops.multiply(lm, m.filter(ImageFilter.MinFilter(5)))
    vgrad(im, (0, top, w * S, body[3]), hexc(liq), hexc(deep), lm)
    # outline and cork
    edge = m.filter(ImageFilter.MaxFilter(5))
    ring = ImageChops.subtract(edge, m)
    im.paste(Image.new('RGBA', im.size, GLASS_EDGE), (0, 0), ring)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((7.5 * S, 0.5 * S, 12.5 * S, 3 * S), S, fill=CORK)
    # a glint on the glass
    d.ellipse((5.5 * S, 9.5 * S, 7.5 * S, 12.5 * S), fill=(255, 255, 255, 170))
    if mark:
        mark(d, (body[0] + body[2]) / 2, (body[1] + body[3]) / 2 + S)
    if sparkle:
        for (x, y, r) in ((16.5, 4, 1.6), (3, 5, 1.1)):
            star(d, x * S, y * S, r * S, (255, 250, 210, 255))
    return done(im, w, h)


def star(d, x, y, r, col):
    d.polygon([(x, y - 2 * r), (x + r * 0.5, y - r * 0.5), (x + 2 * r, y), (x + r * 0.5, y + r * 0.5),
               (x, y + 2 * r), (x - r * 0.5, y + r * 0.5), (x - 2 * r, y), (x - r * 0.5, y - r * 0.5)], fill=col)


def mark_dot(n):
    """luck potions: n small pips (1 to 5), a second row for VI-X, so the strength shows without numerals"""
    def f(d, cx, cy):
        rows = [n] if n <= 5 else [5, n - 5]
        for ri, k in enumerate(rows):
            y = cy + (ri - (len(rows) - 1) / 2) * 3.2 * S
            for i in range(k):
                x = cx + (i - (k - 1) / 2) * 2.4 * S
                d.ellipse((x - 0.9 * S, y - 0.9 * S, x + 0.9 * S, y + 0.9 * S), fill=(255, 255, 255, 235))
    return f


def mark_star(col):
    return lambda d, cx, cy: star(d, cx, cy, 1.8 * S, col)


def mark_ring(d, cx, cy):
    d.ellipse((cx - 3 * S, cy - 3 * S, cx + 3 * S, cy + 3 * S), outline=(255, 255, 255, 240), width=int(1.3 * S))
    d.ellipse((cx - 1 * S, cy - 1 * S, cx + 1 * S, cy + 1 * S), fill=(255, 255, 255, 240))


def mark_swap(d, cx, cy):
    c = (255, 255, 255, 240)
    d.line([(cx - 3 * S, cy - 1.3 * S), (cx + 3 * S, cy - 1.3 * S)], fill=c, width=S)
    d.polygon([(cx + 3.6 * S, cy - 1.3 * S), (cx + 1.8 * S, cy - 2.8 * S), (cx + 1.8 * S, cy + 0.2 * S)], fill=c)
    d.line([(cx - 3 * S, cy + 1.7 * S), (cx + 3 * S, cy + 1.7 * S)], fill=c, width=S)
    d.polygon([(cx - 3.6 * S, cy + 1.7 * S), (cx - 1.8 * S, cy + 0.2 * S), (cx - 1.8 * S, cy + 3.2 * S)], fill=c)


def mark_down(d, cx, cy):
    c = (255, 255, 255, 240)
    d.line([(cx, cy - 3.5 * S), (cx, cy + 1 * S)], fill=c, width=int(1.4 * S))
    d.polygon([(cx - 2.6 * S, cy + 0.5 * S), (cx + 2.6 * S, cy + 0.5 * S), (cx, cy + 3.6 * S)], fill=c)


def wheel():
    im, d = canvas(20, 20)
    cols = ['#ef4444', '#f8fafc', '#22c55e', '#f8fafc', '#3b82f6', '#f8fafc', '#eab308', '#f8fafc']
    box = (1.5 * S, 1.5 * S, 18.5 * S, 18.5 * S)
    for i, c in enumerate(cols):
        d.pieslice(box, i * 45 - 90, (i + 1) * 45 - 90, fill=hexc(c))
    d.ellipse(box, outline=hexc('#b8860b'), width=int(1.6 * S))
    d.ellipse((7.5 * S, 7.5 * S, 12.5 * S, 12.5 * S), fill=hexc('#facc15'), outline=hexc('#7a5a00'), width=S)
    d.polygon([(10 * S, 0), (8.2 * S, 3.4 * S), (11.8 * S, 3.4 * S)], fill=hexc('#ffffff'))
    return done(im, 20, 20)


def slots():
    im, d = canvas(20, 20)
    d.rounded_rectangle((0.5 * S, 3 * S, 19.5 * S, 17 * S), 2.5 * S, fill=hexc('#b91c1c'), outline=hexc('#fca5a5'), width=S)
    for i, ch in enumerate((7, 7, 7)):
        x = 2.5 * S + i * 5.2 * S
        d.rounded_rectangle((x, 5.5 * S, x + 4.4 * S, 14.5 * S), S, fill=hexc('#fff7ed'))
        # a little 7
        d.line([(x + 1.1 * S, 7.5 * S), (x + 3.3 * S, 7.5 * S), (x + 1.8 * S, 12.6 * S)], fill=hexc('#dc2626'), width=int(1.2 * S))
    d.rectangle((2 * S, 9.7 * S, 18 * S, 10.3 * S), fill=hexc('#fde047', 160))
    return done(im, 20, 20)


def knife():
    im, d = canvas(20, 20)
    # blade from top right to the middle, handle to the bottom left
    d.polygon([(17.5 * S, 1.5 * S), (19 * S, 3 * S), (9 * S, 13 * S), (6.5 * S, 11.5 * S)], fill=hexc('#e5e7eb'))
    d.polygon([(17.5 * S, 1.5 * S), (19 * S, 3 * S), (9 * S, 13 * S)], fill=hexc('#9ca3af'))
    d.line([(5 * S, 10.5 * S), (10 * S, 15.5 * S)], fill=hexc('#facc15'), width=int(1.6 * S))
    d.line([(7.5 * S, 13 * S), (2 * S, 18.5 * S)], fill=hexc('#7c2d12'), width=int(3 * S))
    # two digits falling apart
    d.text((1 * S, 0), '', fill=0)
    d.rectangle((2 * S, 2 * S, 4 * S, 7 * S), fill=hexc('#60a5fa'))
    d.rectangle((12 * S, 13 * S, 14 * S, 18 * S), fill=hexc('#f472b6'))
    return done(im, 20, 20)


def mirror():
    im, d = canvas(20, 20)
    d.ellipse((3 * S, 0.5 * S, 17 * S, 16.5 * S), fill=hexc('#d4a017'))
    d.ellipse((5 * S, 2.5 * S, 15 * S, 14.5 * S), fill=hexc('#bfe9ff'))
    d.polygon([(6.5 * S, 9 * S), (11 * S, 4 * S), (13 * S, 5.5 * S), (8 * S, 11 * S)], fill=(255, 255, 255, 200))
    d.rounded_rectangle((8.5 * S, 15.5 * S, 11.5 * S, 19.5 * S), S, fill=hexc('#a16207'))
    return done(im, 20, 20)


def grinder():
    im, d = canvas(20, 20)
    cx = cy = 10 * S
    teeth = []
    for i in range(24):
        a = i * math.pi / 12
        r = (9.5 if i % 2 == 0 else 7.5) * S
        teeth.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(teeth, fill=hexc('#9ca3af'))
    d.ellipse((cx - 6.5 * S, cy - 6.5 * S, cx + 6.5 * S, cy + 6.5 * S), fill=hexc('#6b7280'))
    d.ellipse((cx - 2.5 * S, cy - 2.5 * S, cx + 2.5 * S, cy + 2.5 * S), fill=hexc('#1f2937'))
    d.arc((cx - 5 * S, cy - 5 * S, cx + 5 * S, cy + 5 * S), 200, 290, fill=hexc('#e5e7eb'), width=S)
    return done(im, 20, 20)


def infinity():
    im, d = canvas(20, 20)
    pts = []
    for i in range(121):
        t = i / 120 * 2 * math.pi
        k = 1 + math.sin(t) ** 2
        pts.append((10 * S + 8.5 * S * math.cos(t) / k, 10 * S + 8.5 * S * math.sin(t) * math.cos(t) / k))
    glow = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).line(pts, fill=hexc('#c084fc', 200), width=int(4.5 * S))
    im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(S)))
    d = ImageDraw.Draw(im)
    n = len(pts) - 1
    for i in range(n):  # rainbow along the curve
        h = i / n
        c = tuple(int(255 * (0.5 + 0.5 * math.cos(2 * math.pi * (h + o)))) for o in (0, 1 / 3, 2 / 3))
        d.line([pts[i], pts[i + 1]], fill=c + (255,), width=int(2.2 * S))
    return done(im, 20, 20)


LUCK = [('#7ee08a', '#2f9e4a'), ('#b5e655', '#5f9e1c'), ('#4fe0c0', '#17917a'), ('#48c8ff', '#1569b8'),
        ('#6f8bff', '#2d3fb8'), ('#9a6bff', '#5326c2'), ('#d36bff', '#8a1fc0'), ('#ff6bc8', '#b8186f'),
        ('#ffd24a', '#c27a0c'), ('#ff7a7a', '#7a3cff')]


def item_icons():
    """in Rollmark's item order (src/items.h)"""
    out = []
    for i, (a, b) in enumerate(LUCK):
        out.append(bottle('round', a, b, mark_dot(i + 1), sparkle=i >= 6, level=0.5 + i * 0.03))
    out.append(bottle('bottle', '#f87171', '#7f1d1d', mark_star((255, 240, 240, 240))))                  # Mythic
    out.append(bottle('bottle', '#ffffff', '#7adbbc', mark_star((36, 85, 72, 255))))                     # Unreal
    out.append(bottle('bottle', '#ff5c8a', '#7a0f3a', mark_ring))                                         # Aberration
    out.append(bottle('bottle', '#7dff9a', '#0f7a52', mark_star((255, 255, 255, 250)), sparkle=True))    # Otherworldly
    out.append(wheel())
    out.append(slots())
    out.append(knife())
    out.append(bottle('flask', '#419fff', '#ef4265', mark_swap, level=0.7))                               # Digit Swap
    out.append(mirror())
    out.append(bottle('round', '#7b6bff', '#120a3a', mark_down, level=0.6))                               # Gravity
    out.append(grinder())
    out.append(infinity())
    return out


def glim(frame):
    """Glim, 40x44: the orb (RNGidle's radial gradient), its eyes, blush and mouth, a tilted top hat with an orange
    band, and a soft blue glow. frame 0: looking, 1: blinking, 2: talking (mouth open)"""
    W, H = 40, 44
    im, d = canvas(W, H)
    cx, cy, r = 20 * S, 27 * S, 14.5 * S
    # glow
    g = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(g).ellipse((cx - r - 2 * S, cy - r - 2 * S, cx + r + 2 * S, cy + r + 2 * S), fill=(110, 200, 255, 150))
    im.alpha_composite(g.filter(ImageFilter.GaussianBlur(2.5 * S)))
    # the ball: radial gradient from a light spot at 34% / 28%
    stops = [(0, '#ffffff'), (0.09, '#d8f6ff'), (0.30, '#7fd6ff'), (0.66, '#6a6cff'), (1.0, '#2b1880')]
    ball = Image.new('RGBA', im.size, (0, 0, 0, 0))
    bp = ball.load()
    lx, ly = cx - r + 0.68 * r, cy - r + 0.56 * r
    far = r * 1.55
    for y in range(int(cy - r), int(cy + r) + 1):
        for x in range(int(cx - r), int(cx + r) + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 > r * r:
                continue
            t = min(1, math.hypot(x - lx, y - ly) / far)
            for k in range(len(stops) - 1):
                if t <= stops[k + 1][0]:
                    a, b = stops[k], stops[k + 1]
                    bp[x, y] = mix(hexc(a[1]), hexc(b[1]), (t - a[0]) / (b[0] - a[0]))
                    break
    im.alpha_composite(ball)
    d = ImageDraw.Draw(im)
    ink = hexc('#1b1048')
    # blush
    bl = Image.new('RGBA', im.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(bl)
    for sx in (-1, 1):
        x = cx + sx * 8.5 * S
        bd.ellipse((x - 2.6 * S, cy + 1.5 * S, x + 2.6 * S, cy + 3.6 * S), fill=(255, 122, 184, 150))
    im.alpha_composite(bl.filter(ImageFilter.GaussianBlur(0.6 * S)))
    d = ImageDraw.Draw(im)
    # eyes
    for sx in (-1, 1):
        x = cx + sx * 4.6 * S
        if frame == 1:
            d.arc((x - 2.2 * S, cy - 4.5 * S, x + 2.2 * S, cy - 1 * S), 20, 160, fill=ink, width=int(1.1 * S))
        else:
            d.ellipse((x - 2.1 * S, cy - 6.5 * S, x + 2.1 * S, cy - 0.5 * S), fill=ink)
            d.ellipse((x - 1.3 * S, cy - 5.8 * S, x + 0.1 * S, cy - 4.2 * S), fill=(255, 255, 255, 255))
    # mouth: a half disc, open wider when talking
    mh = 3.2 if frame == 2 else 1.7
    d.chord((cx - 2.6 * S, cy + 1.6 * S - mh * S, cx + 2.6 * S, cy + 1.6 * S + mh * S), 0, 180, fill=ink)
    if frame == 2:
        d.chord((cx - 1.4 * S, cy + 2.6 * S, cx + 1.4 * S, cy + 4.6 * S), 180, 360, fill=hexc('#ff7ab8'))
    # the hat, tilted to the right
    hat = Image.new('RGBA', (24 * S, 20 * S), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hat)
    hd.ellipse((0, 14 * S, 24 * S, 19 * S), fill=hexc('#2a1d45'))
    hd.rounded_rectangle((5 * S, 1 * S, 19 * S, 17 * S), 1.5 * S, fill=hexc('#33245a'))
    hd.rectangle((5 * S, 1 * S, 9 * S, 16 * S), fill=hexc('#22163c'))
    hd.rectangle((5 * S, 10 * S, 19 * S, 13.5 * S), fill=hexc('#ff7a45'))
    hd.rectangle((5 * S, 10 * S, 8 * S, 13.5 * S), fill=hexc('#c0401f'))
    hd.ellipse((14 * S, 6.5 * S, 18 * S, 11.5 * S), fill=hexc('#ffd36b'))   # the feather's tip
    hat = hat.rotate(-14, resample=Image.BICUBIC, expand=True)
    im.alpha_composite(hat, (int(cx - hat.width / 2 + 3 * S), int(cy - r - hat.height + 5 * S)))
    return done(im, W, H)
