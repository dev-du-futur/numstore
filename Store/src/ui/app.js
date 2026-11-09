const $ = (s) => document.querySelector(s);
// calm, NumWorks-like colours for the apps in the storage bar
const COLORS = ["#4a5568", "#ffb734", "#7f8ea3", "#c97b2a", "#2f3b4c", "#a7b0bd", "#e0a040", "#5f6b7a"];
let S = null;            // state from the main process
let tab = "store";
const chosen = {};       // app id -> size variant picked on its card
let preview = null;      // { add: name, size } or { remove: name } while hovering a button

const mb = (b) => (b / 1048576).toFixed(b >= 1048576 * 10 ? 0 : 2).replace(/\.?0+$/, "") + " MB";
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

function colorFor(name) {
  const names = [...new Set([...(S?.installed || []).map((a) => a.name), ...(S?.catalog || []).map((a) => a.name)])];
  return COLORS[Math.max(0, names.indexOf(name)) % COLORS.length];
}

function iconEl(src, name) {
  if (src && typeof src === "object") {           // raw pixels from the calculator
    const c = document.createElement("canvas");
    c.width = src.w; c.height = src.h; c.className = "icon";
    const px = Uint8ClampedArray.from(atob(src.rgba), (ch) => ch.charCodeAt(0));
    c.getContext("2d").putImageData(new ImageData(px, src.w, src.h), 0, 0);
    return c;
  }
  if (src) { const i = new Image(); i.src = src; i.className = "icon"; i.alt = ""; return i; }
  const d = document.createElement("div");
  d.className = "icon blank"; d.textContent = (name || "?")[0].toUpperCase();
  return d;
}

// ---- header + storage ---------------------------------------------------------
function renderCalc() {
  const c = S.calc;
  const notice = $("#notice");
  let title = "", sub = "";
  if (!c.connected) {
    $("#calc-title").textContent = c.restarted ? "RESTARTED" : "NO CALCULATOR";
    title = c.restarted ? "Your calculator restarted." : "No calculator connected.";
    sub = c.restarted ? "Unplug it and plug it back in to see the change." : c.error || "Plug it into this computer with a USB cable and turn it on.";
  } else if (c.error) {
    $("#calc-title").textContent = c.model || "CALCULATOR";
    title = "Calculator found, but it can't be read.";
    sub = c.error;
  } else {
    $("#calc-title").textContent = `${c.model} · EPSILON ${c.version}`;
  }
  notice.classList.toggle("hidden", !title);
  $("#notice-title").textContent = title;
  $("#calc-sub").textContent = sub;
  const ok = c.connected && c.total && !c.error;
  $("#tb-free").textContent = ok ? `${mb(c.total - c.used)} FREE` : "";
  $(".gauge").classList.toggle("hidden", !ok);
  $("#tb-batt").style.width = ok ? (100 * c.used / c.total) + "%" : "0";
}

function renderStorage() {
  const bar = $("#bar"), legend = $("#legend"), text = $("#storage-text");
  bar.innerHTML = ""; legend.innerHTML = "";
  const c = S.calc;
  if (!c.connected || !c.total) {
    text.textContent = "Connect your calculator to see its storage";
    $("#storage").classList.add("hidden");
    return;
  }
  $("#storage").classList.remove("hidden");
  if (preview?.add && c.extend?.on) return renderExtendedPreview(c, bar, legend, text);
  let used = 0;
  for (const a of S.installed) {
    const seg = document.createElement("div");
    seg.className = "seg" + (preview?.remove === a.name ? " leaving" : "");
    seg.style.width = (100 * a.size / c.total) + "%";
    seg.style.background = colorFor(a.name);
    seg.title = `${a.name} · ${mb(a.size)}`;
    bar.appendChild(seg);
    used += a.size;
    legend.insertAdjacentHTML("beforeend", `<span><i style="background:${colorFor(a.name)}"></i>${esc(a.name)} · ${mb(a.size)}</span>`);
  }
  let after = used;
  if (preview?.add) {
    const seg = document.createElement("div");
    const old = preview.remove ? S.installed.find((a) => a.name === preview.remove)?.size || 0 : 0;
    seg.className = "seg preview" + (used - old + preview.size > c.total ? " over" : "");
    seg.style.width = Math.min(100, 100 * preview.size / c.total) + "%";
    seg.style.background = colorFor(preview.add);
    bar.appendChild(seg);
    after += preview.size;
  }
  if (preview?.remove) after -= S.installed.find((a) => a.name === preview.remove)?.size || 0;
  renderExtend(c, bar);
  const free = c.total - used;
  text.innerHTML = preview
    ? (after > c.total
        ? `<b style="color:var(--red)">Doesn't fit</b>: needs ${mb(after - c.total)} more`
        : `After this: <b>${mb(c.total - after)}</b> free of ${mb(c.total)}`)
    : `<b>${mb(free)}</b> free of ${mb(c.total)}`;
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#e4e5e8"></i>Free · ${mb(free)}</span>`);
}

