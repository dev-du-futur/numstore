// electron-builder afterPack hook: on macOS, sign the app ad hoc ("-"), with no Apple
// certificate. Apple Silicon Macs refuse to run code with no signature at all; with
// this, users only need to allow it once in Privacy & Security.
const { execFileSync } = require("child_process");
const path = require("path");

exports.default = async function (ctx) {
  if (ctx.electronPlatformName !== "darwin") return;
  const app = path.join(ctx.appOutDir, `${ctx.packager.appInfo.productFilename}.app`);
  execFileSync("codesign", ["--force", "--deep", "--sign", "-", app], { stdio: "inherit" });
  execFileSync("codesign", ["--verify", "--deep", "--verbose=2", app], { stdio: "inherit" });
};
