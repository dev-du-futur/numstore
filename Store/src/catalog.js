// Where apps come from:
//  - bundled: shipped inside the store (catalog/ folder, made by scripts/build-catalog.js)
//  - online:  catalog.json at the root of the GitHub repo; new or newer apps are
//             downloaded on demand and cached
//  - library: .nwa files the user added from their computer
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

function newer(a, b) { // semver-ish: is a > b ?
  const pa = String(a || "0").split(".").map(Number), pb = String(b || "0").split(".").map(Number);
  for (let i = 0; i < 3; i++) if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0);
  return false;
}

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
    return idx.apps.map((a) => ({
      ...a,
      source: "bundled",
      nwaPath: path.join(this.bundledDir, a.id, "app.nwa"),
      dataPath: a.data ? path.join(this.bundledDir, a.id, "data.bin") : null,
      iconPath: path.join(this.bundledDir, a.id, "icon.png"),
    }));
  }

  cached(id) {
    const dir = path.join(this.cacheDir, id);
    const meta = readJSON(path.join(dir, "meta.json"), null);
    return meta ? { meta, dir } : null;
  }

  library() {
    return fs.readdirSync(this.libDir).map((id) => {
      const dir = path.join(this.libDir, id);
      const m = readJSON(path.join(dir, "meta.json"), null);
      if (!m) return null;
      return {
        ...m, id, source: "library",
        nwaPath: path.join(dir, "app.nwa"),
        dataPath: m.hasData ? path.join(dir, "data.bin") : null,
        iconPath: null,
      };
    }).filter(Boolean);
  }

  async refreshRemote() {
    try {
      const idx = JSON.parse((await fetchBuf(REMOTE_INDEX)).toString("utf8"));
      this.remote = idx.apps || [];
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
      const c = this.cached(r.id);
      if (c && c.meta.version === r.version) {
        if (!have || newer(r.version, have.version))
          byId.set(r.id, { ...r, source: "online", downloaded: true, ...this.paths(c.dir, r) });
      } else if (!have || newer(r.version, have.version)) {
        byId.set(r.id, { ...r, source: "online", downloaded: false, iconUrl: r.icon });
      }
    }
    for (const l of this.library()) if (!byId.has(l.id)) byId.set(l.id, l);
    return [...byId.values()];
  }

  paths(dir, r) {
    return {
      nwaPath: path.join(dir, "app.nwa"),
      dataPath: r.data ? path.join(dir, "data.bin") : null,
      iconPath: fs.existsSync(path.join(dir, "icon.png")) ? path.join(dir, "icon.png") : null,
    };
  }

  // make sure an online app's files are on disk; returns the entry with paths
  async fetchApp(app, onStep) {
    if (app.source !== "online" || app.downloaded) return app;
    const dir = path.join(this.cacheDir, app.id);
    fs.mkdirSync(dir, { recursive: true });
    onStep && onStep(`Downloading ${app.name}…`);
    fs.writeFileSync(path.join(dir, "app.nwa"), await fetchBuf(app.nwa));
    if (app.data) fs.writeFileSync(path.join(dir, "data.bin"), await fetchBuf(app.data));
    if (app.icon) try { fs.writeFileSync(path.join(dir, "icon.png"), await fetchBuf(app.icon)); } catch (e) {}
    fs.writeFileSync(path.join(dir, "meta.json"), JSON.stringify({ version: app.version }));
    return { ...app, downloaded: true, ...this.paths(dir, app) };
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

module.exports = { Catalog };
