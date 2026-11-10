const { app, BrowserWindow, ipcMain, dialog, shell } = require("electron");
const fs = require("fs");
const path = require("path");
const device = require("./device");
const installer = require("./installer");
const nwi = require("./nwi");
const { Catalog, variantFor } = require("./catalog");
const updater = require("./updater");
const { Progress, appFor } = require("./files");

let win, catalog, updates, progress;
let calc = { connected: false };
let busy = false;      // an install is running: don't touch USB
let scanTimer = null;
let lastWrite = 0;     // the calculator restarts after a write and may need replugging

const bundledDir = app.isPackaged
  ? path.join(process.resourcesPath, "catalog")
  : path.join(__dirname, "..", "catalog");

function send(ch, data) { if (win && !win.isDestroyed()) win.webContents.send(ch, data); }

// ---- settings (userData/settings.json) ----------------------------------------
let settings = {};
const settingsFile = () => path.join(app.getPath("userData"), "settings.json");
function loadSettings() { try { settings = JSON.parse(fs.readFileSync(settingsFile(), "utf8")); } catch (e) { settings = {}; } }
function saveSettings() { try { fs.writeFileSync(settingsFile(), JSON.stringify(settings, null, 2)); } catch (e) {} }

// "Extend memory" (experimental, see device.js): on when the user turned it on and this
// calculator allows it; then apps may use the spare system copy too
const extendOn = () => !!settings.extendMemory && !!calc.extendable;

// ---- the last apps seen on each calculator (settings.lastApps[serial]) -------------
// NumWorks deletes every app when the calculator resets (reset button, or an app that
// crashes), so Numstore remembers what was there and offers to put it all back.
const calcKey = () => calc.serial || calc.model || "calculator";
function rememberApps() {
  if (!calc.connected || calc.error || !(calc.apps || []).length) return;
  // the size too: the online catalog (NumPlay's games...) may not be loaded yet at the first
  // scan, so the size variant is worked out again when reinstalling
  const apps = catalog.all();
  const list = calc.apps.map((a) => {
    const c = apps.find((x) => x.name === a.name);
    return { name: a.name, size: a.size, variant: c ? variantFor(c, a.size) : null };
  });
  const old = (settings.lastApps || {})[calcKey()];
  if (old && JSON.stringify(old.apps) === JSON.stringify(list)) return;
  settings.lastApps = { ...(settings.lastApps || {}), [calcKey()]: { apps: list, at: Date.now() } };
  saveSettings();
}
function forgetApps() {
  if (!settings.lastApps || !settings.lastApps[calcKey()]) return;
  delete settings.lastApps[calcKey()];
  saveSettings();
}
// what can be put back on an empty calculator: apps from Numstore only
function restorable() {
  if (!calc.connected || calc.error || (calc.apps || []).length) return null;
  const saved = (settings.lastApps || {})[calcKey()];
  if (!saved || !saved.apps.length) return null;
  const apps = catalog.all();
  const target = [], missing = [];
  for (const t of saved.apps) {
    const c = apps.find((x) => x.name === t.name);
    const variant = c && (t.variant || (t.size ? variantFor(c, t.size) : null));
    if (variant) target.push({ name: t.name, variant });
    else missing.push(t.name);
  }
  return {
    target, missing,
    at: saved.at,
  };
}
// apps already in the spare copy (installed extended before) count against the bigger space
const appsTotal = () => (extendOn() || (calc.extendable && calc.extendedInUse) ? calc.extTotal : calc.total);

function iconData(p) {
  try { return "data:image/png;base64," + fs.readFileSync(p).toString("base64"); } catch (e) { return null; }
}

