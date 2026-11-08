// Packs every app in ../Apps (each folder with a numstore.json) into:
//   Store/catalog/        bundled with the store (index.json + <id>/app.nwa, data-<variant>.bin, icon.png)
//   ../catalog.json       the online catalog the store reads from GitHub (raw file URLs)
// numstore.json either names one "data" file or a list of "variants", each with its own data.
// Sizes are measured with nwlink (linked and padded to 64 KB, as on the calculator).
const fs = require("fs");
const path = require("path");
const os = require("os");
const installer = require("../src/installer");

const ROOT = path.resolve(__dirname, "..", "..");
const OUT = path.resolve(__dirname, "..", "catalog");
const RAW = "https://raw.githubusercontent.com/dev-du-futur/numstore/main/";

(async () => {
  installer.setDataDir(path.join(os.tmpdir(), "numstore-build"));
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  const bundled = [], online = [];
  for (const dir of fs.readdirSync(path.join(ROOT, "Apps"))) {
    const base = path.join(ROOT, "Apps", dir);
    const mf = path.join(base, "numstore.json");
    if (!fs.existsSync(mf)) continue;
    const m = JSON.parse(fs.readFileSync(mf, "utf8"));
    const url = (p) => RAW + ["Apps", dir, ...p.split("/")].map(encodeURIComponent).join("/");
    const nwa = path.join(base, m.nwa);
    const variants = m.variants || [{ id: "default", label: "", data: m.data || null }];
    const dest = path.join(OUT, m.id);
    fs.mkdirSync(dest);
    fs.copyFileSync(nwa, path.join(dest, "app.nwa"));
    if (m.icon) fs.copyFileSync(path.join(base, m.icon), path.join(dest, "icon.png"));
    const vb = [], vo = [];
    for (const v of variants) {
      const data = v.data ? path.join(base, v.data) : null;
      const size = await installer.measure(nwa, data, os.tmpdir());
      if (data) fs.copyFileSync(data, path.join(dest, `data-${v.id}.bin`));
      const e = { id: v.id, label: v.label || "", description: v.description || "", size };
      vb.push({ ...e, data: !!data });
      vo.push({ ...e, data: data ? url(v.data) : null });
      console.log(`${m.name} ${m.version} ${v.id}: ${(size / 1048576).toFixed(2)} MB`);
    }
    const entry = { id: m.id, name: m.name, version: m.version, author: m.author, category: m.category,
      description: m.description, defaultVariant: m.defaultVariant || variants[0].id };
    bundled.push({ ...entry, variants: vb });
    online.push({ ...entry, nwa: url(m.nwa), icon: m.icon ? url(m.icon) : null, variants: vo });
  }
  await linkedGames(online);
  fs.writeFileSync(path.join(OUT, "index.json"), JSON.stringify({ apps: bundled }, null, 2));
  fs.writeFileSync(path.join(ROOT, "catalog.json"), JSON.stringify({ apps: online }, null, 2) + "\n");
})().catch((e) => { console.error(e); process.exit(1); });

