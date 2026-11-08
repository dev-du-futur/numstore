const { app, BrowserWindow, ipcMain, dialog, shell } = require("electron");
const fs = require("fs");
const path = require("path");
const device = require("./device");
const installer = require("./installer");
const nwi = require("./nwi");
const { Catalog, variantFor } = require("./catalog");
const updater = require("./updater");

let win, catalog, updates;
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
// apps already in the spare copy (installed extended before) count against the bigger space
const appsTotal = () => (extendOn() || (calc.extendable && calc.extendedInUse) ? calc.extTotal : calc.total);

function iconData(p) {
  try { return "data:image/png;base64," + fs.readFileSync(p).toString("base64"); } catch (e) { return null; }
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
// null if it fits, or why not
function extendProblem(apps, target, normal, extended) {
  const sizes = target.map((t) => sizeIn(apps, t));
  const all = sizes.reduce((s, x) => s + x, 0);
  if (all > extended) return null; // plain "doesn't fit", shown by the size line
  const beforeLast = all - (sizes[sizes.length - 1] || 0);
  if (beforeLast >= normal)
    return `With extended memory, only the biggest app can go past the normal ${(normal / 1048576).toFixed(2)} MB: the others together must fit in it, and they take ${(beforeLast / 1048576).toFixed(2)} MB.`;
  return null;
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
      if (problem) throw new Error(problem);
    }
    if (files.length === 0) {
      step("Removing the app…");
      await device.removeAll();
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

ipcMain.handle("plan", (_e, { action, id, variant, name }) => {
  // what would happen: apps after the change, apps that would be lost, space used
  const apps = catalog.all();
  const current = (calc.apps || []).map((a) => {
    const c = apps.find((x) => x.name === a.name);
    return { name: a.name, variant: c ? variantFor(c, a.size) : null, known: !!c };
  });
  let target;
  if (action === "install") {
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
  return { target: kept, lost, size, total: appsTotal() || 0, extended: extendOn(), normal: calc.total || 0, problem };
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
  createWindow();
  updates = updater.start({ send, isBusy: () => busy });
  device.watch(() => scheduleScan(1200));
  rescan();
  catalog.refreshRemote().then(push);
  setInterval(() => { if (!busy && !calc.connected) rescan(); }, 4000); // safety net for missed USB events
});

app.on("window-all-closed", () => app.quit());
app.on("activate", () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