// ---- game progress (files.js): kept on this PC across installs, resets and removals ----
// settings.pendingRestore[calc] = names of apps whose files go back once the calculator
// is seen again after an install (it restarts, and may need plugging back in)
let calcFiles = null;    // the last file list read: { key, used, size, apps: { name: bytes }, other: bytes }
let progressNote = null; // { kind: "restored" | "full" | "error", apps, text } for the window
const appsOf = (names) => [...new Set(names)].filter(Boolean);
async function keepProgress() {
  const st = await device.readFiles();
  const key = st.serial || calcKey();
  // the store's apps, and those on the calculator that it doesn't know (installed by hand)
  const apps = [...catalog.all()];
  for (const a of calc.apps || []) if (!apps.some((x) => x.name === a.name)) apps.push({ name: a.name });
  progress.keep(key, st.records, apps);
  // what takes the calculator's file space: each game's saves, and the rest (scripts...)
  calcFiles = { key, used: st.used, size: st.size, apps: {}, other: 0 };
  for (const r of st.records) {
    const bytes = 3 + Buffer.byteLength(r.name, "latin1") + r.data.length;
    const app = appFor(r.name, apps);
    if (app) calcFiles.apps[app] = (calcFiles.apps[app] || 0) + bytes;
    else calcFiles.other += bytes;
  }
  return { key, st };
}
async function restoreProgress() {
  const key = calcKey();
  const pending = (settings.pendingRestore || {})[key];
  if (!pending) return;
  try {
    const st = await device.readFiles();
    const installed = (calc.apps || []).map((a) => a.name);
    const want = progress.wanted(key, st.records, installed);
    const r = want.length ? await device.addFiles(want) : { added: [], left: [] };
    const app = (n) => want.find((w) => w.name === n)?.app;
    progress.mark(key, r.added, false);
    progress.mark(key, r.left, true);
    delete settings.pendingRestore[key];
    saveSettings();
    if (r.left.length)
      progressNote = { kind: "full", apps: appsOf(r.left.map(app)), text: "The calculator's file storage is full: delete some Python scripts or saves on it, then plug it in again." };
    else if (r.added.length) progressNote = { kind: "restored", apps: appsOf(r.added.map(app)) };
    if (r.left.length) { settings.pendingRestore[key] = true; saveSettings(); }
  } catch (e) {
    progressNote = { kind: "error", text: friendly(e) };
  }
}
function progressState() {
  if (!progress) return null;
  const key = calc.connected ? calcKey() : null;
  const kept = key ? progress.byApp(key) : {};
  const files = calcFiles && calcFiles.key === key ? calcFiles : null;
  return {
    kept, // { app: { files, size, savedAt } }
    files, // the calculator's file space, or null
    pending: !!(key && (settings.pendingRestore || {})[key]) || (!calc.connected && !!Object.keys(settings.pendingRestore || {}).length),
    note: progressNote,
  };
}

// ---- state sent to the window -------------------------------------------------
function state() {
  const apps = catalog.all();
  const installed = (calc.apps || []).map((a) => {
    const match = apps.find((c) => c.name === a.name);
    const img = a.icon ? nwi.decode(a.icon) : null;
    return {
      name: a.name, size: a.size, id: match ? match.id : null,
      variant: match ? variantFor(match, a.size) : null,
      icon: img ? { w: img.width, h: img.height, rgba: img.rgba.toString("base64") } : null,
    };
  });
  return {
    busy,
    calc: calc.connected
      ? { connected: true, model: calc.model, version: calc.version, used: calc.used, total: appsTotal(), error: calc.error,
          extend: { on: extendOn(), wanted: !!settings.extendMemory, available: !!calc.extendable, reason: calc.extendReason || "",
                    normal: calc.total, extended: calc.extTotal || 0, inUse: !!calc.extendedInUse } }
      : { connected: false, error: calc.error, restarted: Date.now() - lastWrite < 180000 },
    installed,
    restore: restorable(),
    progress: progressState(),
    catalog: apps.map((a) => {
      const inst = installed.find((i) => i.name === a.name);
      return {
        id: a.id, name: a.name, version: a.version, author: a.author, description: a.description,
        category: a.category, source: a.source,
        license: a.license || "", homepage: a.homepage || "", credit: a.credit || "",
        icon: a.iconPath ? iconData(a.iconPath) : a.iconUrl || null,
        variants: a.variants.map((v) => ({ id: v.id, label: v.label, description: v.description, size: v.size, ready: v.ready })),
        defaultVariant: a.defaultVariant || a.variants[0].id,
        installed: !!inst,
        installedVariant: inst ? inst.variant : null,
      };
    }),
    onlineError: catalog.remoteError,
  };
}