// ---- games linked from another project's GitHub release (Apps/NumPlay/numplay.json) ----
// Online only, never bundled: the store downloads the .nwa from that release. Only the
// icon (read out of the .nwa) is kept here, so the store can show it.
async function linkedGames(online) {
  const mf = path.join(ROOT, "Apps", "NumPlay", "numplay.json");
  if (!fs.existsSync(mf)) return;
  const m = JSON.parse(fs.readFileSync(mf, "utf8"));
  const cache = path.join(os.tmpdir(), "numstore-build", "numplay", m.tag);
  const icons = path.join(ROOT, "Apps", "NumPlay", "icons");
  fs.mkdirSync(cache, { recursive: true });
  fs.mkdirSync(icons, { recursive: true });
  const nwi = require("../src/nwi");
  // fan games (fangames.json): own category, and our own icons, never the original game's art
  const ff = path.join(ROOT, "Apps", "NumPlay", "fangames.json");
  const fans = fs.existsSync(ff) ? JSON.parse(fs.readFileSync(ff, "utf8")).games.map((g) => ({ ...g, fan: true })) : [];
  const APPS_SPACE = 2555904; // N0120 apps region: bigger apps can never be installed
  for (const g of [...m.games, ...fans]) {
    const id = "numplay-" + g.file.replace(/\.nwa$/, "").toLowerCase();
    const url = `https://github.com/${m.repo}/releases/download/${m.tag}/${g.file}`;
    const file = path.join(cache, g.file);
    if (!fs.existsSync(file)) {
      // GitHub sometimes answers with an error (or an HTML error page): try a few times,
      // and only keep real apps (ELF files)
      let buf = null;
      for (let t = 0; t < 4 && !buf; t++) {
        if (t) await new Promise((r) => setTimeout(r, 5000 * t));
        try { const b = await download(url); if (b.length > 4 && b.readUInt32BE(0) === 0x7f454c46) buf = b; } catch (e) {}
      }
      if (!buf) throw new Error(`${url}: not a NumWorks app after 4 tries (GitHub error page?)`);
      fs.writeFileSync(file, buf);
    }
    const size = await installer.measure(file, null, os.tmpdir());
    if (size > APPS_SPACE) { console.log(`${g.name}: ${(size / 1048576).toFixed(2)} MB, too big for the calculator, skipped`); continue; }
    let iconName = null;
    if (g.fan) {
      iconName = "fan-" + g.file.replace(/\.nwa$/, "").toLowerCase() + ".png"; // tools/make_fan_icons.py
      if (!fs.existsSync(path.join(icons, iconName))) iconName = null;
    } else {
      const icon = elfSection(fs.readFileSync(file), ".rodata.eadk_app_icon");
      const img = icon && nwi.decode(icon);
      if (img) { iconName = id + ".png"; fs.writeFileSync(path.join(icons, iconName), png(img.width, img.height, img.rgba)); }
    }
    online.push({
      id, name: g.name, version: m.tag.replace(/^v/, ""), author: m.author, category: g.fan ? "Fan games" : "Games",
      description: (g.fan ? "Unofficial fan game. " : "") + g.description, defaultVariant: "default",
      license: m.license, homepage: `https://github.com/${m.repo}`, credit: g.credit || "",
      nwa: url, icon: iconName ? RAW + `Apps/NumPlay/icons/${iconName}` : null,
      variants: [{ id: "default", label: "", description: "", size, data: null }],
    });
    console.log(`${g.name} (NumPlay ${m.tag}): ${(size / 1048576).toFixed(2)} MB`);
  }
}

function download(url, redirects = 5) {
  const https = require("https");
  return new Promise((res, rej) => {
    https.get(url, { headers: { "User-Agent": "numstore-build" } }, (r) => {
      if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location && redirects > 0) {
        r.resume();
        return res(download(new URL(r.headers.location, url).toString(), redirects - 1));
      }
      if (r.statusCode !== 200) { r.resume(); return rej(new Error(`${url}: HTTP ${r.statusCode}`)); }
      const c = [];
      r.on("data", (d) => c.push(d)).on("end", () => res(Buffer.concat(c))).on("error", rej);
    }).on("error", rej);
  });
}

// contents of a named section of a 32-bit little-endian ELF file (a .nwa)
function elfSection(b, name) {
  const shoff = b.readUInt32LE(32), shentsize = b.readUInt16LE(46), shnum = b.readUInt16LE(48), shstrndx = b.readUInt16LE(50);
  const sh = (i) => shoff + i * shentsize;
  const strOff = b.readUInt32LE(sh(shstrndx) + 16);
  for (let i = 0; i < shnum; i++) {
    const n = b.readUInt32LE(sh(i));
    const end = b.indexOf(0, strOff + n);
    if (b.toString("latin1", strOff + n, end) === name) {
      const off = b.readUInt32LE(sh(i) + 16), size = b.readUInt32LE(sh(i) + 20);
      return b.subarray(off, off + size);
    }
  }
  return null;
}

// minimal RGBA PNG encoder
function png(w, h, rgba) {
  const zlib = require("zlib");
  const crcTable = [...Array(256)].map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (buf) => { let c = 0xffffffff; for (const x of buf) c = crcTable[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, data) => {
    const t = Buffer.concat([Buffer.from(type), data]);
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(t));
    return Buffer.concat([len, t, c]);
  };
  const hdr = Buffer.alloc(13);
  hdr.writeUInt32BE(w, 0); hdr.writeUInt32BE(h, 4); hdr[8] = 8; hdr[9] = 6;
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", hdr),
    chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
