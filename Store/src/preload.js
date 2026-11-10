const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("store", {
  state: () => ipcRenderer.invoke("state"),
  rescan: () => ipcRenderer.invoke("rescan"),
  plan: (p) => ipcRenderer.invoke("plan", p),
  apply: (p) => ipcRenderer.invoke("apply", p),
  addFile: () => ipcRenderer.invoke("add-file"),
  forget: (id) => ipcRenderer.invoke("forget", id),
  open: (url) => ipcRenderer.invoke("open", url),
  restartToUpdate: () => ipcRenderer.invoke("restart-update"),
  version: () => ipcRenderer.invoke("version"),
  setExtend: (on) => ipcRenderer.invoke("set-extend", on),
  forgetProgress: (app) => ipcRenderer.invoke("forget-progress", app),
  dismissProgressNote: () => ipcRenderer.invoke("dismiss-progress-note"),
  on: (ch, fn) => ipcRenderer.on(ch, (_e, d) => fn(d)),
});
