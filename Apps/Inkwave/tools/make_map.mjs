// Turns an Inkwave stage layout (oriented boxes, src/world/stages/<id>/layout.js) into the blocky heightfield the
// calculator plays on: src/map_data.h, plus an optional top-down preview PNG.
//   node tools/make_map.mjs <unzipped inkwave-main folder> [stage id] [preview.png] > src/map_data.h
// Two bytes per 0.5 m column:
//   map_h     top of the column in 0.1 m steps from -2 m (the sea, -1.6 m, is 4)
//   map_look  bits 7..6 = kind (0 floor, 1 wall: an off-limits building, 2 rail: blocks walking, 3 water),
//             bits 5..4 = surface (0 stucco / plain, 1 herringbone brick, 2 terrazzo, 3 stone steps),
//             bits 3..0 = colour index into map_pal (the Inkwave block's own colour, merged down to 16)
//   map_light bits 3..0 = sun shadow on the column top, one bit per 0.25 m quarter (x + 2y),
//             bits 7..4 = a taller neighbour (land, for water) on side -y, +y, -x, +x (contact shadow / foam)
// Grid x = world +Z (Alpha's spawn at low x), grid y = world -X. Props (benches, lamps...) are left out.
import { pathToFileURL } from 'node:url';
import { writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import path from 'node:path';

const root = process.argv[2], stage = process.argv[3] || 'tidewater', png = process.argv[4];
if (!root) { console.error('usage: node tools/make_map.mjs <inkwave-main> [stage] [preview.png]'); process.exit(1); }
const L = (await import(pathToFileURL(path.join(root, 'src/world/stages', stage, 'layout.js')).href)).LAYOUT;

// ---- blocks (the same maths as Inkwave's src/world/level.js, without three.js)
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(...a); return mul(a, 1 / l); };
function mirror(d) {
  if (d.kind === 'box') return { ...d, min: [-d.max[0], d.min[1], -d.max[2]], max: [-d.min[0], d.max[1], -d.min[2]] };
  if (d.kind === 'obox') return { ...d, center: [-d.center[0], d.center[1], -d.center[2]] };
  return { ...d, low: [-d.low[0], d.low[1], -d.low[2]], high: [-d.high[0], d.high[1], -d.high[2]] };
}
function block(d) {
  const b = { roof: !!d.roof, rail: !!d.rail, tag: d.tag, color: d.color || '#dddddd', pattern: d.pattern ?? 0, axes: [[1, 0, 0], [0, 1, 0], [0, 0, 1]] };
  if (d.kind === 'box') {
    b.c = mul(add(d.min, d.max), 0.5); b.h = mul(sub(d.max, d.min), 0.5);
  } else if (d.kind === 'obox') {
    const a = d.rotY * Math.PI / 180, c = Math.cos(a), s = Math.sin(a);
    b.c = d.center; b.h = mul(d.size, 0.5); b.axes = [[c, 0, -s], [0, 1, 0], [s, 0, c]];
  } else {
    const L0 = d.low, H0 = d.high, sv = sub(H0, L0), len = Math.hypot(...sv), s = norm(sv);
    const flat = norm([s[0], 0, s[2]]); let side = norm(cross([0, 1, 0], flat)); let n = norm(cross(s, side));
    if (n[1] < 0) { n = mul(n, -1); side = mul(side, -1); }
    const rise = H0[1] - L0[1], thick = d.thin ? d.thickness : Math.max(d.thickness ?? 0.6, rise * n[1] + 0.35), ext = 0.6;
    const a = add(L0, mul(s, -ext)), topMid = mul(add(a, H0), 0.5);
    b.c = add(topMid, mul(n, -thick / 2)); b.h = [d.width / 2, thick / 2, (len + ext) / 2]; b.axes = [side, n, s];
  }
  return b;
}
const inside = (b, p) => { const v = sub(p, b.c); return b.axes.every((ax, i) => Math.abs(dot(v, ax)) < b.h[i] + 1e-3); };
function topAt(b, x, z) {   // top of block b above (x, z), or null
  const n = b.axes[1], top = add(b.c, mul(n, b.h[1]));
  const y = top[1] - (n[0] * (x - top[0]) + n[2] * (z - top[2])) / n[1];
  return inside(b, [x, y - 0.02, z]) ? y : null;
}
const blocks = [...L.single, ...L.half, ...L.half.map(mirror)].map(block);

