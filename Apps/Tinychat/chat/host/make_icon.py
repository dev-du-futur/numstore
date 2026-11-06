"""Draw TinyChat's home-screen icon (55x56 PNG, no libraries): an indigo-to-violet tile with a
white speech bubble holding the bot's face. Run: python host/make_icon.py src/icon.png"""
import math, struct, sys, zlib

W, H, SS = 55, 56, 4                       # supersampling for smooth edges
A, B = (79, 91, 213), (124, 92, 230)        # same accent colours as the app
WHITE, HOME = (255, 255, 255), (255, 255, 255)

def in_rrect(x, y, x0, y0, x1, y1, r):
    cx = min(max(x, x0 + r), x1 - r); cy = min(max(y, y0 + r), y1 - r)
    return x0 <= x <= x1 and y0 <= y <= y1 and (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def sample(x, y):
    if not in_rrect(x, y, 0.5, 0.5, W - 0.5, H - 0.5, 11):
        return HOME
    t = (x + y) / (W + H)
    tile = tuple(round(a + (b - a) * t) for a, b in zip(A, B))
    # speech bubble: rounded rect plus a tail at bottom-left
    bub = in_rrect(x, y, 9, 11, 46, 39, 10)
    tail = 13 <= x <= 25 and 36 <= y <= 47 and (x - 13) * 11 >= (y - 36) * 1.0 and y - 36 <= (25 - x) * 0.95
    if not (bub or tail):
        return tile
    face = tuple(round(a + (b - a) * 0.3) for a, b in zip(A, B))
    for ex in (21.5, 33.5):                  # eyes
        if abs(x - ex) <= 2.6 and 19 <= y <= 26 and (abs(x - ex) <= 1.6 or 20 <= y <= 25):
            return face
    d = math.hypot(x - 27.5, y - 25.5)      # smile: lower arc
    if 7.2 <= d <= 9.6 and y > 29.5 and abs(x - 27.5) <= 6.5:
        return face
    return WHITE

rows = []
for py in range(H):
    row = bytearray([0])
    for px in range(W):
        acc = [0, 0, 0]
        for sy in range(SS):
            for sx in range(SS):
                c = sample(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS)
                for k in range(3): acc[k] += c[k]
        row += bytes(round(v / SS / SS) for v in acc)
    rows.append(bytes(row))

def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
print("wrote", sys.argv[1], len(png), "bytes")