function push() { send("state", state()); }

// The scan in progress, if any: an install waits for it, so that Numstore and nwlink
// never talk to the calculator at the same time (an install hung like that on 2026-11-06).
let scanning = null;

function rescan() {
  if (busy) return Promise.resolve();
  if (scanning) return scanning;
  scanning = (async () => {
    try {
      const r = await device.scan();
      calc = r.connected ? r : { connected: false };
      rememberApps();
      if (calc.connected && !calc.error) {
        if ((settings.pendingRestore || {})[calcKey()]) await restoreProgress();
        try {
          const { key, st } = await keepProgress();
          if (!(settings.pendingRestore || {})[key]) progress.prune(key, st.records, (calc.apps || []).map((a) => a.name));
        } catch (e) { /* the file list can't be read: progress is simply not kept */ }
      }
    } catch (e) {
      calc = { connected: !!device.findDevice(), apps: [], error: friendly(e) };
      if (calc.connected) calc = { ...calc, model: "?", used: 0, total: 0 };
    }
    push();
  })().finally(() => { scanning = null; });
  return scanning;
}

function scheduleScan(ms = 800) {
  clearTimeout(scanTimer);
  scanTimer = setTimeout(rescan, ms);
}

function friendly(e) {
  const m = String((e && e.message) || e);
  if (/LIBUSB_ERROR_ACCESS|access/i.test(m)) return "Another program is using the calculator (close the NumWorks website or other tools).";
  if (/LIBUSB_ERROR_NOT_SUPPORTED/i.test(m)) return "Windows has no driver for the calculator yet. Install it from numworks.com (Update → Install the driver).";
  if (/no device|not connected|No NumWorks/i.test(m)) return "Calculator not connected.";
  if (/overflow|too large|fit/i.test(m)) return "Not enough space on the calculator.";
  return m;
}

// ---- install / remove ----------------------------------------------------------
const sizeIn = (apps, t) => {
  const a = apps.find((x) => x.name === t.name);
  return a ? (a.variants.find((v) => v.id === t.variant) || a.variants[0]).size || 0 : 0;
};
// Extended memory: the calculator only finds apps that START in the normal space, so the
// biggest app goes last, and only that one may run on into the spare system copy.
function extendOrder(apps, target) {
  if (target.length < 2) return target;
  let big = 0;
  target.forEach((t, i) => { if (sizeIn(apps, t) > sizeIn(apps, target[big])) big = i; });
  return [...target.slice(0, big), ...target.slice(big + 1), target[big]];
}
// null if it fits, or why not: { big, bigSize, others: [{name, size}], othersSize, normal, needFree, tooBig, text }
function extendProblem(apps, target, normal, extended) {
  const sizes = target.map((t) => sizeIn(apps, t));
  const all = sizes.reduce((s, x) => s + x, 0);
  if (all > extended) return null; // plain "doesn't fit", shown by the size line
  const beforeLast = all - (sizes[sizes.length - 1] || 0);
  if (beforeLast < normal) return null;
  const MB = (b) => (b / 1048576).toFixed(2) + " MB";
  const last = target[target.length - 1];
  const others = target.slice(0, -1).map((t, i) => ({ name: t.name, size: sizes[i] })).sort((x, y) => y.size - x.size);
  const tooBig = others.filter((o) => o.size >= normal).map((o) => o.name); // a second big app can never fit
  const needFree = beforeLast - normal + 1;
  const text = tooBig.length
    ? `Only one app bigger than ${MB(normal)} can be on the calculator at a time, and ${[last.name, ...tooBig].join(" and ")} both are.`
    : `${last.name} uses the extended memory, so all the other apps must fit in the normal ${MB(normal)}. They take ${MB(beforeLast)}: remove at least ${MB(needFree)} of them.`;
  return { big: last.name, bigSize: sizes[sizes.length - 1], others, othersSize: beforeLast, normal, needFree, tooBig, text };
}

