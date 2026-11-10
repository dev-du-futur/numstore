// Reads a NumWorks calculator over USB (DFU): model, firmware version, the external-apps
// flash region and the apps installed in it, and its file list (games' saves, in RAM).
// Writing apps is left to NumWorks' own nwlink tool (see installer.js); Numstore only
// writes files back into the file list (addFiles).
const { usb, getDeviceList } = require("usb");

const VID = 0x0483;
const PIDS = [0xa291, 0xdf11]; // calculator in DFU mode, STM32 bootloader
const DFU_DNLOAD = 1, DFU_UPLOAD = 2, DFU_GETSTATUS = 3, DFU_CLRSTATUS = 4, DFU_ABORT = 6;
const APP_MAGIC = 0xdec0beba;
const APP_SECTOR = 0x10000; // apps are padded to 64 KB, like nwlink does

const MODELS = { 0x0120: "N0120", 0x0110: "N0110", 0x0115: "N0115", 0x0200: "N0200" };
// RAM address of the slot info (N0110/N0115 use 0x20000000, N0120 0x24000000)
const SLOT_INFO_RAM = { N0120: 0x24000000, N0110: 0x20000000, N0115: 0x20000000 };
// "Extend memory" (experimental): the OS has two copies, slot A from 0x90000000 and slot B
// from 0x90400000, and runs one. While it runs slot A, apps may also use slot B, up to the
// end of the 8 MB flash. Only tried on the N0120 (Epsilon 26.4).
const SLOT_B = 0x90400000;
const EXT_END = 0x90800000;
const EXT_MODELS = ["N0120"];

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
    const kernelAddr = slot.readUInt32LE(4);
    const kernel = await this.read(kernelAddr, 24);
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
      runningSlot: kernelAddr < SLOT_B ? "A" : "B",
    };
  }

  // Walk the external apps region: each app starts on a 64 KB boundary with
  // [magic, api level, name offset, icon size, icon offset, entry, end offset, magic]
  // limit: how far apps may go (the official end, or EXT_END when memory can be extended)
  async apps(info, limit = info.appsEnd) {
    const list = [];
    let addr = info.appsStart;
    while (addr + 32 <= limit) {
      const h = await this.read(addr, 32);
      if (h.readUInt32LE(0) !== APP_MAGIC || h.readUInt32LE(28) !== APP_MAGIC) break;
      const nameOff = h.readUInt32LE(8), iconSize = h.readUInt32LE(12), iconOff = h.readUInt32LE(16);
      const end = h.readUInt32LE(24);
      const name = (await this.read(addr + nameOff, 64)).toString("latin1").replace(/\0.*$/s, "");
      const icon = iconSize > 0 && iconSize < 65536 ? await this.read(addr + iconOff, iconSize) : null;
      // the app's code + data ends after `end` (.data is stored right after); find the next
      // app by looking at the following 64 KB boundaries
      let next = addr + Math.ceil((end + 1) / APP_SECTOR) * APP_SECTOR;
      while (next + 4 <= limit) {
        const m = (await this.read(next, 4)).readUInt32LE(0);
        if (m === APP_MAGIC || m === 0xffffffff) break;
        next += APP_SECTOR;
      }
      list.push({ name, address: addr, size: next - addr, icon });
      addr = next;
    }
    return { apps: list, used: addr - info.appsStart, total: info.appsEnd - info.appsStart };
  }

  // can apps go over the spare OS copy (slot B)? -> { ok, reason }
  extension(info) {
    const flash = this.alts.find((a) => /^@Flash/.test(a.name));
    if (!EXT_MODELS.includes(this.model)) return { ok: false, reason: `Only tried on the N0120 so far (this is an ${this.model}).` };
    if (info.runningSlot !== "A")
      return { ok: false, reason: "Your calculator is running its second system copy right now. Update it once on my.numworks.com, then try again." };
    if (!flash || flash.end < EXT_END || info.appsEnd > SLOT_B)
      return { ok: false, reason: "This calculator's memory layout isn't one Numstore knows." };
    return { ok: true, reason: "" };
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
    const ext = c.extension(info);
    // reading further is harmless, and shows apps that already use the extended space
    const apps = await c.apps(info, ext.ok ? EXT_END : info.appsEnd);
    return {
      connected: true,
      model: c.model,
      serial: c.serial,
      version: info.version,
      ...apps,
      appsStart: info.appsStart,
      appsEnd: info.appsEnd,
      runningSlot: info.runningSlot,
      extendable: ext.ok,
      extendReason: ext.reason,
      extTotal: EXT_END - info.appsStart,
      extendedInUse: apps.used > apps.total, // apps already reach into the spare copy
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

// DfuSe write (RAM only here): set the address pointer, then DNLOAD blocks 2, 3, ...
Calculator.prototype.write = async function (addr, data) {
  await this.selectFor(addr);
  await this.abort();
  await this.dfuse(0x21, addr);
  const ts = this.transferSize;
  for (let off = 0, block = 2; off < data.length; off += ts, block++) {
    await ctrl(this.dev, false, DFU_DNLOAD, block, this.ifaceNum, data.subarray(off, off + ts));
    await this.status(); // executes the write
    await this.waitIdle();
  }
  await this.abort();
};

// The calculator's file list (see files.js): its records, used space and size.
async function withCalc(fn) {
  const dev = findDevice();
  if (!dev) throw new Error("Calculator not connected");
  const c = new Calculator(dev);
  try {
    await c.open();
    return await fn(c, await c.info());
  } finally {
    await c.close();
  }
}
async function readStorage(c, info) {
  const files = require("./files");
  const buf = await c.read(info.storageAddress, info.storageSize + 8);
  return { serial: c.serial, ...files.parse(buf, info.storageSize), size: info.storageSize };
}
function readFiles() { return withCalc(readStorage); }

// Adds records at the end of the list (nothing already there moves or changes). Those that
// don't fit are left out: -> { added: [names], left: [names] }
function addFiles(records) {
  const files = require("./files");
  return withCalc(async (c, info) => {
    const st = await readStorage(c, info);
    const have = new Set(st.records.map((r) => r.name));
    const todo = records.filter((r) => !have.has(r.name));
    const added = [], left = [];
    let end = st.end;
    for (const r of todo) { // one by one: as many as fit
      const bytes = files.appended([r], end, info.storageSize);
      if (!bytes) { left.push(r.name); continue; }
      // the new record first, then its first two bytes (its size): the list stays ended
      // by a zero size at every moment
      const at = info.storageAddress + 4 + end;
      await c.write(at + 2, bytes.subarray(2));
      await c.write(at, bytes.subarray(0, 2));
      end += bytes.length - 2;
      added.push(r.name);
    }
    if (added.length) { // check it reads back as written
      const after = await readStorage(c, info);
      const got = new Map(after.records.map((r) => [r.name, r.data]));
      for (const r of todo) if (added.includes(r.name) && !(got.get(r.name) || Buffer.alloc(0)).equals(r.data))
        throw new Error(`${r.name} didn't read back right`);
    }
    return { added, left };
  });
}

// Takes files out of the list: the ones after them move up, as when Epsilon deletes a
// file. Epsilon remembers where the last file it looked up is (just after the list: the
// size it may use, that file's name checksum, a pointer into the list); that's cleared,
// as Epsilon itself does after a change, so it looks files up again.
function removeFiles(names) {
  const files = require("./files");
  return withCalc(async (c, info) => {
    const st = await readStorage(c, info);
    const change = files.Progress.without(st.records, names);
    if (!change) return { removed: [] };
    const a = info.storageAddress, n = info.storageSize;
    const tail = await c.read(a + 4 + n, 256);
    let cache = -1;
    for (let i = 8; i + 12 <= tail.length; i += 4) {
      const ptr = tail.readUInt32LE(i + 8);
      if (tail.readUInt32LE(i) === n && (ptr === 0 || (ptr >= a + 4 && ptr < a + 4 + n))) { cache = i + 4; break; }
    }
    // not found: another firmware, or files put aside (Epsilon does that in exam mode)
    if (cache < 0) throw new Error("Numstore can't safely change the files on this calculator's firmware");
    const clear = () => c.write(a + 4 + n + cache, Buffer.alloc(8));
    await clear();
    await c.write(a + 4 + change.from, change.bytes);
    const oldEnd = st.end + 2, newEnd = change.from + change.bytes.length; // zeros where it used to end
    if (oldEnd > newEnd) await c.write(a + 4 + newEnd, Buffer.alloc(oldEnd - newEnd));
    await clear();
    const after = await readStorage(c, info);
    const left = st.records.filter((r) => !names.includes(r.name));
    if (after.records.length !== left.length || after.records.some((r, i) => r.name !== left[i].name || !r.data.equals(left[i].data)))
      throw new Error("the calculator's files didn't read back right");
    return { removed: st.records.filter((r) => names.includes(r.name)).map((r) => r.name) };
  });
}

function watch(cb) {
  usb.on("attach", cb);
  usb.on("detach", cb);
  return () => { usb.off("attach", cb); usb.off("detach", cb); };
}

module.exports = { scan, watch, removeAll, readFiles, addFiles, removeFiles, findDevice, Calculator, APP_SECTOR, EXT_END };

if (require.main === module) {
  scan().then(
    (r) => { console.log(JSON.stringify(r, (k, v) => (k === "icon" ? (v ? `<${v.length} bytes>` : null) : v), 2)); process.exit(0); },
    (e) => { console.error("error:", e); process.exit(1); }
  );
}
