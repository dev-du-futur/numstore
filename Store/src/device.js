// Reads a NumWorks calculator over USB (DFU, read-only): model, firmware version,
// the external-apps flash region and the apps installed in it.
// Writing is left to NumWorks' own nwlink tool (see installer.js).
const { usb, getDeviceList } = require("usb");

const VID = 0x0483;
const PIDS = [0xa291, 0xdf11]; // calculator in DFU mode, STM32 bootloader
const DFU_DNLOAD = 1, DFU_UPLOAD = 2, DFU_GETSTATUS = 3, DFU_CLRSTATUS = 4, DFU_ABORT = 6;
const APP_MAGIC = 0xdec0beba;
const APP_SECTOR = 0x10000; // apps are padded to 64 KB, like nwlink does

const MODELS = { 0x0120: "N0120", 0x0110: "N0110", 0x0115: "N0115", 0x0200: "N0200" };
// RAM address of the slot info (N0110/N0115 use 0x20000000, N0120 0x24000000)
const SLOT_INFO_RAM = { N0120: 0x24000000, N0110: 0x20000000, N0115: 0x20000000 };

function findDevice() {
  return getDeviceList().find(
    (d) => d.deviceDescriptor.idVendor === VID && PIDS.includes(d.deviceDescriptor.idProduct)
  );
}

function ctrl(dev, dirIn, req, value, iface, dataOrLen) {
  const bm = (dirIn ? 0x80 : 0x00) | 0x21; // class, interface
  return new Promise((res, rej) =>
    dev.controlTransfer(bm, req, value, iface, dataOrLen, (e, d) => (e ? rej(e) : res(d)))
  );
}

function getString(dev, idx) {
  return new Promise((res) => {
    if (!idx) return res("");
    dev.getStringDescriptor(idx, (e, s) => res(e ? "" : s));
  });
}

class Calculator {
  constructor(dev) {
    this.dev = dev;
  }

  async open() {
    const d = this.dev;
    d.open();
    this.product = await getString(d, d.deviceDescriptor.iProduct);
    this.serial = await getString(d, d.deviceDescriptor.iSerialNumber);
    this.model = MODELS[d.deviceDescriptor.bcdDevice] || "unknown";
    const iface = d.interfaces[0];
    try { iface.claim(); } catch (e) { /* Windows: WinUSB claims implicitly */ }
    this.ifaceNum = iface.interfaceNumber;
    // every alt setting names a memory: "@Flash/0x90000000/..." ; read their address ranges
    this.alts = [];
    const cfg = d.configDescriptor;
    for (const alt of cfg.interfaces[0]) {
      const name = await getString(d, alt.iInterface);
      const l = parseLayout(name);
      // the flash alt setting reads the whole external flash (headers sit below the range it names)
      if (/^@Flash/.test(name) && l.start >= 0x90000000) l.start = 0x90000000;
      this.alts.push({ alt: alt.bAlternateSetting, name, ...l });
    }
    // DFU functional descriptor (in the interface's extra bytes): transfer size
    const extra = cfg.interfaces[0][0].extra;
    this.transferSize = extra && extra.length >= 7 && extra[1] === 0x21 ? extra.readUInt16LE(5) : 2048;
    this.curAlt = -1;
    return this;
  }

  // release fully before closing: Windows gives the calculator to one program at a time,
  // and nwlink (another process) can't see it while we still hold it
  async close() {
    await new Promise((res) => {
      try { this.dev.interfaces[0].release(true, () => res()); } catch (e) { res(); }
    });
    try { this.dev.close(); } catch (e) {}
  }

  async selectFor(addr) {
    const a = this.alts.find((x) => x.start <= addr && addr < x.end);
    if (!a) throw new Error("no memory at 0x" + addr.toString(16));
    if (a.alt !== this.curAlt) {
      await new Promise((res, rej) =>
        this.dev.interfaces[0].setAltSetting(a.alt, (e) => (e ? rej(e) : res()))
      );
      this.curAlt = a.alt;
    }
  }

  async status() {
    const s = await ctrl(this.dev, true, DFU_GETSTATUS, 0, this.ifaceNum, 6);
    return { status: s[0], wait: s[1] | (s[2] << 8) | (s[3] << 16), state: s[4] };
  }

  async waitIdle() {
    for (let i = 0; i < 200; i++) {
      const s = await this.status();
      if (s.state === 10) { // dfuERROR
        await ctrl(this.dev, false, DFU_CLRSTATUS, 0, this.ifaceNum, Buffer.alloc(0));
        continue;
      }
      if (s.state === 2 || s.state === 5 || s.state === 9) return s; // idle / dnload-idle / upload-idle
      await new Promise((r) => setTimeout(r, Math.max(s.wait, 2)));
    }
    throw new Error("calculator did not answer");
  }

  async abort() {
    await ctrl(this.dev, false, DFU_ABORT, 0, this.ifaceNum, Buffer.alloc(0));
    await this.waitIdle();
  }

  // DfuSe read: set the address pointer, then UPLOAD blocks 2, 3, ...
  async read(addr, len) {
    await this.selectFor(addr);
    await this.abort();
    const cmd = Buffer.alloc(5);
    cmd[0] = 0x21;
    cmd.writeUInt32LE(addr >>> 0, 1);
    await ctrl(this.dev, false, DFU_DNLOAD, 0, this.ifaceNum, cmd);
    await this.status(); // executes the command
    await this.waitIdle();
    await this.abort();
    const ts = this.transferSize;
    const out = Buffer.alloc(len);
    let got = 0;
    for (let block = 2; got < len; block++) {
      const n = Math.min(ts, len - got);
      const d = await ctrl(this.dev, true, DFU_UPLOAD, block, this.ifaceNum, ts);
      d.copy(out, got, 0, Math.min(n, d.length));
      got += n;
      if (d.length < ts) break;
    }
    await this.abort();
    return out;
  }