// When the apps don't fit: the closest way that does, trying every size of every app and
// leaving apps out (never `keep`, the one being installed). Fewest apps removed first, then
// fewest size changes, then the most space kept. null if nothing works (or too many apps to try).
function findFit(apps, target, keep, room) {
  const fits = (list) => {
    const all = list.reduce((s, t) => s + sizeIn(apps, t), 0);
    if (!room.extend) return all <= room.total;
    return all <= room.ext && !extendProblem(apps, extendOrder(apps, list), room.normal, room.ext);
  };
  const choices = target.map((t) => {
    const a = apps.find((x) => x.name === t.name);
    const vs = a ? a.variants.map((v) => v.id) : [t.variant];
    return [t.variant, ...vs.filter((v) => v !== t.variant), ...([].concat(keep || []).includes(t.name) ? [] : [null])];
  });
  if (choices.reduce((n, c) => n * c.length, 1) > 50000) return null;
  let best = null;
  const walk = (i, pick) => {
    if (i === target.length) {
      const list = pick.filter((p) => p.variant !== null);
      if (!fits(list)) return;
      const removed = pick.filter((p) => p.variant === null).length;
      const changed = pick.filter((p, k) => p.variant !== null && p.variant !== target[k].variant).length;
      const size = list.reduce((s, t) => s + sizeIn(apps, t), 0);
      const score = [removed, changed, -size];
      const k = best ? score.findIndex((x, j) => x !== best.score[j]) : 0;
      if (!best || (k >= 0 && score[k] < best.score[k]))
        best = { score, pick: pick.slice() };
      return;
    }
    for (const v of choices[i]) { pick.push({ name: target[i].name, variant: v }); walk(i + 1, pick); pick.pop(); }
  };
  walk(0, []);
  if (!best || (best.score[0] === 0 && best.score[1] === 0)) return null;
  const label = (name, v) => apps.find((x) => x.name === name)?.variants.find((x) => x.id === v)?.label || v;
  let list = best.pick.filter((p) => p.variant !== null);
  if (room.extend) list = extendOrder(apps, list);
  return {
    target: list,
    removed: best.pick.filter((p) => p.variant === null).map((p) => p.name),
    changes: best.pick.filter((p, k) => p.variant !== null && p.variant !== target[k].variant).map((p) => ({ name: p.name, label: label(p.name, p.variant) })),
  };
}