// With extended memory, show the apps as they would be laid out (biggest last, like
// main.js extendOrder): an app that would start after the line is red, since the
// calculator wouldn't find it.
function renderExtendedPreview(c, bar, legend, text) {
  const normal = c.extend.normal;
  let list = S.installed.map((a) => ({ name: a.name, size: a.size }));
  const at = list.findIndex((a) => a.name === preview.add);
  if (at >= 0) list[at] = { name: preview.add, size: preview.size, isNew: true };
  else list.push({ name: preview.add, size: preview.size, isNew: true });
  if (list.length > 1) {
    let big = 0;
    list.forEach((a, i) => { if (a.size > list[big].size) big = i; });
    list = [...list.slice(0, big), ...list.slice(big + 1), list[big]];
  }
  let start = 0;
  const lost = [];
  for (const a of list) {
    const seg = document.createElement("div");
    const late = start >= normal;
    if (late) lost.push(a.name);
    seg.className = "seg" + (a.isNew ? " preview" : "") + (late ? " lost" : "");
    seg.style.width = Math.max(0, Math.min(100 - 100 * start / c.total, 100 * a.size / c.total)) + "%";
    seg.style.background = colorFor(a.name);
    seg.title = `${a.name} · ${mb(a.size)}${late ? " · would start after the line: the calculator wouldn't find it" : ""}`;
    bar.appendChild(seg);
    start += a.size;
    legend.insertAdjacentHTML("beforeend", `<span><i style="background:${late ? "var(--red)" : colorFor(a.name)}"></i>${esc(a.name)} · ${mb(a.size)}</span>`);
  }
  renderExtend(c, bar);
  text.innerHTML = start > c.total
    ? `<b style="color:var(--red)">Doesn't fit</b>: needs ${mb(start - c.total)} more`
    : lost.length
      ? `<b style="color:var(--red)">Doesn't fit</b>: ${esc(lost.join(", "))} would start after the line`
      : `After this: <b>${mb(c.total - start)}</b> free of ${mb(c.total)}`;
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#e4e5e8"></i>Free · ${mb(Math.max(0, c.total - start))}</span>`);
}

// ---- extend memory (experimental: apps may use the calculator's spare system copy) ----
function renderExtend(c, bar) {
  const btn = $("#extend-btn"), x = c.extend;
  $("#storage .extend-note")?.remove();
  if (!x || (!x.available && !x.wanted && !x.inUse)) { btn.classList.add("hidden"); return; }
  btn.classList.remove("hidden");
  btn.disabled = S.busy;
  if (x.on) {
    btn.className = "key small on";
    btn.textContent = "Extended memory: on";
    btn.title = "Apps may use the calculator's spare system copy. Click to turn it off.";
    btn.onclick = askExtendOff;
    const l = document.createElement("div");
    l.className = "limit";
    l.style.left = (100 * x.normal / c.total) + "%";
    l.title = `Normal space ends here (${mb(x.normal)}); after it, the spare system copy`;
    bar.appendChild(l);
  } else if (x.available) {
    btn.className = "key small";
    btn.textContent = "Extend memory";
    btn.title = `Raise the space for apps from ${mb(x.normal)} to ${mb(x.extended)} (experimental)`;
    btn.onclick = askExtendOn;
    if (x.inUse) $("#storage").insertAdjacentHTML("beforeend",
      `<div class="extend-note">Your apps already use the extended memory. Turn it on to keep installing apps there; with it off, the next install has to fit in ${mb(x.normal)}.</div>`);
  } else { // turned on, but this calculator can't right now
    btn.className = "key small";
    btn.textContent = "Extended memory: unavailable";
    btn.title = x.reason;
    btn.onclick = askExtendOff;
    $("#storage").insertAdjacentHTML("beforeend", `<div class="extend-note">${esc(x.reason)}</div>`);
  }
}

