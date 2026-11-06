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
  fs.writeFileSync(path.join(OUT, "index.json"), JSON.stringify({ apps: bundled }, null, 2));
  fs.writeFileSync(path.join(ROOT, "catalog.json"), JSON.stringify({ apps: online }, null, 2) + "\n");
})().catch((e) => { console.error(e); process.exit(1); });