// The calculator always gets the whole list of apps again (that's how nwlink works),
// so every change = apps that stay (in their order, same size variant) + the new one.
// A target is a list of { name, variant }.
async function apply(target, label) {
  if (busy) return;
  busy = true;
  clearTimeout(scanTimer);
  push();
  if (scanning) await scanning.catch(() => {});
  const step = (text, detail) => send("progress", { label, text, detail });
  try {
    const apps = catalog.all();
    if (settings.extendMemory) target = extendOrder(apps, target);
    const files = [];
    for (const t of target) {
      const a = apps.find((x) => x.name === t.name);
      if (!a) throw new Error(`${t.name} is not in the store`);
      files.push(await catalog.fetchVariant(a, t.variant, step));
    }
    if (!device.findDevice()) throw new Error("Calculator not connected");
    let extend = false;
    if (settings.extendMemory && files.length) {
      // check again right before writing: after a system update the calculator may run its
      // second copy, which extended apps would overwrite
      step("Checking the calculator…");
      const fresh = await device.scan();
      if (!fresh.connected) throw new Error("Calculator not connected");
      extend = !!fresh.extendable;
      const need = target.reduce((s, t) => s + sizeIn(apps, t), 0);
      if (!extend && need > fresh.total) throw new Error(fresh.extendReason || "Extended memory isn't available on this calculator right now.");
      const problem = extend && extendProblem(apps, target, fresh.total, fresh.extTotal);
      if (problem) throw new Error(problem.text);
    }
    // games' saves first: the install empties the calculator's file list
    step("Saving your games' progress…");
    try { await keepProgress(); } catch (e) { /* not readable: nothing to keep */ }
    settings.pendingRestore = { ...(settings.pendingRestore || {}), [calcKey()]: true };
    saveSettings();
    progressNote = null;
    if (files.length === 0) {
      step("Removing the app…");
      await device.removeAll();
      forgetApps(); // the user emptied it on purpose: nothing to offer back
    } else {
      await installer.install(files, step, { extend });
    }
    step("Done");
    send("done", { ok: true, label });
  } catch (e) {
    send("done", { ok: false, label, error: friendly(e) });
  } finally {
    busy = false;
    lastWrite = Date.now();
    scheduleScan(3500); // the calculator restarts after a write
  }
}

ipcMain.handle("state", () => state());
ipcMain.handle("rescan", () => rescan());

ipcMain.handle("plan", (_e, { action, id, variant, name, add, remove }) => {
  // what would happen: apps after the change, apps that would be lost, space used
  const apps = catalog.all();
  const current = (calc.apps || []).map((a) => {
    const c = apps.find((x) => x.name === a.name);
    return { name: a.name, variant: c ? variantFor(c, a.size) : null, known: !!c };
  });
  let target;
  if (action === "restore") {
    const r = restorable();
    target = r ? r.target.map((t) => ({ ...t, known: true })) : [];
  } else if (action === "batch") {
    // the basket: every change at once (one write, one restart). add: [{ id, variant }], remove: [names]
    target = current.filter((t) => !(remove || []).includes(t.name));
    for (const x of add || []) {
      const a = apps.find((c) => c.id === x.id);
      if (!a) continue;
      const v = x.variant || a.defaultVariant || a.variants[0].id;
      const at = target.findIndex((t) => t.name === a.name);
      if (at >= 0) target[at] = { name: a.name, variant: v, known: true };
      else target.push({ name: a.name, variant: v, known: true });
    }
  } else if (action === "install") {
    const a = apps.find((x) => x.id === id);
    const v = variant || a.defaultVariant || a.variants[0].id;
    const at = current.findIndex((t) => t.name === a.name);
    target = current.slice();
    if (at >= 0) target[at] = { name: a.name, variant: v, known: true }; // switching size keeps its place
    else target.push({ name: a.name, variant: v, known: true });
  } else {
    target = current.filter((t) => t.name !== name);
  }
  const lost = target.filter((t) => !t.known).map((t) => t.name);
  let kept = target.filter((t) => t.known).map(({ name, variant }) => ({ name, variant }));
  if (extendOn()) kept = extendOrder(apps, kept);
  const size = kept.reduce((s, t) => s + sizeIn(apps, t), 0);
  const problem = extendOn() ? extendProblem(apps, kept, calc.total, calc.extTotal) : null;
  const total = appsTotal() || 0;
  // never offered for removal: the app being installed, or the basket's apps (restore: any may go)
  const keep = action === "install" ? apps.find((x) => x.id === id).name
    : action === "batch" ? (add || []).map((x) => apps.find((c) => c.id === x.id)?.name).filter(Boolean) : null;
  const fix = action !== "remove" && (problem || size > total)
    ? findFit(apps, kept, keep, { extend: extendOn(), normal: calc.total, ext: calc.extTotal, total })
    : null;
  return { target: kept, lost, size, total, extended: extendOn(), normal: calc.total || 0, problem, fix };
});

