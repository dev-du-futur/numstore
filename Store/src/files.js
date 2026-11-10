// Game progress: the files apps keep in the calculator's file list (Epsilon's "storage",
// in RAM), kept on this PC so they survive an install, a reset, or the app being removed.
//
// The storage is a list of records: a 16-bit size (the whole record), the name ("base.ext"
// and a zero), then the content; a zero size ends the list. It sits between two magic
// words, 0xEE0BDDBA. Installing apps and resetting the calculator empty it (installs keep
// Python scripts only), so Numstore copies the files before every change and puts back,
// afterwards, the ones of apps that are on the calculator. It never overwrites a file the
// calculator already has, and only ever adds files at the end of the list: nothing moves.
// Only files that belong to an app Numstore knows are kept (by name: "rollmark.sav" is
// Rollmark's); Python scripts and the calculator's own files are left as they are.
//
// On this PC: userData/progress/<calculator>/<file name> + index.json
// { files: { "rollmark.sav": { app: "Rollmark", size, savedAt } } }
const fs = require("fs");
const path = require("path");

const MAGIC = 0xee0bddba;
// Epsilon's own settings: never copied back (they belong to the system, not to a game)
const SYSTEM = /\.(sys)$/i;

// buffer = the storage from its first magic word on (size + 8 bytes)
function parse(buf, size) {
  if (buf.length < size + 8 || buf.readUInt32LE(0) !== MAGIC || buf.readUInt32LE(4 + size) !== MAGIC)
    throw new Error("the calculator's file list isn't where Numstore expects it");
  const records = [];
  let p = 4;
  const end = 4 + size;
  while (p + 2 <= end) {
    const n = buf.readUInt16LE(p);
    if (!n) return { records, used: p - 4, end: p - 4 };
    const z = buf.indexOf(0, p + 2);
    if (n < 4 || p + n > end || z < 0 || z >= p + n) throw new Error("the calculator's file list looks damaged");
    records.push({ name: buf.toString("latin1", p + 2, z), data: Buffer.from(buf.subarray(z + 1, p + n)) });
    p += n;
  }
  throw new Error("the calculator's file list has no end");
}

// the bytes to write at `end` (offset in the record area) to add these records, with the
// zero size that ends the list; null if they don't fit
function appended(records, end, size) {
  const parts = records.map((r) => {
    const name = Buffer.from(r.name + "\0", "latin1");
    const head = Buffer.alloc(2);
    head.writeUInt16LE(2 + name.length + r.data.length);
    return Buffer.concat([head, name, r.data]);
  });
  const out = Buffer.concat([...parts, Buffer.alloc(2)]);
  return end + out.length <= size ? out : null;
}

// which app a file belongs to: its name starts like the app's ("rollmark.sav" -> Rollmark,
// "mrbullet_saves.py" -> Mr Bullet), or like its catalog id or .nwa file name
const squash = (s) => String(s || "").toLowerCase().replace(/[^a-z0-9]/g, "");
function appFor(fileName, apps) {
  const base = squash(fileName.replace(/\.[^.]*$/, ""));
  if (base.length < 3) return null;
  // the same name first, then a file name longer than the app's ("mrbullet_saves": the
  // longest app name wins), then a shortened one (the shortest app name wins)
  let best = null, score = [0, 0];
  for (const a of apps) {
    const keys = [a.name, (a.id || "").replace(/^numplay-/, ""), a.nwa ? path.basename(String(a.nwa), ".nwa") : ""]
      .map(squash).filter((k) => k.length >= 3);
    for (const k of keys) {
      const s = k === base ? [3, 0] : base.startsWith(k) ? [2, k.length] : k.startsWith(base) ? [1, -k.length] : null;
      if (s && (s[0] > score[0] || (s[0] === score[0] && s[1] > score[1]))) { best = a.name; score = s; }
    }
  }
  return best;
}