async function askExtendOn() {
  const x = S.calc.extend;
  const html = `
    <p>Your calculator keeps a <b>spare copy of its system</b> that it doesn't use. Numstore can install apps over it,
    raising the space for apps from <b>${mb(x.normal)}</b> to <b>${mb(x.extended)}</b>: enough for big games like FNaF World.
    One limit: only <b>one</b> app can use the extra space, the biggest one. The others still share the normal ${mb(x.normal)}.</p>
    <div class="warnbox"><b>This is unofficial, and at your own risk.</b> NumWorks doesn't support it.
    It works like Nwagyu's “Nwagra” and was tested on an N0120 with Epsilon 26.4.</div>
    <p><b>What can go wrong</b></p>
    <ul class="risks">
      <li><b>No backup system any more.</b> If the calculator's system ever gets damaged, it can't fall back on the spare copy.
        You would reinstall it with NumWorks' recovery on numworks.com, which erases your apps and files.</li>
      <li><b>Updates remove the apps stored there.</b> Updating the calculator on numworks.com writes the new system over the spare copy.
        Reinstall those apps from Numstore afterwards: it checks first that it's still safe.</li>
      <li><b>Possible exam-mode message.</b> On some system versions, apps outside the normal space make the calculator say exam mode is on.
        If that happens, turn this off and reinstall your apps in the normal space.</li>
    </ul>
    <label class="agree"><input type="checkbox" id="m-agree"> I understand the risks and want to extend the memory</label>
    <p class="hint" style="margin-top:10px">Nothing changes on the calculator until your next install. You can turn it off any time.</p>`;
  if (await modal("Extend memory", html, "Extend memory", false, true)) window.store.setExtend(true);
}

async function askExtendOff() {
  const x = S.calc.extend;
  const tooBig = S.calc.used > x.normal;
  const html = `
    <p>Numstore will install apps in the normal space again (${mb(x.normal)}).</p>
    ${tooBig ? `<div class="warnbox">Your apps take <b>${mb(S.calc.used)}</b> right now, more than the normal ${mb(x.normal)}.
      They stay where they are, but the next install has to fit in ${mb(x.normal)}: remove the big ones first.</div>` : ""}
    <p class="hint">The spare system copy comes back the next time you update the calculator on numworks.com.</p>`;
  if (await modal("Normal memory", html, "Turn off")) window.store.setExtend(false);
}

// ---- views --------------------------------------------------------------------
function matches(a) {
  const q = $("#q").value.trim().toLowerCase();
  return !q || [a.name, a.description, a.author, a.category].some((f) => String(f || "").toLowerCase().includes(q));
}

