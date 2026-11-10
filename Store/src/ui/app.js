const $ = (s) => document.querySelector(s);
// calm, NumWorks-like colours for the apps in the storage bar
const COLORS = ["#4a5568", "#ffb734", "#7f8ea3", "#c97b2a", "#2f3b4c", "#a7b0bd", "#e0a040", "#5f6b7a"];
let S = null;            // state from the main process
let tab = "store";
const chosen = {};       // app id -> size variant picked on its card
let preview = null;      // { add: name, size } or { remove: name } while hovering a button
// name -> { action: "add", id, variant, size, label } or { action: "remove" }: applied together
const basket = new Map();

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

// The apps as they will be after the basket (and the change under the mouse), laid out like
// main.js does: with extended memory the biggest app goes last, and an app that would start
// after the line is red (the calculator wouldn't find it); without it, what overflows is red.
function projected() {
  let list = S.installed.map((a) => ({ name: a.name, size: a.size }));
  const changes = [...basket.entries()];
  if (preview?.add) changes.push([preview.add, { action: "add", size: preview.size }]);
  else if (preview?.remove) changes.push([preview.remove, { action: "remove" }]);
  const removed = [];
  for (const [name, ch] of changes) {
    if (ch.action === "remove") { if (list.some((a) => a.name === name)) removed.push(name); list = list.filter((a) => a.name !== name); continue; }
    const i = list.findIndex((a) => a.name === name), e = { name, size: ch.size, isNew: true };
    if (i >= 0) list[i] = e; else list.push(e);
  }
  if (S.calc.extend?.on && list.length > 1) {
    let big = 0;
    list.forEach((x, i) => { if (x.size > list[big].size) big = i; });
    list = [...list.slice(0, big), ...list.slice(big + 1), list[big]];
  }
  return { list, removed, changed: changes.length > 0 };
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
  const { list, removed, changed } = projected();
  const ext = !!c.extend?.on, normal = c.extend?.normal || c.total;
  let start = 0;
  const lost = [];
  for (const a of list) {
    const late = ext ? start >= normal : start + a.size > c.total;
    if (late) lost.push(a.name);
    const seg = document.createElement("div");
    seg.className = "seg" + (a.isNew ? " preview" : "") + (late ? (ext ? " lost" : " over") : "");
    seg.style.width = Math.max(0, Math.min(100 - 100 * start / c.total, 100 * a.size / c.total)) + "%";
    seg.style.background = colorFor(a.name);
    seg.title = `${a.name} · ${mb(a.size)}${late ? (ext ? " · would start after the line: the calculator wouldn't find it" : " · doesn't fit") : ""}`;
    bar.appendChild(seg);
    start += a.size;
    legend.insertAdjacentHTML("beforeend", `<span${a.isNew ? ' class="new"' : ""}><i style="background:${late ? "var(--red)" : colorFor(a.name)}"></i>${a.isNew ? "+ " : ""}${esc(a.name)} · ${mb(a.size)}</span>`);
  }
  for (const n of removed) legend.insertAdjacentHTML("beforeend", `<span class="gone"><i></i>− ${esc(n)}</span>`);
  renderExtend(c, bar);
  const free = Math.max(0, c.total - start);
  text.innerHTML = !changed ? `<b>${mb(free)}</b> free of ${mb(c.total)}`
    : start > c.total ? `<b style="color:var(--red)">Doesn't fit</b>: needs ${mb(start - c.total)} more`
    : lost.length ? `<b style="color:var(--red)">Doesn't fit</b>: ${esc(lost.join(", "))} would start after the line`
    : `After this: <b>${mb(free)}</b> free of ${mb(c.total)}`;
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#e4e5e8"></i>Free · ${mb(free)}</span>`);
  renderFiles();
}

