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
  const free = c.total - used;
  text.innerHTML = preview
    ? (after > c.total
        ? `<b style="color:var(--red)">Doesn't fit</b>: needs ${mb(after - c.total)} more`
        : `After this: <b>${mb(c.total - after)}</b> free of ${mb(c.total)}`)
    : `<b>${mb(free)}</b> free of ${mb(c.total)}`;
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#e4e5e8"></i>Free · ${mb(free)}</span>`);
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

function render() {
  if (!S) return;
  renderCalc(); renderStorage(); renderStore(); renderDevice();
  $("#view-store").classList.toggle("hidden", tab !== "store");
  $("#view-device").classList.toggle("hidden", tab !== "device");
  const note = $("#online-note");
  note.classList.toggle("hidden", !S.onlineError || tab !== "store");
  note.textContent = S.onlineError ? "Showing the apps that come with Numstore (the online catalog isn't reachable right now)." : "";
}

// ---- dialogs ------------------------------------------------------------------
function modal(title, html, okText, danger) {
  return new Promise((res) => {
    $("#m-title").textContent = title;
    $("#m-body").innerHTML = html;
    const ok = $("#m-ok");
    ok.textContent = okText;
    ok.className = danger ? "key dark" : "key orange";
    $("#modal").classList.remove("hidden");
    const close = (v) => { $("#modal").classList.add("hidden"); ok.onclick = null; $("#m-cancel").onclick = null; res(v); };
    ok.onclick = () => close(true);
    $("#m-cancel").onclick = () => close(false);
    ok.disabled = !!$("#m-body .space.bad");
  });
}

function planHtml(plan, lead) {
  let h = `<p>${lead}</p>`;
  if (plan.lost.length)
    h += `<div class="warnbox"><b>${plan.lost.map(esc).join(", ")}</b> ${plan.lost.length > 1 ? "were" : "was"} not installed with Numstore, so ${plan.lost.length > 1 ? "they" : "it"} will be removed. Add the .nwa file first if you want to keep ${plan.lost.length > 1 ? "them" : "it"}.</div>`;
  const fits = plan.size <= plan.total;
  h += `<div class="space ${fits ? "" : "bad"}"><span>Apps after this</span><span>${mb(plan.size)} of ${mb(plan.total)}</span></div>`;
  h += `<p class="hint" style="margin-top:12px">The calculator restarts after the change. This takes up to a minute for big apps.</p>`;
  return h;
}

async function askInstall(a, variant) {
  const plan = await window.store.plan({ action: "install", id: a.id, variant });
  const label = a.variants.find((x) => x.id === variant)?.label;
  const multi = a.variants.length > 1;
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
  if (d.ok) toast(`${d.label.replace(/^Installing/, "Installed").replace(/^Removing/, "Removed").replace(/^Switching/, "Switched")} . The calculator restarts: plug it back in to see the change.`, "ok");
  else toast(`${d.label} failed: ${d.error}`, "bad");
});
window.store.state().then((s) => { S = s; render(); });
