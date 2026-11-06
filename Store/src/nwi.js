// NumWorks icons (NWI): an LZ4 block of 55x56 RGB565 pixels, little-endian.
const W = 55, H = 56;

function lz4Block(src, outLen) {
  const out = Buffer.alloc(outLen);
  let i = 0, o = 0;
  while (i < src.length && o < outLen) {
    const tok = src[i++];
    let lit = tok >> 4;
    if (lit === 15) { let b; do { b = src[i++]; lit += b; } while (b === 255); }
    src.copy(out, o, i, i + lit); i += lit; o += lit;
    if (i >= src.length || o >= outLen) break;
    const off = src[i] | (src[i + 1] << 8); i += 2;
    let len = (tok & 15) + 4;
    if ((tok & 15) === 15) { let b; do { b = src[i++]; len += b; } while (b === 255); }
    for (let k = 0; k < len && o < outLen; k++, o++) out[o] = out[o - off];
  }
  return out;
}

// -> { width, height, rgba: Buffer } or null
function decode(nwi) {
  try {
    const px = lz4Block(nwi, W * H * 2);
    const rgba = Buffer.alloc(W * H * 4);
    for (let p = 0; p < W * H; p++) {
      const v = px[2 * p] | (px[2 * p + 1] << 8);
      const r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
      rgba[4 * p] = (r << 3) | (r >> 2);
      rgba[4 * p + 1] = (g << 2) | (g >> 4);
      rgba[4 * p + 2] = (b << 3) | (b >> 2);
      rgba[4 * p + 3] = 255;
    }
    return { width: W, height: H, rgba };
  } catch (e) {
    return null;
  }
}

module.exports = { decode };