ipcMain.handle("set-extend", (_e, on) => {
  if (busy) return false;
  settings.extendMemory = !!on;
  saveSettings();
  push();
  return true;
});

ipcMain.handle("apply", (_e, { target, label }) => { apply(target, label); return true; });

ipcMain.handle("add-file", async () => {
  const r = await dialog.showOpenDialog(win, {
    title: "Choose a NumWorks app",
    filters: [{ name: "NumWorks app", extensions: ["nwa"] }],
    properties: ["openFile"],
  });
  if (r.canceled || !r.filePaths[0]) return null;
  const nwa = r.filePaths[0];
  let data = null;
  const ask = await dialog.showMessageBox(win, {
    type: "question",
    buttons: ["No", "Yes, choose the file"],
    defaultId: 0,
    message: "Does this app come with a data file?",
    detail: "Some apps (like TinyChat's model) need an extra file next to the .nwa. Most don't.",
  });
  if (ask.response === 1) {
    const d = await dialog.showOpenDialog(win, { title: "Choose the app's data file", properties: ["openFile"] });
    if (d.canceled) return null;
    data = d.filePaths[0];
  }
  try {
    const name = await installer.appName(nwa);
    const size = await installer.measure(nwa, data, app.getPath("temp"));
    const id = catalog.addToLibrary({
      name, version: "", author: "", size, category: "My apps",
      description: "Added from " + path.basename(nwa),
    }, nwa, data);
    push();
    return { id };
  } catch (e) {
    return { error: friendly(e) };
  }
});

ipcMain.handle("restart-update", () => updates.restart());
ipcMain.handle("version", () => app.getVersion());
ipcMain.handle("forget", (_e, id) => { catalog.forget(id); push(); });
ipcMain.handle("forget-progress", (_e, appName) => { if (calc.connected) progress.forget(calcKey(), appName); push(); });
ipcMain.handle("dismiss-progress-note", () => { progressNote = null; push(); });
ipcMain.handle("open", (_e, url) => { if (/^https:\/\//.test(url)) shell.openExternal(url); });

// ---- window -------------------------------------------------------------------
function createWindow() {
  win = new BrowserWindow({
    width: 1100, height: 760, minWidth: 820, minHeight: 560,
    backgroundColor: "#0f1020",
    title: "Numstore",
    icon: path.join(__dirname, "ui", "logo.png"),
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  win.loadFile(path.join(__dirname, "ui", "index.html"));
  // development aid: NUMSTORE_SHOT=file.png [NUMSTORE_JS=code] saves a screenshot and quits
  if (process.env.NUMSTORE_SHOT) {
    win.webContents.once("did-finish-load", () => setTimeout(async () => {
      if (process.env.NUMSTORE_JS) await win.webContents.executeJavaScript(process.env.NUMSTORE_JS);
      await new Promise((r) => setTimeout(r, 600));
      fs.writeFileSync(process.env.NUMSTORE_SHOT, (await win.webContents.capturePage()).toPNG());
      app.quit();
    }, 5000));
  }
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (e) => e.preventDefault());
}

app.whenReady().then(async () => {
  installer.setDataDir(app.getPath("userData"));
  loadSettings();
  catalog = new Catalog(bundledDir, app.getPath("userData"));
  progress = new Progress(path.join(app.getPath("userData"), "progress"));
  createWindow();
  updates = updater.start({ send, isBusy: () => busy });
  device.watch(() => scheduleScan(1200));
  rescan();
  catalog.refreshRemote().then(push);
  setInterval(() => { if (!busy && !calc.connected) rescan(); }, 4000); // safety net for missed USB events
});

app.on("window-all-closed", () => app.quit());
app.on("activate", () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