function renderStore() {
  const view = $("#view-store");
  view.innerHTML = "";
  const list = S.catalog.filter(matches);
  $("#n-store").textContent = S.catalog.length || "";
  if (!list.length) {
    view.innerHTML = `<div class="empty"><b>No apps found</b>Try another search.</div>`;
    return;
  }
  for (const a of list) {
    const card = document.createElement("div");
    card.className = "card";
    card.appendChild(iconEl(a.icon, a.name));
    card.insertAdjacentHTML("beforeend", `<div class="name">${esc(a.name)}</div>
      <div class="meta">${esc([a.category, a.author, a.version && "v" + a.version].filter(Boolean).join(" · "))}</div>
      <p class="desc">${esc(a.description)}</p>`);
    if (a.credit) card.insertAdjacentHTML("beforeend", `<div class="meta">${esc(a.credit)}</div>`);
    if (a.homepage) { // apps from other projects: where their source and licence are
      const l = document.createElement("button");
      l.className = "link";
      l.textContent = `Source${a.license ? " · " + a.license : ""}`;
      l.onclick = () => window.store.open(a.homepage);
      card.appendChild(l);
    }
    const v = pickVariant(a);
    const variant = a.variants.find((x) => x.id === v);
    if (a.variants.length > 1) {
      const seg = document.createElement("div");
      seg.className = "sizes";
      for (const x of a.variants) {
        const k = document.createElement("button");
        k.className = "size" + (x.id === v ? " on" : "");
        k.innerHTML = `${esc(x.label)}<span>${mb(x.size)}</span>`;
        k.onclick = () => { chosen[a.id] = x.id; render(); };
        seg.appendChild(k);
      }
      card.appendChild(seg);
      card.insertAdjacentHTML("beforeend", `<div class="size-desc">${esc(variant.description)}</div>`);
    }
    const foot = document.createElement("div");
    foot.className = "card-foot";
    const instLabel = a.installed ? a.variants.find((x) => x.id === a.installedVariant)?.label : "";
    foot.insertAdjacentHTML("beforeend", a.installed
      ? `<span class="badge on">Installed${instLabel && a.variants.length > 1 ? " · " + esc(instLabel) : ""}</span>`
      : `<span class="badge">${a.variants.length > 1 ? "" : mb(variant.size)}${variant.ready === false ? " · download" : ""}</span>`);
    if (a.source === "library" && !a.installed) {
      const f = document.createElement("button");
      f.className = "link"; f.textContent = "Forget";
      f.onclick = () => window.store.forget(a.id);
      foot.appendChild(f);
    }
    const b = document.createElement("button");
    if (a.installed && a.installedVariant === v) {
      b.className = "key"; b.textContent = "Remove";
      b.onclick = () => askRemove(a.name);
      hoverPreview(b, { remove: a.name });
    } else if (a.installed) {
      b.className = "key orange"; b.textContent = `Switch to ${variant.label}`;
      b.onclick = () => askInstall(a, v);
      hoverPreview(b, { add: a.name, size: variant.size, remove: a.name });
    } else {
      b.className = "key orange"; b.textContent = "Install";
      b.onclick = () => askInstall(a, v);
      hoverPreview(b, { add: a.name, size: variant.size });
    }
    b.disabled = !S.calc.connected || !!S.calc.error || S.busy;
    if (b.disabled) b.title = S.calc.connected ? "" : "Connect your calculator first";
    foot.appendChild(b);
    card.appendChild(foot);
    view.appendChild(card);
  }
}

// size shown on a card: what the user picked, else what's installed, else the default
// if it fits in the free space, else the smallest
function pickVariant(a) {
  if (chosen[a.id] && a.variants.some((x) => x.id === chosen[a.id])) return chosen[a.id];
  if (a.installedVariant) return a.installedVariant;
  const free = S.calc.connected && S.calc.total ? S.calc.total - S.calc.used : Infinity;
  const def = a.variants.find((x) => x.id === a.defaultVariant) || a.variants[0];
  if ((def.size || 0) <= free) return def.id;
  return [...a.variants].sort((x, y) => (x.size || 0) - (y.size || 0))[0].id;
}

function renderDevice() {
  const v = $("#view-device");
  v.innerHTML = "";
  $("#n-device").textContent = S.calc.connected && S.installed.length ? S.installed.length : "";
  if (!S.calc.connected) {
    v.innerHTML = `<div class="empty"><b>No calculator connected</b>Plug your NumWorks into this computer with a USB cable, turn it on, and it will show up here.</div>`;
    return;
  }
  if (!S.installed.length) {
    v.innerHTML = `<div class="empty"><b>No apps installed yet</b>Pick one in the Store tab.</div>`;
    return;
  }
  for (const a of S.installed) {
    const row = document.createElement("div");
    row.className = "row";
    row.appendChild(iconEl(a.icon || S.catalog.find((c) => c.name === a.name)?.icon, a.name));
    const known = S.catalog.some((c) => c.name === a.name);
    row.insertAdjacentHTML("beforeend", `<div class="grow"><h3>${esc(a.name)}</h3>
      <div class="meta">${[variantLabel(a), mb(a.size)].filter(Boolean).join(" · ")}${known ? "" : " · not from Numstore"}</div></div>`);
    const b = document.createElement("button");
    b.className = "key"; b.textContent = "Remove";
    b.disabled = S.busy;
    b.onclick = () => askRemove(a.name);
    hoverPreview(b, { remove: a.name });
    row.appendChild(b);
    v.appendChild(row);
  }
}

function variantLabel(inst) {
  const c = S.catalog.find((x) => x.name === inst.name);
  return c && c.variants.length > 1 ? c.variants.find((x) => x.id === inst.variant)?.label : "";
}