  // Firmware info: where the external apps live
  async info() {
    const ram = SLOT_INFO_RAM[this.model];
    if (ram === undefined) throw new Error("unsupported model " + this.model);
    let slot;
    for (let tries = 0; tries < 3; tries++) { // the first read after connecting can come back empty
      slot = await this.read(ram, 16);
      if (slot.toString("hex", 0, 4) === "badbeeef") break;
    }
    if (slot.toString("hex", 0, 4) !== "badbeeef") throw new Error("slot info not found: " + slot.toString("hex"));
    const kernel = await this.read(slot.readUInt32LE(4), 24);
    const uAddr = slot.readUInt32LE(8);
    const u = await this.read(uAddr, 48);
    if (u.toString("hex", 0, 4) !== "feedc0de") throw new Error("userland header not found");
    const str = (b) => b.toString("latin1").replace(/\0.*$/s, "");
    return {
      version: str(kernel.subarray(4, 12)),
      userlandVersion: str(u.subarray(4, 12)),
      storageAddress: u.readUInt32LE(12),
      storageSize: u.readUInt32LE(16),
      appsStart: u.readUInt32LE(20),
      appsEnd: u.readUInt32LE(24),
    };
  }

  // Walk the external apps region: each app starts on a 64 KB boundary with
  // [magic, api level, name offset, icon size, icon offset, entry, end offset, magic]
  async apps(info) {
    const list = [];
    let addr = info.appsStart;
    while (addr + 32 <= info.appsEnd) {
      const h = await this.read(addr, 32);
      if (h.readUInt32LE(0) !== APP_MAGIC || h.readUInt32LE(28) !== APP_MAGIC) break;
      const nameOff = h.readUInt32LE(8), iconSize = h.readUInt32LE(12), iconOff = h.readUInt32LE(16);
      const end = h.readUInt32LE(24);
      const name = (await this.read(addr + nameOff, 64)).toString("latin1").replace(/\0.*$/s, "");
      const icon = iconSize > 0 && iconSize < 65536 ? await this.read(addr + iconOff, iconSize) : null;
      // the app's code + data ends after `end` (.data is stored right after); find the next
      // app by looking at the following 64 KB boundaries
      let next = addr + Math.ceil((end + 1) / APP_SECTOR) * APP_SECTOR;
      while (next + 4 <= info.appsEnd) {
        const m = (await this.read(next, 4)).readUInt32LE(0);
        if (m === APP_MAGIC || m === 0xffffffff) break;
        next += APP_SECTOR;
      }
      list.push({ name, address: addr, size: next - addr, icon });
      addr = next;
    }
    return { apps: list, used: addr - info.appsStart, total: info.appsEnd - info.appsStart };
  }
}

// "@Flash/0x90000000/08*004Kg,..." -> start/end
function parseLayout(s) {
  const m = /^@[^/]*\/0x([0-9a-fA-F]+)\/(.*)$/.exec(s || "");
  if (!m) return { start: 0, end: 0 };
  const start = parseInt(m[1], 16);
  let size = 0;
  for (const seg of m[2].split(",")) {
    const g = /^(\d+)\*(\d+)([ KMB ]?)/.exec(seg.trim());
    if (!g) continue;
    const mult = { K: 1024, M: 1024 * 1024 }[g[3]] || 1;
    size += parseInt(g[1]) * parseInt(g[2]) * mult;
  }
  return { start, end: start + size };
}

// Connect, read everything the store needs, disconnect.
async function scan() {
  const dev = findDevice();
  if (!dev) return { connected: false };
  const c = new Calculator(dev);
  try {
    await c.open();
    const info = await c.info();
    const apps = await c.apps(info);
    return {
      connected: true,
      model: c.model,
      serial: c.serial,
      version: info.version,
      ...apps,
      appsStart: info.appsStart,
      appsEnd: info.appsEnd,
    };
  } finally {
    await c.close();
  }
}

// Removing the last app: nwlink always installs at least one, so erase the first
// 64 KB sector of the apps region (no app header there = no apps), then let the
// calculator leave DFU mode so it reloads its app list.
Calculator.prototype.dfuse = async function (code, addr) {
  const cmd = Buffer.alloc(5);
  cmd[0] = code;
  cmd.writeUInt32LE(addr >>> 0, 1);
  await ctrl(this.dev, false, DFU_DNLOAD, 0, this.ifaceNum, cmd);
  await this.status();
  await this.waitIdle();
};

async function removeAll() {
  const dev = findDevice();
  if (!dev) throw new Error("Calculator not connected");
  const c = new Calculator(dev);
  try {
    await c.open();
    const info = await c.info();
    await c.selectFor(info.appsStart);
    await c.abort();
    await c.dfuse(0x41, info.appsStart); // erase sector
    // leave: point at the apps region, then an empty download starts manifestation
    await c.dfuse(0x21, info.appsStart);
    await ctrl(c.dev, false, DFU_DNLOAD, 0, c.ifaceNum, Buffer.alloc(0));
    try { await c.status(); } catch (e) { /* the calculator resets here */ }
  } finally {
    await c.close();
  }
}

function watch(cb) {
  usb.on("attach", cb);
  usb.on("detach", cb);
  return () => { usb.off("attach", cb); usb.off("detach", cb); };
}

module.exports = { scan, watch, removeAll, findDevice, Calculator, APP_SECTOR };

if (require.main === module) {
  scan().then(
    (r) => { console.log(JSON.stringify(r, (k, v) => (k === "icon" ? (v ? `<${v.length} bytes>` : null) : v), 2)); process.exit(0); },
    (e) => { console.error("error:", e); process.exit(1); }
  );
}
