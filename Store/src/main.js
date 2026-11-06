const { app, BrowserWindow, ipcMain, dialog, shell } = require("electron");
const fs = require("fs");
const path = require("path");
const device = require("./device");
const installer = require("./installer");
const nwi = require("./nwi");
const { Catalog } = require("./catalog");

let win, catalog;
let calc = { connected: false };
let busy = false;      // an install is running: don't touch USB
let scanTimer = null;
let lastWrite = 0;     // the calculator restarts after a write and may need replugging

const bundledDir = app.isPackaged
  ? path.join(process.resourcesPath, "catalog")
  : path.join(__dirname, "..", "catalog");

function send(ch, data) { if (win && !win.isDestroyed()) win.webContents.send(ch, data); }

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
      icon: img ? { w: img.width, h: img.height, rgba: img.rgba.toString("base64") } : null,
    };
  });
  return {
    busy,
    calc: calc.connected
      ? { connected: true, model: calc.model, version: calc.version, used: calc.used, total: calc.total, error: calc.error }
      : { connected: false, error: calc.error, restarted: Date.now() - lastWrite < 180000 },
    installed,
    catalog: apps.map((a) => ({
      id: a.id, name: a.name, version: a.version, author: a.author, description: a.description,
      category: a.category, size: a.size, source: a.source, downloaded: a.downloaded !== false,
      icon: a.iconPath ? iconData(a.iconPath) : a.iconUrl || null,
      installed: installed.some((i) => i.name === a.name),
    })),
    onlineError: catalog.remoteError,
  };
}

function push() { send("state", state()); }

async function rescan() {
  if (busy) return;
  try {
    const r = await device.scan();
    calc = r.connected ? r : { connected: false };
  } catch (e) {
    calc = { connected: !!device.findDevice(), apps: [], error: friendly(e) };
    if (calc.connected) calc = { ...calc, model: "?", used: 0, total: 0 };
  }
  push();
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
// The calculator always gets the whole list of apps again (that's how nwlink works),
// so every change = apps that stay (in their order) + the new one.
function sourcesFor(names) {
  const apps = catalog.all();
  return names.map((n) => apps.find((a) => a.name === n) || null);
}

async function apply(targetNames, label) {
  if (busy) return;
  busy = true;
  push();
  const step = (text, detail) => send("progress", { label, text, detail });
  try {
    let srcs = sourcesFor(targetNames);
    srcs = await Promise.all(srcs.map((s) => catalog.fetchApp(s, step)));
    if (!device.findDevice()) throw new Error("Calculator not connected");
    if (srcs.length === 0) {
      step("Removing the app…");
      await device.removeAll();
    } else {
      await installer.install(srcs.map((s) => ({ nwa: s.nwaPath, data: s.dataPath })), step);
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

ipcMain.handle("plan", (_e, { action, id, name }) => {
  // what would happen: list of names after the change, apps that would be lost, space
  const apps = catalog.all();
  const current = (calc.apps || []).map((a) => a.name);
  let target;
  if (action === "install") {
    const a = apps.find((x) => x.id === id);
    target = current.filter((n) => n !== a.name).concat(a.name);
  } else {
    target = current.filter((n) => n !== name);
  }
  const unknown = target.filter((n) => !apps.some((a) => a.name === n));
  const kept = target.filter((n) => !unknown.includes(n));
  const size = kept.reduce((s, n) => s + (apps.find((a) => a.name === n).size || 0), 0);
  return { target: kept, lost: unknown, size, total: calc.total || 0 };
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
  catalog = new Catalog(bundledDir, app.getPath("userData"));
  createWindow();
  device.watch(() => scheduleScan(1200));
  rescan();
  catalog.refreshRemote().then(push);
  setInterval(() => { if (!busy && !calc.connected) rescan(); }, 4000); // safety net for missed USB events
});

app.on("window-all-closed", () => app.quit());