const CELL = 0.5, bx = L.bounds;
const GH = Math.round((bx.maxX - bx.minX) / CELL), GW = Math.round((bx.maxZ - bx.minZ) / CELL);
const toWorld = (gx, gy) => [bx.maxX - (gy + 0.5) * CELL, bx.minZ + (gx + 0.5) * CELL]; // [x, z]
const FLOOR = 0, WALL = 1, RAIL = 2, WATER = 3;
const surf = new Uint8Array(GW * GH), kinds = new Uint8Array(GW * GH), hts = new Uint8Array(GW * GH), col = new Array(GW * GH).fill('#3070b0');
for (let gy = 0; gy < GH; gy++) for (let gx = 0; gx < GW; gx++) {
  const [x, z] = toWorld(gx, gy), i = gy * GW + gx;
  // the walking surface: the highest top at the centre; a rail anywhere in the cell (they are thin) blocks it
  let best = null, bestY = -99, rail = false;
  for (const [ox, oz] of [[0, 0], [-0.17, -0.17], [0.17, 0.17], [-0.17, 0.17], [0.17, -0.17]]) for (const b of blocks) {
    const y = topAt(b, x + ox, z + oz);
    if (y === null) continue;
    if (b.rail) { rail = true; continue; }
    if (ox === 0 && oz === 0 && y > bestY) { bestY = y; best = b; }
  }
  let kind;
  if (!best || bestY < -0.95) { kind = WATER; bestY = -1.6; }
  else if (best.roof || bestY > 4.2) kind = WALL;
  else kind = rail ? RAIL : FLOOR;
  if (best && kind !== WATER) col[i] = best.color;
  if (best) surf[i] = best.pattern === 28 ? 1 : best.pattern === 29 ? 2 : best.pattern === 25 ? 3 : 0;
  kinds[i] = kind;
  hts[i] = Math.max(0, Math.min(255, Math.round((bestY + 2) / 0.1)));
}
const spawn = L.spawnPads.map(([x, , z]) => [Math.round((z - bx.minZ) / CELL), Math.round((bx.maxX - x) / CELL)]);

