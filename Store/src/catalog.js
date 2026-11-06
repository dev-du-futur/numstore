// Where apps come from:
//  - bundled: shipped inside the store (catalog/ folder, made by scripts/build-catalog.js)
//  - online:  catalog.json at the root of the GitHub repo; new or newer apps are
//             downloaded on demand and cached
//  - library: .nwa files the user added from their computer
//
// Every app has one or more variants (e.g. TinyChat "small" and "full"): same .nwa,
// different data file. Apps without variants get a single "default" one.
const fs = require("fs");
const path = require("path");
const https = require("https");

const REMOTE_INDEX = "https://raw.githubusercontent.com/dev-du-futur/numstore/main/catalog.json";

function readJSON(p, def) {
  try { return JSON.parse(fs.readFileSync(p, "utf8")); } catch (e) { return def; }
}

function fetchBuf(url, redirects = 5) {
  return new Promise((res, rej) => {
    const req = https.get(url, { headers: { "User-Agent": "numstore" }, timeout: 15000 }, (r) => {
      if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location && redirects > 0) {
        r.resume();
        return res(fetchBuf(new URL(r.headers.location, url).toString(), redirects - 1));
      }
      if (r.statusCode !== 200) { r.resume(); return rej(new Error(`HTTP ${r.statusCode}`)); }
      const chunks = [];
      r.on("data", (c) => chunks.push(c));
      r.on("end", () => res(Buffer.concat(chunks)));
    });
    req.on("timeout", () => req.destroy(new Error("timeout")));
    req.on("error", rej);
  });
}

function newer(a, b) { // is version a > b ?
  const pa = String(a || "0").split(".").map(Number), pb = String(b || "0").split(".").map(Number);
  for (let i = 0; i < 3; i++) if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0);
  return false;
}

const exists = (p) => p && fs.existsSync(p);

class Catalog {
  constructor(bundledDir, dataDir) {
    this.bundledDir = bundledDir;
    this.cacheDir = path.join(dataDir, "apps");
    this.libDir = path.join(dataDir, "library");
    fs.mkdirSync(this.cacheDir, { recursive: true });
    fs.mkdirSync(this.libDir, { recursive: true });
    this.remote = [];
    this.remoteError = null;
  }

  bundled() {
    const idx = readJSON(path.join(this.bundledDir, "index.json"), { apps: [] });
    return idx.apps.map((a) => {
      const dir = path.join(this.bundledDir, a.id);
      return {
        ...a,
        source: "bundled",
        nwaPath: path.join(dir, "app.nwa"),
        iconPath: path.join(dir, "icon.png"),
        variants: a.variants.map((v) => ({
          ...v, ready: true,
          dataPath: v.data ? path.join(dir, `data-${v.id}.bin`) : null,
        })),
      };
    });
  }

  // an online app, with whatever of it is already cached
  online(r) {
    const dir = path.join(this.cacheDir, r.id);
    const meta = readJSON(path.join(dir, "meta.json"), {});
    const same = meta.version === r.version;
    const nwaPath = path.join(dir, "app.nwa");
    return {
      ...r,
      source: "online",
      dir,
      nwaPath: same && exists(nwaPath) ? nwaPath : null,
      iconPath: exists(path.join(dir, "icon.png")) ? path.join(dir, "icon.png") : null,
      iconUrl: r.icon,
      variants: r.variants.map((v) => {
        const dp = path.join(dir, `data-${v.id}.bin`);
        const ready = same && exists(nwaPath) && (!v.data || exists(dp));
        return { ...v, url: v.data, dataPath: v.data ? dp : null, ready };
      }),
    };
  }

  library() {
    return fs.readdirSync(this.libDir).map((id) => {
      const dir = path.join(this.libDir, id);
      const m = readJSON(path.join(dir, "meta.json"), null);
      if (!m) return null;
      return {
        ...m, id, source: "library",
        nwaPath: path.join(dir, "app.nwa"),
        iconPath: null,
        variants: [{ id: "default", label: "", size: m.size, ready: true,
          dataPath: m.hasData ? path.join(dir, "data.bin") : null }],
      };
    }).filter(Boolean);
  }

  async refreshRemote() {
    try {
      const idx = JSON.parse((await fetchBuf(REMOTE_INDEX)).toString("utf8"));
      this.remote = (idx.apps || []).filter((a) => Array.isArray(a.variants));
      this.remoteError = null;
    } catch (e) {
      this.remoteError = e.message;
    }
  }

  // Every app the store can install, one entry per id (newest version wins)
  all() {
    const byId = new Map();
    for (const a of this.bundled()) byId.set(a.id, a);
    for (const r of this.remote) {
      const have = byId.get(r.id);
      if (!have || newer(r.version, have.version)) byId.set(r.id, this.online(r));
    }
    for (const l of this.library()) if (!byId.has(l.id)) byId.set(l.id, l);
    return [...byId.values()];
  }

  // make sure one variant of an app is on disk; returns { nwa, data }
  async fetchVariant(app, variantId, onStep) {
    const v = app.variants.find((x) => x.id === variantId) || app.variants[0];
    if (app.source === "online" && !v.ready) {
      fs.mkdirSync(app.dir, { recursive: true });
      onStep && onStep(`Downloading ${app.name}…`);
      const meta = readJSON(path.join(app.dir, "meta.json"), {});
      if (meta.version !== app.version || !exists(path.join(app.dir, "app.nwa"))) {
        for (const f of fs.readdirSync(app.dir)) fs.rmSync(path.join(app.dir, f), { force: true });
        fs.writeFileSync(path.join(app.dir, "app.nwa"), await fetchBuf(app.nwa));
        if (app.icon) try { fs.writeFileSync(path.join(app.dir, "icon.png"), await fetchBuf(app.icon)); } catch (e) {}
      }
      if (v.url) fs.writeFileSync(v.dataPath, await fetchBuf(v.url));
      fs.writeFileSync(path.join(app.dir, "meta.json"), JSON.stringify({ version: app.version }));
    }
    return { nwa: path.join(app.source === "online" ? app.dir : path.dirname(app.nwaPath), "app.nwa"), data: v.dataPath };
  }

  addToLibrary(meta, nwaFile, dataFile) {
    const id = "file-" + meta.name.toLowerCase().replace(/[^a-z0-9]+/g, "-");
    const dir = path.join(this.libDir, id);
    fs.mkdirSync(dir, { recursive: true });
    fs.copyFileSync(nwaFile, path.join(dir, "app.nwa"));
    if (dataFile) fs.copyFileSync(dataFile, path.join(dir, "data.bin"));
    fs.writeFileSync(path.join(dir, "meta.json"), JSON.stringify({ ...meta, hasData: !!dataFile }));
    return id;
  }

  forget(id) {
    if (!id.startsWith("file-")) return;
    fs.rmSync(path.join(this.libDir, id), { recursive: true, force: true });
  }
}

// Which variant of an app is installed: the one whose size matches what the calculator holds
function variantFor(app, installedSize) {
  if (!app || !app.variants.length) return null;
  let best = app.variants[0], d = Infinity;
  for (const v of app.variants) {
    const dd = Math.abs((v.size || 0) - installedSize);
    if (dd < d) { d = dd; best = v; }
  }
  return best.id;
}

module.exports = { Catalog, variantFor };
