// Writes apps to the calculator with NumWorks' own `nwlink` tool, which links each
// .nwa app at its place in flash and writes them all in one go (it replaces every
// installed app, so "remove" = install the apps that stay).
//
// nwlink is not bundled (its licence keeps all rights with NumWorks): the store uses
// a copy already installed with npm, or downloads the official package from the npm
// registry once, into the store's own data folder. Its helper packages (usb,
// commander, cli-progress, all MIT) ship with the store.
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const https = require("https");
const tar = require("tar-stream");

const NWLINK_TGZ = "https://registry.npmjs.org/nwlink/-/nwlink-1.0.0.tgz";

let dataDir = null;
function setDataDir(d) { dataDir = d; }

function candidates() {
  const list = [];
  if (dataDir) list.push(path.join(dataDir, "nwlink", "package"));
  if (process.env.APPDATA) list.push(path.join(process.env.APPDATA, "npm", "node_modules", "nwlink"));
  list.push("/usr/local/lib/node_modules/nwlink", "/opt/homebrew/lib/node_modules/nwlink");
  return list;
}

function findNwlink() {
  for (const dir of candidates()) {
    const js = path.join(dir, "bin", "nwlink");
    if (fs.existsSync(js)) return js;
  }
  return null;
}

function get(url, redirects = 5) {
  return new Promise((res, rej) => {
    https.get(url, { headers: { "User-Agent": "numstore" } }, (r) => {
      if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location && redirects > 0) {
        r.resume();
        return res(get(new URL(r.headers.location, url).toString(), redirects - 1));
      }
      if (r.statusCode !== 200) { r.resume(); return rej(new Error(`download failed (${r.statusCode})`)); }
      const chunks = [];
      r.on("data", (c) => chunks.push(c));
      r.on("end", () => res(Buffer.concat(chunks)));
      r.on("error", rej);
    }).on("error", rej);
  });
}

async function downloadNwlink() {
  const dest = path.join(dataDir, "nwlink");
  const tgz = await get(NWLINK_TGZ);
  const files = await new Promise((res, rej) => {
    const ex = tar.extract();
    const out = [];
    ex.on("entry", (h, stream, next) => {
      const chunks = [];
      stream.on("data", (c) => chunks.push(c));
      stream.on("end", () => { if (h.type === "file") out.push([h.name, Buffer.concat(chunks)]); next(); });
      stream.resume();
    });
    ex.on("finish", () => res(out));
    ex.on("error", rej);
    ex.end(zlib.gunzipSync(tgz));
  });
  for (const [name, buf] of files) {
    const p = path.join(dest, path.normalize(name));
    if (!p.startsWith(dest + path.sep)) continue; // never write outside the folder
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, buf);
  }
  const js = findNwlink();
  if (!js) throw new Error("nwlink download looked wrong");
  return js;
}

async function ensureNwlink(onStep) {
  const js = findNwlink();
  if (js) return js;
  onStep && onStep("Downloading NumWorks' install tool (once)…");
  return downloadNwlink();
}

// node_modules folder holding usb/commander/cli-progress for nwlink
function modulesDir() {
  const here = path.join(__dirname, "..", "node_modules");
  return here.replace("app.asar", "app.asar.unpacked");
}

function run(js, args, onLine) {
  return new Promise((res, rej) => {
    const env = { ...process.env, ELECTRON_RUN_AS_NODE: "1", NODE_PATH: modulesDir() };
    const p = spawn(process.execPath, [js, ...args], { env, windowsHide: true });
    let out = "";
    const feed = (d) => {
      const s = d.toString();
      out += s;
      for (const line of s.split(/[\r\n]+/)) if (line.trim()) onLine && onLine(line.trim());
    };
    p.stdout.on("data", feed);
    p.stderr.on("data", feed);
    p.on("error", rej);
    p.on("close", (code) => (code === 0 ? res(out) : rej(new Error(lastError(out) || `nwlink failed (${code})`))));
  });
}

function lastError(out) {
  const lines = out.split(/[\r\n]+/).map((l) => l.trim()).filter(Boolean);
  const err = lines.filter((l) => /error|fail|no device|not found|overflow/i.test(l));
  return (err.length ? err[err.length - 1] : lines[lines.length - 1] || "").replace(/^Error:\s*/, "");
}

// apps: [{ nwa: path, data: path|null }] in install order
async function install(apps, onStep) {
  const js = await ensureNwlink(onStep);
  onStep && onStep("Writing to the calculator…");
  const args = ["install-nwa", ...apps.map((a) => a.nwa)];
  if (apps.some((a) => a.data)) args.push("--external-data", apps.map((a) => a.data || "").join(","));
  const onLine = (line) => onStep && /\d+%|ETA|\|/.test(line) && onStep("Writing to the calculator…", line);
  try {
    return await run(js, args, onLine);
  } catch (e) {
    // right after a scan or a replug the calculator can be briefly unavailable: try once more
    if (!/no devices found|selection failure/i.test(e.message)) throw e;
    await new Promise((r) => setTimeout(r, 2500));
    return run(js, args, onLine);
  }
}

// size an app takes once installed (linked, padded to 64 KB), via nwlink nwa-bin
async function measure(nwa, data, tmpDir) {
  const js = await ensureNwlink();
  const out = path.join(tmpDir, `measure-${Date.now()}.bin`);
  const args = ["nwa-bin", nwa, out];
  if (data) args.splice(1, 0, "--external-data", data);
  await run(js, args);
  const size = fs.statSync(out).size;
  fs.rmSync(out, { force: true });
  return Math.ceil(size / 0x10000) * 0x10000;
}

async function appName(nwa) {
  const js = await ensureNwlink();
  return (await run(js, ["nwa-name", nwa])).trim();
}

module.exports = { setDataDir, findNwlink, ensureNwlink, install, measure, appName };