// the calculator's file space (RAM): each game's saves, then scripts and the rest
function renderFiles() {
  const f = S.progress?.files, row = $("#files-row"), bar = $("#fbar"), legend = $("#flegend");
  row.classList.toggle("hidden", !f); legend.classList.toggle("hidden", !f);
  bar.innerHTML = ""; legend.innerHTML = "";
  if (!f) return;
  const on = new Set((S.installed || []).map((i) => i.name));
  const parts = [...Object.entries(f.apps).sort((x, y) => y[1] - x[1]).map(([n, b]) => [on.has(n) ? `${n} save` : `${n} save (app removed)`, b, colorFor(n),
      on.has(n) ? "" : `${n} isn't on the calculator, but its save is still in the file list: removing an app doesn't erase the files it wrote. ${n} uses it again if you reinstall it.`]),
    ...(f.other ? [["Python scripts and other files", f.other, "#a7b0bd"]] : [])];
  for (const [label, bytes, color, why] of parts) {
    const seg = document.createElement("div");
    seg.className = "seg";
    seg.style.width = Math.max(0.6, 100 * bytes / f.size) + "%";
    seg.style.background = color;
    seg.title = why || `${label} · ${kb(bytes)}`;
    bar.appendChild(seg);
    legend.insertAdjacentHTML("beforeend", `<span${why ? ` class="stale" title="${esc(why)}"` : ""}><i style="background:${color}"></i>${esc(label)} · ${kb(bytes)}</span>`);
  }
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#e4e5e8"></i>Free · ${kb(f.size - f.used)}</span>`);
  $("#ftext").innerHTML = `<b>${kb(f.size - f.used)}</b> free of ${kb(f.size)}`;
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
// ---- the store: cards, a list or icons; filters and order (remembered) -----------------
const look = Object.assign({ view: "cards", show: "all", cat: "", author: "", sort: "store" },
  (() => { try { return JSON.parse(localStorage.getItem("numstore.look")) || {}; } catch (e) { return {}; } })());
const saveLook = () => { try { localStorage.setItem("numstore.look", JSON.stringify(look)); } catch (e) {} };

function matches(a) {
  const q = $("#q").value.trim().toLowerCase();
  if (q && ![a.name, a.description, a.author, a.category].some((f) => String(f || "").toLowerCase().includes(q))) return false;
  if (look.show === "in" && !a.installed) return false;
  if (look.show === "out" && a.installed) return false;
  if (look.cat && a.category !== look.cat) return false;
  if (look.author && a.author !== look.author) return false;
  return true;
}

// a select's options from the catalog, with how many apps each has
function fillPick(sel, all, key, value) {
  const n = {};
  for (const a of S.catalog) if (a[key]) n[a[key]] = (n[a[key]] || 0) + 1;
  const opts = Object.keys(n).sort((x, y) => x.localeCompare(y));
  const html = `<option value="">${all}</option>` + opts.map((o) => `<option value="${esc(o)}">${esc(o)} (${n[o]})</option>`).join("");
  if (sel.dataset.html !== html) { sel.innerHTML = html; sel.dataset.html = html; }
  sel.value = opts.includes(value) ? value : "";
}

function renderTools() {
  document.querySelectorAll(".views button").forEach((b) => {
    b.classList.toggle("on", b.dataset.view === look.view);
    b.setAttribute("aria-checked", b.dataset.view === look.view);
  });
  fillPick($("#f-cat"), "All categories", "category", look.cat);
  fillPick($("#f-author"), "All authors", "author", look.author);
  $("#f-show").value = look.show; $("#f-sort").value = look.sort;
  const filtered = look.show !== "all" || look.cat || look.author;
  $("#f-reset").classList.toggle("hidden", !filtered);
  for (const id of ["#f-show", "#f-cat", "#f-author"]) $(id).classList.toggle("set", !!$(id).value && $(id).value !== "all");
}

// the button of an app: install, remove or switch size, or take it out of the basket
function action(a, v) {
  const variant = a.variants.find((x) => x.id === v);
  const inB = basket.get(a.name);
  const off = !S.calc.connected || !!S.calc.error || S.busy;
  const offWhy = off && !S.calc.connected ? "Connect your calculator first" : "";
  if (a.installed && a.installedVariant === v) {
    const marked = inB?.action === "remove";
    return { kind: "remove", marked, off, label: marked ? "✓ Removing" : "Remove",
      title: offWhy || (marked ? "Click to keep it" : `Remove ${a.name}`),
      click: () => toggleRemove(a.name), hover: marked ? null : { remove: a.name } };
  }
  const marked = inB?.action === "add" && inB.variant === v;
  return { kind: a.installed ? "switch" : "add", marked, off,
    label: marked ? "✓ In the basket" : a.installed ? `Switch to ${variant.label}` : "Install",
    title: offWhy || (marked ? "Click to take it out" : a.installed ? `Switch ${a.name} to ${variant.label}` : `Install ${a.name} · ${mb(variant.size)}`),
    click: () => toggleAdd(a, v),
    hover: marked ? null : a.installed ? { add: a.name, size: variant.size, remove: a.name } : { add: a.name, size: variant.size } };
}
const ROUND = {
  add: `<svg viewBox="0 0 24 24"><path d="M12 4v11M7 10l5 5 5-5M5 20h14"/></svg>`,
  remove: `<svg viewBox="0 0 24 24"><path d="M5 12h14"/></svg>`,
  switch: `<svg viewBox="0 0 24 24"><path d="M4 8h13l-3-3M20 16H7l3 3"/></svg>`,
  marked: `<svg viewBox="0 0 24 24"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg>`,
};
function actionButton(a, v, round) {
  const x = action(a, v);
  const b = document.createElement("button");
  if (round) {
    b.className = `round ${x.marked ? "marked" : x.kind}`;
    b.innerHTML = ROUND[x.marked ? "marked" : x.kind];
    b.setAttribute("aria-label", x.title);
  } else {
    b.className = x.marked ? "key dark" : x.kind === "remove" ? "key" : "key orange";
    b.textContent = x.label;
  }
  b.title = x.title;
  b.disabled = x.off;
  b.onclick = (e) => { e.stopPropagation(); x.click(); };
  if (x.hover) hoverPreview(b, x.hover);
  return b;
}
function sizePicker(a, v) {
  const seg = document.createElement("div");
  seg.className = "sizes";
  for (const x of a.variants) {
    const k = document.createElement("button");
    k.className = "size" + (x.id === v ? " on" : "");
    k.innerHTML = `${esc(x.label)}<span>${mb(x.size)}</span>`;
    k.onclick = () => { chosen[a.id] = x.id; render(); };
    seg.appendChild(k);
  }
  return seg;
}
const keptNote = (a) => kept(a.name) && !a.installed
  ? `<div class="kept" title="Kept on this PC: it goes back on the calculator when you install ${esc(a.name)}">Progress saved · ${esc(day(kept(a.name).savedAt))}</div>` : "";

function renderStore() {
  renderTools();
  const view = $("#view-store");
  view.innerHTML = "";
  view.className = { cards: "grid", list: "slist", icons: "tiles" }[look.view] || "grid";
  const sizeOf = (a) => a.variants.find((x) => x.id === pickVariant(a))?.size || 0;
  const list = S.catalog.filter(matches);
  if (look.sort === "name") list.sort((x, y) => x.name.localeCompare(y.name));
  if (look.sort === "light") list.sort((x, y) => sizeOf(x) - sizeOf(y));
  if (look.sort === "heavy") list.sort((x, y) => sizeOf(y) - sizeOf(x));
  $("#n-store").textContent = S.catalog.length || "";
  $("#found").textContent = list.length === S.catalog.length ? "" : `${list.length} of ${S.catalog.length}`;
  if (!list.length) {
    view.innerHTML = `<div class="empty"><b>No apps found</b>Try another search, or reset the filters.</div>`;
    return;
  }
  for (const a of list) view.appendChild((look.view === "list" ? listRow : look.view === "icons" ? tile : card)(a));
}

// cards: everything, with the full description and the sizes
function card(a) {
  const el = document.createElement("div");
  el.className = "card";
  el.appendChild(iconEl(a.icon, a.name));
  el.insertAdjacentHTML("beforeend", `<div class="name">${esc(a.name)}</div>
    <div class="meta">${esc([a.category, a.author, a.version && "v" + a.version].filter(Boolean).join(" · "))}</div>
    <p class="desc">${esc(a.description)}</p>`);
  if (a.credit) el.insertAdjacentHTML("beforeend", `<div class="meta">${esc(a.credit)}</div>`);
  el.insertAdjacentHTML("beforeend", keptNote(a));
  if (a.homepage) { // apps from other projects: where their source and licence are
    const l = document.createElement("button");
    l.className = "link";
    l.textContent = `Source${a.license ? " · " + a.license : ""}`;
    l.onclick = () => window.store.open(a.homepage);
    el.appendChild(l);
  }
  const v = pickVariant(a);
  const variant = a.variants.find((x) => x.id === v);
  if (a.variants.length > 1) {
    el.appendChild(sizePicker(a, v));
    el.insertAdjacentHTML("beforeend", `<div class="size-desc">${esc(variant.description)}</div>`);
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
  foot.appendChild(actionButton(a, v));
  el.appendChild(foot);
  return el;
}

// list: one line each, icon, name and the description's first sentence, size, button
const short = (s) => { const t = String(s || ""); const m = t.match(/^.+?[.!?](\s|$)/); return (m ? m[0] : t).trim(); };
function listRow(a) {
  const v = pickVariant(a);
  const variant = a.variants.find((x) => x.id === v);
  const el = document.createElement("div");
  el.className = "srow-app" + (a.installed ? " installed" : "");
  el.appendChild(iconEl(a.icon, a.name));
  el.insertAdjacentHTML("beforeend", `<div class="grow">
      <div class="line"><h3>${esc(a.name)}</h3><span class="meta">${esc([a.category, a.author].filter(Boolean).join(" · "))}</span></div>
      <div class="sdesc" title="${esc(a.description)}">${esc(short(a.description))}</div>${keptNote(a)}</div>`);
  if (a.variants.length > 1) {
    const sel = document.createElement("select");
    sel.className = "pick small";
    sel.setAttribute("aria-label", `${a.name} size`);
    sel.innerHTML = a.variants.map((x) => `<option value="${esc(x.id)}">${esc(x.label)} · ${mb(x.size)}</option>`).join("");
    sel.value = v;
    sel.onchange = () => { chosen[a.id] = sel.value; render(); };
    el.appendChild(sel);
  } else el.insertAdjacentHTML("beforeend", `<span class="badge">${mb(variant.size)}</span>`);
  el.insertAdjacentHTML("beforeend", `<span class="state">${a.installed ? "Installed" : ""}</span>`);
  el.appendChild(actionButton(a, v));
  return el;
}

// icons: like the calculator's home screen, with a round button on the icon
function tile(a) {
  const v = pickVariant(a);
  const variant = a.variants.find((x) => x.id === v);
  const el = document.createElement("div");
  el.className = "tile" + (a.installed ? " installed" : "") + (basket.has(a.name) ? " queued" : "");
  el.title = `${a.name} · ${mb(variant.size)}${a.variants.length > 1 ? ` (${variant.label})` : ""}\n${a.description || ""}`;
  const pic = document.createElement("div");
  pic.className = "pic";
  pic.appendChild(iconEl(a.icon, a.name));
  if (a.installed) pic.insertAdjacentHTML("beforeend", `<span class="on-calc" title="On your calculator"></span>`);
  pic.appendChild(actionButton(a, v, true));
  el.appendChild(pic);
  el.insertAdjacentHTML("beforeend", `<div class="tname">${esc(a.name)}</div><div class="tsize">${mb(variant.size)}</div>`);
  return el;
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

// sizes of files: bytes or KB
const kb = (b) => b < 1024 ? `${b} B` : `${(b / 1024).toFixed(b < 10240 ? 1 : 0).replace(/.0$/, "")} KB`;
const ICON_CALC = `<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="2.5" width="12" height="19" rx="2"/><path d="M9 6.5h6M9 11h.01M12 11h.01M15 11h.01M9 14.5h.01M12 14.5h.01M15 14.5h.01M9 18h6"/></svg>`;
const ICON_PC = `<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="12" rx="1.5"/><path d="M8 20h8M12 16v4"/></svg>`;

// a game's save: how much of the calculator's file space it takes, and where it is
function saveCell(name) {
  const k = kept(name), files = S.progress?.files;
  const onCalc = files?.apps?.[name] || 0;
  if (!k && !onCalc) return `<div class="save none">No save</div>`;
  const bytes = onCalc || k.size;
  const size = files?.size || 0;
  const pct = size ? Math.max(2, Math.min(100, 100 * bytes / size)) : 0;
  // an app that isn't installed: each place its save is can be emptied (the × on the pill)
  const away = !S.installed.some((a) => a.name === name);
  const where = (on, icon, short, long, place) => on && away && !S.busy
    ? `<button class="where on del" data-app="${esc(name)}" data-place="${place}" title="Saved ${long}. Click to delete it from there">${icon}${short}<b aria-hidden="true">×</b></button>`
    : `<span class="where ${on ? "on" : ""}" title="${on ? "Saved " + long : "Not " + long}">${icon}${short}</span>`;
  return `<div class="save">
    <div class="save-top"><span class="save-size">Save · ${kb(bytes)}</span>
      ${where(!!onCalc, ICON_CALC, "Calculator", "on the calculator", "calc")}${where(!!k, ICON_PC, "PC", "on this PC", "pc")}</div>
    ${size ? `<div class="minibar" title="${kb(bytes)} of the calculator's ${kb(size)} for files"><span style="width:${pct}%;background:${colorFor(name)}"></span></div>` : ""}
    ${k ? `<div class="save-when">Copied ${esc(day(k.savedAt))}</div>` : ""}
  </div>`;
}

function deviceRow(v, name, icon, meta, cell, button) {
  const row = document.createElement("div");
  row.className = "row";
  row.appendChild(iconEl(icon, name));
  row.insertAdjacentHTML("beforeend", `<div class="grow"><h3>${esc(name)}</h3><div class="meta">${meta}</div></div>${cell}`);
  row.appendChild(button);
  v.appendChild(row);
}

function renderDevice() {
  const v = $("#view-device");
  v.innerHTML = "";
  $("#n-device").textContent = S.calc.connected && S.installed.length ? S.installed.length : "";
  if (!S.calc.connected) {
    v.innerHTML = `<div class="empty"><b>No calculator connected</b>Plug your NumWorks into this computer with a USB cable, turn it on, and it will show up here.</div>`;
    return;
  }
  // apps that aren't installed but have a save, on this PC or still on the calculator
  const away = [...new Set([...Object.keys(S.progress?.kept || {}), ...Object.keys(S.progress?.files?.apps || {})])]
    .filter((n) => !S.installed.some((a) => a.name === n)).sort();
  v.insertAdjacentHTML("beforeend", `<div class="list-head"><span>On the calculator</span><span>${S.installed.length} app${S.installed.length === 1 ? "" : "s"}</span></div>`);
  if (!S.installed.length) v.insertAdjacentHTML("beforeend", `<div class="empty"><b>No apps installed yet</b>Pick one in the Store tab.</div>`);
  for (const a of S.installed) {
    const known = S.catalog.some((c) => c.name === a.name);
    const b = document.createElement("button");
    const marked = basket.get(a.name)?.action === "remove";
    b.className = marked ? "key dark" : "key"; b.textContent = marked ? "✓ Removing" : "Remove";
    b.disabled = S.busy;
    b.onclick = () => toggleRemove(a.name);
    if (!marked) hoverPreview(b, { remove: a.name });
    deviceRow(v, a.name, a.icon || S.catalog.find((c) => c.name === a.name)?.icon,
      esc([variantLabel(a), mb(a.size), !known && "not from Numstore"].filter(Boolean).join(" · ")), saveCell(a.name), b);
  }
  if (!away.length) return;
  v.insertAdjacentHTML("beforeend", `<div class="list-head"><span>Saves of removed apps</span><span>used again when you reinstall</span></div>`);
  for (const name of away) {
    const b = document.createElement("button");
    b.className = "key"; b.textContent = "Delete save";
    b.title = "Delete it from the calculator and from this PC";
    b.disabled = S.busy;
    b.onclick = () => askDeleteSave(name, "both");
    deviceRow(v, name, S.catalog.find((c) => c.name === name)?.icon, "Not installed", saveCell(name), b);
  }
  v.querySelectorAll(".where.del").forEach((x) => x.onclick = () => askDeleteSave(x.dataset.app, x.dataset.place));
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

// ---- game progress kept on this PC (main.js, files.js) ------------------------------
const kept = (name) => S.progress?.kept?.[name] || null;
const day = (t) => new Date(t).toLocaleDateString(undefined, { day: "numeric", month: "long" });
function renderProgressNote() {
  const box = $("#progress-note");
  const p = S.progress || {};
  const n = p.note;
  let html = "";
  if (n?.kind === "restored")
    html = `<div class="grow"><b class="title">Your progress is back</b><p>${names(n.apps.map((a) => `<b>${esc(a)}</b>`))}: right where you left off.</p></div>`;
  else if (n?.kind === "full")
    html = `<div class="grow"><b class="title">Some progress couldn't go back yet</b><p>${n.apps.length ? names(n.apps.map((a) => `<b>${esc(a)}</b>`)) + ": " : ""}${esc(n.text)} It stays safe on this PC.</p></div>`;
  else if (n?.kind === "error")
    html = `<div class="grow"><b class="title">Couldn't put your progress back</b><p>${esc(n.text)} It stays safe on this PC; Numstore tries again next time you plug the calculator in.</p></div>`;
  else if (p.pending && !S.calc.connected && !S.busy)
    html = `<div class="grow"><b class="title">Plug your calculator back in</b><p>Numstore puts your games' progress back then. Don't open your games before, or they start a new save.</p></div>`;
  box.classList.toggle("hidden", !html);
  box.classList.toggle("good", n?.kind === "restored");
  box.innerHTML = html;
  if (n) {
    const x = document.createElement("button");
    x.className = "key small"; x.textContent = "OK";
    x.onclick = () => window.store.dismissProgressNote();
    box.appendChild(x);
  }
}

// delete a removed app's save: from the calculator, from this PC, or both
async function askDeleteSave(name, place) {
  const onCalc = S.progress?.files?.apps?.[name] || 0, k = kept(name);
  const calc = (place === "calc" || place === "both") && onCalc, pc = (place === "pc" || place === "both") && !!k;
  if (!calc && !pc) return;
  const gone = (calc || !onCalc) && (pc || !k); // nowhere left
  const what = calc && pc ? "from the calculator and from this PC" : calc ? "from the calculator" : "from this PC";
  const rest = gone ? `<b>${esc(name)} will start from the beginning</b> next time you install it.`
    : calc ? `It frees ${kb(onCalc)} for files. The copy on this PC stays, and goes back on the calculator when you install ${esc(name)} again.`
    : `It stays on the calculator, and ${esc(name)} uses it again if you reinstall it.`;
  if (!await modal(`Delete ${name}'s save`, `<p>${esc(name)}'s save will be deleted ${what}.</p><p class="hint">${rest}</p>`, "Delete", gone)) return;
  if (pc) await window.store.forgetProgress(name);
  if (calc) {
    const r = await window.store.removeSave(name);
    if (!r.ok) return toast(`Couldn't delete it from the calculator: ${r.error}`, "bad");
  }
  toast(`${name}'s save deleted ${what}`, "ok");
}

async function askForgetProgress(name) {
  const k = kept(name);
  if (await modal(`Delete ${name}'s progress`, `<p>The progress Numstore keeps for <b>${esc(name)}</b> (saved on ${esc(day(k.savedAt))}) will be deleted from this PC. Next time you install it, it starts from the beginning.</p>`, "Delete progress", true))
    window.store.forgetProgress(name);
}

function render() {
  if (!S) return;
  renderCalc(); renderStorage(); renderRestore(); renderProgressNote(); renderStore(); renderDevice(); renderBasket();
  $("#view-store").classList.toggle("hidden", tab !== "store");
  $("#store-tools").classList.toggle("hidden", tab !== "store");
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
  const keep = `<p class="hint">Your progress in ${esc(name)} stays on this PC, and goes back on the calculator when you install it again.</p>`;
  if (await modal(`Remove ${name}`, planHtml(plan, `${esc(name)} will be removed from your calculator. You can install it again any time.`) + keep, "Remove", true))
    run(plan.target, `Removing ${name}`);
}

// ---- the basket ---------------------------------------------------------------------
function toggleAdd(a, v) {
  const cur = basket.get(a.name);
  const x = a.variants.find((y) => y.id === v);
  if (cur?.action === "add" && cur.variant === v) basket.delete(a.name);
  else if (a.installed && a.installedVariant === v) basket.delete(a.name); // back to what's installed
  else basket.set(a.name, { action: "add", id: a.id, variant: v, size: x.size, label: a.variants.length > 1 ? x.label : "", switching: a.installed });
  preview = null;
  render();
}
function toggleRemove(name) {
  if (basket.get(name)?.action === "remove") basket.delete(name);
  else basket.set(name, { action: "remove" });
  preview = null;
  render();
}
const basketWords = (name, ch) => ch.action === "remove" ? `Remove ${name}`
  : ch.switching ? `Switch ${name} to ${ch.label}` : `Install ${name}${ch.label ? ` (${ch.label})` : ""}`;

function renderBasket() {
  const box = $("#basket");
  box.classList.toggle("hidden", !basket.size || !S.calc.connected);
  if (!basket.size) { box.innerHTML = ""; return; }
  const chips = [...basket].map(([name, ch]) => `<span class="chip ${ch.action}" data-name="${esc(name)}">${ch.action === "remove" ? "−" : ch.switching ? "↔" : "+"} ${esc(name)}${ch.label ? ` <em>${esc(ch.label)}</em>` : ""}<button title="Take it out of the basket" aria-label="Take ${esc(name)} out">×</button></span>`).join("");
  box.innerHTML = `<div class="basket-main"><b>${basket.size} change${basket.size > 1 ? "s" : ""}</b><span class="hint">applied together: the calculator restarts once</span>
    <div class="chips">${chips}</div></div>
    <button class="key" id="basket-clear">Clear</button>
    <button class="key orange" id="basket-apply">Apply changes</button>`;
  box.querySelectorAll(".chip button").forEach((x) => x.onclick = () => { basket.delete(x.parentElement.dataset.name); render(); });
  $("#basket-clear").onclick = () => { basket.clear(); render(); };
  $("#basket-apply").onclick = applyBasket;
  $("#basket-apply").disabled = S.busy || !!S.calc.error;
}

async function applyBasket() {
  const add = [...basket].filter(([, ch]) => ch.action === "add").map(([, ch]) => ({ id: ch.id, variant: ch.variant }));
  const remove = [...basket].filter(([, ch]) => ch.action === "remove").map(([name]) => name);
  const plan = await window.store.plan({ action: "batch", add, remove });
  const list = `<ul class="changes">${[...basket].map(([n, ch]) => `<li class="${ch.action}">${esc(basketWords(n, ch))}</li>`).join("")}</ul>`;
  const saved = remove.filter((n) => kept(n) || S.progress?.files?.apps?.[n]);
  const keep = saved.length ? `<p class="hint">Your progress in ${names(saved.map(esc))} stays on this PC, and goes back on the calculator when you install ${saved.length > 1 ? "them" : "it"} again.</p>` : "";
  const many = basket.size > 1;
  const html = (lead) => planHtml(plan, lead).replace("</p>", "</p>" + list);
  const label = basket.size > 1 ? `Applying ${basket.size} changes` : basketWords(...[...basket][0]).replace(/^Install/, "Installing").replace(/^Remove/, "Removing").replace(/^Switch/, "Switching");
  if (fits(plan)) {
    if (await modal("Apply changes", html(`${many ? "These changes are" : "This change is"} made in one go:`) + keep, "Apply", remove.length > 0 && !add.length, true)) run(plan.target, label);
  } else if (plan.fix) {
    if (await modal("Doesn't fit", html(`<b>${many ? "These changes don't" : "This change doesn't"} fit</b> with your apps as they are:`), `${fixWords(plan.fix)} and apply`, !!plan.fix.removed.length, true)) run(plan.fix.target, label);
  } else {
    await modal("Doesn't fit", html(`<b>${many ? "These changes don't" : "This change doesn't"} fit</b> on your calculator. Take something out of the basket, or add apps to remove:`), "Apply", false, true);
  }
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
document.querySelectorAll(".views button").forEach((b) => b.addEventListener("click", () => { look.view = b.dataset.view; saveLook(); render(); }));
for (const [id, key] of [["#f-show", "show"], ["#f-cat", "cat"], ["#f-author", "author"], ["#f-sort", "sort"]])
  $(id).addEventListener("change", () => { look[key] = $(id).value; saveLook(); render(); });
$("#f-reset").addEventListener("click", () => { Object.assign(look, { show: "all", cat: "", author: "" }); saveLook(); render(); });
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
  if (d.ok) { basket.clear(); render(); }
  if (d.ok) toast(`${d.label.replace(/^Reinstalling/, "Reinstalled").replace(/^Installing/, "Installed").replace(/^Removing/, "Removed").replace(/^Switching/, "Switched")} . The calculator restarts: plug it back in to see the change.`, "ok");
  else toast(`${d.label} failed: ${d.error}`, "bad");
});
window.store.state().then((s) => { S = s; render(); });