class Progress {
  constructor(dir) { this.root = dir; }
  dir(calc) { return path.join(this.root, String(calc).replace(/[^A-Za-z0-9_-]/g, "_")); }
  index(calc) {
    try { return JSON.parse(fs.readFileSync(path.join(this.dir(calc), "index.json"), "utf8")); }
    catch (e) { return { files: {} }; }
  }
  saveIndex(calc, idx) {
    fs.mkdirSync(this.dir(calc), { recursive: true });
    const f = path.join(this.dir(calc), "index.json");
    fs.writeFileSync(f + ".tmp", JSON.stringify(idx, null, 2));
    fs.renameSync(f + ".tmp", f);
  }
  // copy the calculator's files (the newest version wins); returns the names that changed
  keep(calc, records, apps) {
    const idx = this.index(calc);
    const changed = [];
    for (const r of records) {
      if (SYSTEM.test(r.name) || /[\\/:*?"<>|]/.test(r.name) || r.name.startsWith(".")) continue;
      const app = appFor(r.name, apps);
      if (!app) continue;
      const old = idx.files[r.name];
      const file = path.join(this.dir(calc), r.name);
      if (old && old.size === r.data.length && fs.existsSync(file) && fs.readFileSync(file).equals(r.data)) {
        if (old.app !== app) { old.app = app; changed.push(r.name); }
        continue;
      }
      fs.mkdirSync(this.dir(calc), { recursive: true });
      fs.writeFileSync(file + ".tmp", r.data);
      fs.renameSync(file + ".tmp", file);
      idx.files[r.name] = { app, size: r.data.length, savedAt: Date.now() };
      changed.push(r.name);
    }
    if (changed.length) this.saveIndex(calc, idx);
    return changed;
  }
  // the kept files of apps on the calculator that it doesn't have
  wanted(calc, onCalc, installedNames) {
    const idx = this.index(calc);
    const have = new Set(onCalc.map((r) => r.name));
    const out = [];
    for (const [name, f] of Object.entries(idx.files)) {
      if (have.has(name)) continue;
      if (!f.app || !installedNames.includes(f.app)) continue;
      const file = path.join(this.dir(calc), name);
      if (!fs.existsSync(file)) continue;
      out.push({ name, app: f.app, data: fs.readFileSync(file) });
    }
    return out.sort((a, b) => a.data.length - b.data.length); // small ones first if space runs out
  }
  // files that couldn't be put back (no room) aren't taken for deleted (see prune)
  mark(calc, names, unplaced) {
    const idx = this.index(calc);
    for (const n of names) if (idx.files[n]) idx.files[n].unplaced = unplaced || undefined;
    this.saveIndex(calc, idx);
  }
  // an app that's on the calculator but no longer has one of its files deleted it (a new
  // game, a game over): forget it too, or it would come back at the next install
  prune(calc, onCalc, installedNames) {
    const idx = this.index(calc);
    const have = new Set(onCalc.map((r) => r.name));
    const gone = Object.entries(idx.files)
      .filter(([name, f]) => f.app && installedNames.includes(f.app) && !have.has(name) && !f.unplaced)
      .map(([name]) => name);
    for (const name of gone) {
      try { fs.unlinkSync(path.join(this.dir(calc), name)); } catch (e) {}
      delete idx.files[name];
    }
    if (gone.length) this.saveIndex(calc, idx);
    return gone;
  }
  // per app: { "Rollmark": { files: [...], size, savedAt } } (files of no known app left out)
  byApp(calc) {
    const out = {};
    for (const [name, f] of Object.entries(this.index(calc).files)) {
      if (!f.app) continue;
      const a = out[f.app] || (out[f.app] = { files: [], size: 0, savedAt: 0 });
      a.files.push(name); a.size += f.size; a.savedAt = Math.max(a.savedAt, f.savedAt);
    }
    return out;
  }
  forget(calc, app) {
    const idx = this.index(calc);
    for (const [name, f] of Object.entries(idx.files)) {
      if (f.app !== app) continue;
      try { fs.unlinkSync(path.join(this.dir(calc), name)); } catch (e) {}
      delete idx.files[name];
    }
    this.saveIndex(calc, idx);
  }
}

module.exports = { parse, appended, appFor, Progress, MAGIC };