function hoverPreview(el, p) {
  el.addEventListener("mouseenter", () => { preview = p; renderStorage(); });
  el.addEventListener("mouseleave", () => { preview = null; renderStorage(); });
}

// the calculator is empty but had apps before (it was reset, maybe by a crash): offer them back
const names = (list) => list.length < 2 ? list.join("") : list.slice(0, -1).join(", ") + " and " + list[list.length - 1];
function renderRestore() {
  const box = $("#restore");
  const r = S.restore;
  box.classList.toggle("hidden", !r || S.busy);
  if (!r) { box.innerHTML = ""; return; }
  const n = r.target.length;
  const when = new Date(r.at).toLocaleDateString(undefined, { day: "numeric", month: "long" });
  box.innerHTML = `<div class="grow"><b class="title">Your apps are gone</b>
    <p>On ${esc(when)} this calculator had ${n ? names(r.target.map((t) => `<b>${esc(t.name)}</b>`)) : "apps"}${r.missing.length && n ? ", plus " + names(r.missing.map(esc)) : ""}.
    NumWorks deletes every app when the calculator resets: the reset button on the back, or an app that crashes.</p>
    ${r.missing.length ? `<p class="hint">${names(r.missing.map((m) => `<b>${esc(m)}</b>`))} ${r.missing.length > 1 ? "weren't" : "wasn't"} installed from Numstore: add ${r.missing.length > 1 ? "their" : "its"} .nwa file again to get ${r.missing.length > 1 ? "them" : "it"} back.</p>` : ""}</div>`;
  if (!n) return;
  const b = document.createElement("button");
  b.className = "key orange";
  b.textContent = n > 1 ? `Reinstall these ${n} apps` : `Reinstall ${r.target[0].name}`;
  b.onclick = askRestore;
  box.appendChild(b);
}

async function askRestore() {
  const plan = await window.store.plan({ action: "restore" });
  if (!plan.target.length) return;
  const list = names(plan.target.map((t) => `<b>${esc(t.name)}</b>`));
  if (fits(plan)) {
    if (await modal("Reinstall your apps", planHtml(plan, `${list} will be installed again, in the same sizes as before.`), "Reinstall", false, true))
      run(plan.target, "Reinstalling your apps");
  } else if (plan.fix) {
    if (await modal("Reinstall your apps", planHtml(plan, `${list} don't all fit any more (Extend memory may have been turned off since).`), `${fixWords(plan.fix)} and reinstall`, !!plan.fix.removed.length, true))
      run(plan.fix.target, "Reinstalling your apps");
  } else {
    await modal("Reinstall your apps", planHtml(plan, `${list} don't fit on the calculator together any more.`), "Reinstall", false, true);
  }
}

function render() {
  if (!S) return;
  renderCalc(); renderStorage(); renderRestore(); renderStore(); renderDevice();
  $("#view-store").classList.toggle("hidden", tab !== "store");
  $("#view-device").classList.toggle("hidden", tab !== "device");
  const note = $("#online-note");
  note.classList.toggle("hidden", !S.onlineError || tab !== "store");
  note.textContent = S.onlineError ? "Showing the apps that come with Numstore (the online catalog isn't reachable right now)." : "";
}

// ---- dialogs ------------------------------------------------------------------
function modal(title, html, okText, danger, wide) {
  return new Promise((res) => {
    $("#m-title").textContent = title;
    $("#m-body").innerHTML = html;
    $("#modal .sheet").classList.toggle("wide", !!wide);
    const ok = $("#m-ok");
    ok.textContent = okText;
    ok.className = danger ? "key dark" : "key orange";
    $("#modal").classList.remove("hidden");
    const close = (v) => { $("#modal").classList.add("hidden"); ok.onclick = null; $("#m-cancel").onclick = null; res(v); };
    ok.onclick = () => close(true);
    $("#m-cancel").onclick = () => close(false);
    ok.disabled = !!$("#m-body .space.bad, #m-body .problem") && !$("#m-body .fix");
    // a dialog with an "I understand" box: OK only once it's ticked
    const agree = $("#m-agree");
    if (agree) { ok.disabled = true; agree.onchange = () => { ok.disabled = !agree.checked; }; }
  });
}