// ---- colours: the blocks' own, merged down to 16 (the closest pair goes to the more common one)
const hex = (c) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16));
const key = (c) => hex(c).join();
let pal = [...new Set(col.map(key))].map((k) => k.split(',').map(Number));
while (pal.length > 16) {
  let bi = 0, bj = 1, bd = Infinity;
  for (let i = 0; i < pal.length; i++) for (let j = i + 1; j < pal.length; j++) {
    const d = pal[i].reduce((s, v, k) => s + (v - pal[j][k]) ** 2, 0);
    if (d < bd) { bd = d; bi = i; bj = j; }
  }
  const n = (p) => col.filter((c) => key(c) === p.join()).length;
  if (n(pal[bi]) < n(pal[bj])) [bi, bj] = [bj, bi];
  const gone = pal[bj].join(), keep = '#' + pal[bi].map((v) => v.toString(16).padStart(2, '0')).join('');
  for (let i = 0; i < col.length; i++) if (key(col[i]) === gone) col[i] = keep;
  pal.splice(bj, 1);
}
const look = new Uint8Array(GW * GH);
for (let i = 0; i < look.length; i++) look[i] = (kinds[i] << 6) | (surf[i] << 4) | pal.findIndex((p) => p.join() === key(col[i]));
// ---- light: the sun (high in the sky, from the -x, -y side), shadows marched over the heightfield
const SUN = (() => { const v = [-0.45, -0.55, 0.7], l = Math.hypot(...v); return v.map((k) => k / l); })();
const topAt2 = (x, y) => { const gx = Math.floor(x / CELL), gy = Math.floor(y / CELL); return gx < 0 || gy < 0 || gx >= GW || gy >= GH ? -1.6 : hts[gy * GW + gx] * 0.1 - 2; };
const light = new Uint8Array(GW * GH);
for (let gy = 0; gy < GH; gy++) for (let gx = 0; gx < GW; gx++) {
  const i = gy * GW + gx, k = kinds[i], z0 = hts[i] * 0.1 - 2;
  let v = 0;
  if (k !== WATER) for (let q = 0; q < 4; q++) {
    const x0 = (gx + 0.25 + 0.5 * (q & 1)) * CELL, y0 = (gy + 0.25 + 0.5 * (q >> 1)) * CELL;
    for (let d = 0.3; d < 30; d += 0.2) {
      const z = z0 + 0.02 + SUN[2] * d;
      if (z > 14) break;
      if (topAt2(x0 + SUN[0] * d, y0 + SUN[1] * d) > z) { v |= 1 << q; break; }
    }
  }
  const nb = [[0, -1], [0, 1], [-1, 0], [1, 0]];
  nb.forEach(([dx, dy], b) => {
    const x = gx + dx, y = gy + dy, inside = x >= 0 && y >= 0 && x < GW && y < GH, j = y * GW + x;
    if (k === WATER) { if (inside && kinds[j] !== WATER) v |= 16 << b; }
    else if (k !== WALL && inside && (kinds[j] === WALL || hts[j] > hts[i] + 3)) v |= 16 << b;
  });
  light[i] = v;
}
const rgb565 = ([r, g, b]) => ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
console.error(`${stage}: ${GW} x ${GH} columns, spawns ${JSON.stringify(spawn)}, ${pal.length} colours`);

const rows = (a) => { let s = ''; for (let i = 0; i < a.length; i += 32) s += '  ' + Array.from(a.slice(i, i + 32)).join(',') + ',\n'; return s; };
let out = `/* Generated by tools/make_map.mjs from Inkwave's ${stage} layout (MIT, Jayden Davis). Do not edit. */\n`;
out += `#define MAP_W ${GW}\n#define MAP_H ${GH}\n`;
out += `static const short map_spawn[2][2] = {{${spawn[0]}}, {${spawn[1]}}};\n`;
out += `static const unsigned short map_pal[16] = {${pal.map(rgb565).join(',')}};\n`;
out += `static const unsigned char map_h[MAP_W * MAP_H] = {\n${rows(hts)}};\n`;
out += `static const unsigned char map_look[MAP_W * MAP_H] = {\n${rows(look)}};\n`;
out += `static const unsigned char map_light[MAP_W * MAP_H] = {
${rows(light)}};
`;
out += `static const float map_sun[3] = {${SUN.map((v) => v.toFixed(4) + 'f').join(', ')}};
`;
process.stdout.write(out);

if (png) {   // preview: 4 px per column, brighter = higher
  const S = 4, W = GW * S, H = GH * S, raw = Buffer.alloc((W * 3 + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W * 3 + 1)] = 0;
    for (let x = 0; x < W; x++) {
      const i = (y / S | 0) * GW + (x / S | 0), k = kinds[i], sh = Math.min(1.3, 0.7 + hts[i] / 150);
      const shd = (light[i] & 15) ? 0.7 : 1;
      const c0 = k === WATER ? [40, 90, 150] : k === RAIL ? [200, 60, 60] : hex(col[i]).map((v) => Math.min(255, v * sh * (k === WALL ? 0.6 : 1)));
      const c = c0.map((v) => v * shd);
      raw.set(c, y * (W * 3 + 1) + 1 + x * 3);
    }
  }
  const crc = (buf) => { let c = ~0; for (const b of buf) { c ^= b; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1)); } return ~c >>> 0; };
  const chunk = (t, d) => { const len = Buffer.alloc(4); len.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
  writeFileSync(png, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
