// Packs every app in ../Apps (each folder with a numstore.json) into:
//   Store/catalog/        bundled with the store (index.json + <id>/app.nwa, data.bin, icon.png)
//   ../catalog.json       the online catalog the store reads from GitHub (raw file URLs)
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
    const nwa = path.join(base, m.nwa), data = m.data ? path.join(base, m.data) : null;
    const size = await installer.measure(nwa, data, os.tmpdir());
    const dest = path.join(OUT, m.id);
    fs.mkdirSync(dest);
    fs.copyFileSync(nwa, path.join(dest, "app.nwa"));
    if (data) fs.copyFileSync(data, path.join(dest, "data.bin"));
    if (m.icon) fs.copyFileSync(path.join(base, m.icon), path.join(dest, "icon.png"));
    const entry = { id: m.id, name: m.name, version: m.version, author: m.author, category: m.category, description: m.description, size };
    bundled.push({ ...entry, data: !!data });
    const url = (p) => RAW + ["Apps", dir, p].map(encodeURIComponent).join("/").replace(/%2F/g, "/");
    online.push({ ...entry, nwa: url(m.nwa), data: data ? url(m.data) : null, icon: m.icon ? url(m.icon) : null });
    console.log(`${m.name} ${m.version}: ${(size / 1048576).toFixed(2)} MB`);
  }
  fs.writeFileSync(path.join(OUT, "index.json"), JSON.stringify({ apps: bundled }, null, 2));
  fs.writeFileSync(path.join(ROOT, "catalog.json"), JSON.stringify({ apps: online }, null, 2) + "\n");
})().catch((e) => { console.error(e); process.exit(1); });