// why the apps can't be laid out with extended memory, with the numbers
function problemHtml(p, fixed) {
  const n = mb(p.normal);
  if (p.tooBig.length) {
    const names = [p.big, ...p.tooBig];
    return `<div class="warnbox problem"><b>Only one big app at a time.</b>
      <p>With Extend memory, one app can be bigger than the normal ${n}, but only one. ${names.map((x) => `<b>${esc(x)}</b>`).join(" and ")} both are, so they can't be on the calculator together.</p>
      ${fixed ? "" : `<p style="margin:0"><b>Remove one of them to install the other.</b></p>`}</div>`;
  }
  return `<div class="warnbox problem"><b>The other apps don't fit in the normal ${n}.</b>
    <p>With Extend memory, the calculator only finds apps that <i>start</i> in its normal ${n}. One big app can go past it, here <b>${esc(p.big)}</b> (${mb(p.bigSize)}), but all the other apps must fit inside the ${n}:</p>
    <div class="plist">${p.others.map((o) => `<div><span>${esc(o.name)}</span><span>${mb(o.size)}</span></div>`).join("")}
      <div class="bad"><span>Together</span><span>${mb(p.othersSize)} of ${n}</span></div></div>
    ${fixed ? "" : `<p style="margin:0"><b>Remove at least ${mb(p.needFree)} of these apps</b> (in On my calculator), then try again.</p>`}</div>`;
}

// the closest way it fits (main.js findFit), offered by the OK button
const fits = (plan) => plan.size <= plan.total && !plan.problem;
const fixWords = (fix) => [
  ...fix.removed.map((n) => `remove ${n}`),
  ...fix.changes.map((c) => `switch ${c.name} to ${c.label}`),
].join(", ").replace(/^./, (x) => x.toUpperCase());
function fixHtml(fix) {
  const parts = [
    ...fix.removed.map((n) => `<li><b>${esc(n)}</b> is removed from your calculator (you can install it again later)</li>`),
    ...fix.changes.map((c) => `<li><b>${esc(c.name)}</b> switches to its <b>${esc(c.label)}</b> size</li>`),
  ];
  return `<div class="fix"><b>It fits if:</b><ul>${parts.join("")}</ul></div>`;
}

function planHtml(plan, lead) {
  let h = `<p>${lead}</p>`;
  if (plan.lost.length)
    h += `<div class="warnbox"><b>${plan.lost.map(esc).join(", ")}</b> ${plan.lost.length > 1 ? "were" : "was"} not installed with Numstore, so ${plan.lost.length > 1 ? "they" : "it"} will be removed. Add the .nwa file first if you want to keep ${plan.lost.length > 1 ? "them" : "it"}.</div>`;
  const ok = fits(plan);
  if (plan.problem) h += problemHtml(plan.problem, !!plan.fix);
  if (!ok && plan.fix) h += fixHtml(plan.fix);
  if (!plan.problem && !(plan.fix && !ok)) h += `<div class="space ${ok ? "" : "bad"}"><span>Apps after this</span><span>${mb(plan.size)} of ${mb(plan.total)}${plan.extended ? " (extended)" : ""}</span></div>`;
  if (!ok && !plan.extended && S.calc.extend?.available && plan.size <= S.calc.extend.extended)
    h += `<p class="hint" style="margin-top:8px">It would fit with <b>Extend memory</b> (button in the Storage panel, experimental).</p>`;
  if (ok && plan.extended && plan.size > plan.normal)
    h += `<p class="hint" style="margin-top:8px">Uses the extended memory: part of it goes over the calculator's spare system copy.</p>`;
  if (ok || plan.fix) h += `<p class="hint" style="margin-top:12px">The calculator restarts after the change. This takes up to a minute for big apps.</p>`;
  return h;
}

async function askInstall(a, variant) {
  const plan = await window.store.plan({ action: "install", id: a.id, variant });
  const label = a.variants.find((x) => x.id === variant)?.label;
  const multi = a.variants.length > 1;
  if (!fits(plan)) {
    const what = a.installed ? `${a.name} (${label})` : a.name;
    const lead = plan.fix ? `<b>${esc(what)} doesn't fit</b> with your apps as they are.` : `<b>${esc(what)} doesn't fit</b> on your calculator.`;
    if (await modal(`${a.name} doesn't fit`, planHtml(plan, lead), plan.fix ? `${fixWords(plan.fix)} and install` : "Install", !!plan.fix?.removed.length, true) && plan.fix)
      run(plan.fix.target, `Installing ${a.name}`);
    return;
  }
  if (a.installed) {
    if (await modal(`${a.name}: ${label}`, planHtml(plan, `${esc(a.name)} will be replaced by its ${esc(label)} version.`), `Switch to ${label}`))
      run(plan.target, `Switching ${a.name} to ${label}`);
  } else if (await modal(`Install ${a.name}`, planHtml(plan, `${esc(a.name)}${multi ? ` (${esc(label)})` : ""} will be added to your calculator.`), "Install")) {
    run(plan.target, `Installing ${a.name}`);
  }
}

