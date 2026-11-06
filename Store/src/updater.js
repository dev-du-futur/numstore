// Self-update from the GitHub releases of dev-du-futur/numstore (electron-updater).
// A newer release is downloaded in the background; the window then offers
// "Restart to update", and otherwise it installs when the app quits.
// While the repo is private (or offline) the check just fails quietly.
// On macOS the app isn't signed, so it can't replace itself: the window offers the
// download instead.
const { app, shell } = require("electron");
const RELEASES = "https://github.com/dev-du-futur/numstore/releases/latest";
const MAC = process.platform === "darwin";
const { autoUpdater } = require("electron-updater");

const CHECK_EVERY = 4 * 3600 * 1000;

function start({ send, isBusy }) {
  if (!app.isPackaged) return { restart() {} }; // only the installed app updates itself
  autoUpdater.autoDownload = !MAC;
  autoUpdater.autoInstallOnAppQuit = !MAC;
  autoUpdater.logger = null;

  autoUpdater.on("update-available", (i) => send("update", { state: MAC ? "available" : "downloading", version: i.version }));
  autoUpdater.on("download-progress", (p) => send("update", { state: "downloading", percent: Math.round(p.percent) }));
  autoUpdater.on("update-downloaded", (i) => send("update", { state: "ready", version: i.version }));
  autoUpdater.on("error", () => send("update", { state: "idle" }));

  const check = () => autoUpdater.checkForUpdates().catch(() => {});
  setTimeout(check, 5000);
  setInterval(check, CHECK_EVERY);

  return {
    restart() {
      if (MAC) { shell.openExternal(RELEASES); return true; }
      if (isBusy()) return false; // never in the middle of writing to the calculator
      autoUpdater.quitAndInstall(false, true);
      return true;
    },
  };
}

module.exports = { start };
