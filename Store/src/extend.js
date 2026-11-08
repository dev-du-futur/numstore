// EXPERIMENTAL "extend memory": preloaded into nwlink (node -r) like tty.js.
// The calculator keeps two copies of its OS (slot A from 0x90000000, slot B from 0x90400000)
// and only runs one; apps normally live between the end of the running OS and 0x903F0000
// (about 2.5 MB). This makes nwlink see the apps area as ending at NWEXTEND_END (default
// 0x90800000, the end of the 8 MB flash), so big apps are written over the spare OS copy.
// It only changes what nwlink reads; nothing in the calculator's own headers is modified.
// Same idea as Nwagyu's Nwagra bookmarklet, written separately for Numstore.
// Only safe while the calculator runs slot A (the caller checks that first).
const path = require("path");
// patch the usb module nwlink itself loads (an npm-installed nwlink has its own copy;
// the one the store downloads uses ours through NODE_PATH)
const { WebUSBDevice } = require(require.resolve("usb", { paths: [path.dirname(process.argv[1] || __dirname), __dirname] }));

const END = Number(process.env.NWEXTEND_END || 0x90800000);
const orig = WebUSBDevice.prototype.controlTransferIn;

WebUSBDevice.prototype.controlTransferIn = async function (setup, length) {
  const r = await orig.call(this, setup, length);
  const d = r && r.data;
  // the userland header: magic 0xFEEDC0DE, then version, storage address/size,
  // external apps flash start (offset 20) and end (offset 24)
  if (d && d.byteLength >= 28 && d.getUint32(0) === 0xfeedc0de) {
    const start = d.getUint32(20, true), end = d.getUint32(24, true);
    if (end > start && end < END && start < END) {
      d.setUint32(24, END, true);
      process.stderr.write(`[extend] apps area 0x${start.toString(16)}-0x${end.toString(16)} -> 0x${END.toString(16)}\n`);
    }
  }
  return r;
};