async function askRemove(name) {
  const plan = await window.store.plan({ action: "remove", name });
  if (await modal(`Remove ${name}`, planHtml(plan, `${esc(name)} will be removed from your calculator. You can install it again any time.`), "Remove", true))
    run(plan.target, `Removing ${name}`);
}

function run(target, label) {
  $("#b-title").textContent = label;
  $("#b-text").textContent = "Preparing…";
  setBar(null);
  $("#busy").classList.remove("hidden");
  window.store.apply({ target, label });
}

let toastTimer;
function toast(msg, kind) {
  const t = $("#toast");
  t.textContent = msg; t.className = "toast " + (kind || "");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add("hidden"), kind === "bad" ? 9000 : 4000);
}

// ---- wiring -------------------------------------------------------------------
document.querySelectorAll(".tab").forEach((t) => t.addEventListener("click", () => {
  tab = t.dataset.tab;
  document.querySelectorAll(".tab").forEach((x) => x.classList.toggle("active", x === t));
  render();
}));
$("#q").addEventListener("input", render);
$("#add-file").addEventListener("click", async () => {
  const r = await window.store.addFile();
  if (r?.error) toast(r.error, "bad");
  else if (r) { toast("Added to the store. Click Install to put it on your calculator.", "ok"); tab = "store"; render(); }
});
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !$("#modal").classList.contains("hidden")) $("#m-cancel").click(); });

window.store.on("state", (s) => { S = s; render(); });
window.store.on("update", (u) => {
  const box = $("#update");
  if (u.state === "available") { // macOS: download it from GitHub
    $("#update-title").textContent = `Numstore ${u.version} is out.`;
    $("#update-text").textContent = "Download it and replace the app in Applications.";
    $("#update-btn").textContent = "Download";
    box.classList.remove("hidden");
    return;
  }
  if (u.state !== "ready") { box.classList.add("hidden"); return; }
  $("#update-title").textContent = `Numstore ${u.version} is ready.`;
  $("#update-text").textContent = "Restart to use it, or it installs next time you close Numstore.";
  $("#update-btn").textContent = "Restart now";
  box.classList.remove("hidden");
});
$("#update-btn").addEventListener("click", async () => {
  if (!(await window.store.restartToUpdate())) toast("Wait until the calculator is done, then restart.", "bad");
});
window.store.version().then((v) => { $("#version").textContent = `Numstore ${v}`; });
// a real bar while writing (nwlink reports a percentage), a moving one before that
function setBar(percent) {
  const bar = $("#busy .progress");
  bar.classList.toggle("known", percent != null);
  bar.firstElementChild.style.width = percent != null ? `${percent}%` : "";
}
window.store.on("progress", (p) => {
  const d = p.detail;
  if (d && typeof d === "object") {
    const left = d.eta == null || d.percent >= 100 ? "" :
      d.eta >= 90 ? ` · about ${Math.round(d.eta / 60)} min left` : ` · about ${Math.max(1, d.eta)} s left`;
    $("#b-text").textContent = `${p.text} ${d.percent}%${left}`;
    setBar(d.percent);
  } else {
    $("#b-text").textContent = d ? `${p.text} ${d}` : p.text;
    setBar(null);
  }
});
window.store.on("done", (d) => {
  $("#busy").classList.add("hidden");
  if (d.ok) toast(`${d.label.replace(/^Reinstalling/, "Reinstalled").replace(/^Installing/, "Installed").replace(/^Removing/, "Removed").replace(/^Switching/, "Switched")} . The calculator restarts: plug it back in to see the change.`, "ok");
  else toast(`${d.label} failed: ${d.error}`, "bad");
});
window.store.state().then((s) => { S = s; render(); });
