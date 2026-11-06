const $ = (s) => document.querySelector(s);
const COLORS = ["#7c5cff", "#3ddc97", "#5b7cff", "#ffb547", "#ff5d73", "#38c6f4", "#e86bd3", "#a3e05b"];
let S = null;            // state from the main process
let tab = "store";
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
  const c = S.calc, box = $("#calc");
  box.className = "calc " + (c.connected ? (c.error ? "err" : "") : "off");
  if (!c.connected) {
    $("#calc-title").textContent = "No calculator";
    $("#calc-title").textContent = c.restarted ? "Calculator restarted" : "No calculator";
    $("#calc-sub").textContent = c.restarted ? "Unplug it and plug it back in" : c.error || "Plug it in with a USB cable";
  } else if (c.error) {
    $("#calc-title").textContent = "Calculator found";
    $("#calc-sub").textContent = c.error;
  } else {
    $("#calc-title").textContent = `NumWorks ${c.model}`;
    $("#calc-sub").textContent = `Epsilon ${c.version} · connected`;
  }
}

function renderStorage() {
  const bar = $("#bar"), legend = $("#legend"), text = $("#storage-text");
  bar.innerHTML = ""; legend.innerHTML = "";
  const c = S.calc;
  if (!c.connected || !c.total) {
    text.textContent = "Connect your calculator to see its storage";
    return;
  }
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
    seg.className = "seg preview";
    seg.style.width = Math.min(100, 100 * preview.size / c.total) + "%";
    seg.style.background = colorFor(preview.add);
    bar.appendChild(seg);
    after += preview.size;
  }
  if (preview?.remove) after -= S.installed.find((a) => a.name === preview.remove)?.size || 0;
  const free = c.total - used;
  text.innerHTML = preview
    ? (after > c.total
        ? `<b style="color:var(--bad)">Doesn't fit</b>: needs ${mb(after - c.total)} more`
        : `After this: <b>${mb(c.total - after)}</b> free of ${mb(c.total)}`)
    : `<b>${mb(free)}</b> free of ${mb(c.total)}`;
  legend.insertAdjacentHTML("beforeend", `<span><i style="background:#0c0d1c;border:1px solid var(--line)"></i>Free · ${mb(free)}</span>`);
}

// ---- views --------------------------------------------------------------------
function matches(a) {
  const q = $("#q").value.trim().toLowerCase();
  return !q || [a.name, a.description, a.author, a.category].some((f) => String(f || "").toLowerCase().includes(q));
}

function renderStore() {
  const v = $("#view-store");
  v.innerHTML = "";
  const list = S.catalog.filter(matches);
  $("#n-store").textContent = S.catalog.length || "";
  if (!list.length) {
    v.innerHTML = `<div class="empty"><b>No apps found</b>Try another search.</div>`;
    return;
  }
  for (const a of list) {
    const card = document.createElement("div");
    card.className = "card";
    const head = document.createElement("div");
    head.className = "card-head";
    head.appendChild(iconEl(a.icon, a.name));
    head.insertAdjacentHTML("beforeend", `<div><h3>${esc(a.name)}</h3>
      <div class="meta">${esc([a.author, a.version && "v" + a.version, a.size && mb(a.size)].filter(Boolean).join(" · "))}</div></div>`);
    card.appendChild(head);
    card.insertAdjacentHTML("beforeend", `<p class="desc">${esc(a.description)}</p>`);
    const foot = document.createElement("div");
    foot.className = "card-foot";
    const tags = [a.category && `<span class="pill">${esc(a.category)}</span>`,
      a.installed && `<span class="pill ok">✓ On your calculator</span>`,
      a.source === "online" && !a.downloaded && `<span class="pill">Download</span>`].filter(Boolean).join(" ");
    foot.innerHTML = `<div>${tags}</div>`;
    const right = document.createElement("div");
    right.style.display = "flex"; right.style.gap = "8px"; right.style.alignItems = "center";
    if (a.source === "library" && !a.installed) {
      const f = document.createElement("button");
      f.className = "link"; f.textContent = "Forget";
      f.onclick = () => window.store.forget(a.id);
      right.appendChild(f);
    }
    const b = document.createElement("button");
    if (a.installed) {
      b.className = "danger"; b.textContent = "Remove";
      b.onclick = () => askRemove(a.name);
      hoverPreview(b, { remove: a.name });
    } else {
      b.className = "primary"; b.textContent = "Install";
      b.onclick = () => askInstall(a);
      hoverPreview(b, { add: a.name, size: a.size || 0 });
    }
    b.disabled = !S.calc.connected || !!S.calc.error || S.busy;
    if (b.disabled) b.title = S.calc.connected ? "" : "Connect your calculator first";
    right.appendChild(b);
    foot.appendChild(right);
    card.appendChild(foot);
    v.appendChild(card);
  }
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
      <div class="meta">${mb(a.size)}${known ? "" : " · not from Numstore"}</div></div>`);
    const b = document.createElement("button");
    b.className = "danger"; b.textContent = "Remove";
    b.disabled = S.busy;
    b.onclick = () => askRemove(a.name);
    hoverPreview(b, { remove: a.name });
    row.appendChild(b);
    v.appendChild(row);
  }
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
    ok.className = danger ? "danger" : "primary";
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
  h += `<p class="warn">The calculator restarts after the change. This takes up to a minute for big apps.</p>`;
  return h;
}

async function askInstall(a) {
  const plan = await window.store.plan({ action: "install", id: a.id });
  if (await modal(`Install ${a.name}?`, planHtml(plan, `${esc(a.name)} will be added to your calculator.`), "Install"))
    run(plan.target, `Installing ${a.name}`);
}

async function askRemove(name) {
  const plan = await window.store.plan({ action: "remove", name });
  if (await modal(`Remove ${name}?`, planHtml(plan, `${esc(name)} will be removed from your calculator. You can install it again any time.`), "Remove", true))
    run(plan.target, `Removing ${name}`);
}

function run(target, label) {
  $("#b-title").textContent = label + "…";
  $("#b-text").textContent = "Preparing…";
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
window.store.on("progress", (p) => { $("#b-text").textContent = p.detail ? `${p.text} ${p.detail}` : p.text; });
window.store.on("done", (d) => {
  $("#busy").classList.add("hidden");
  if (d.ok) toast(`${d.label.replace(/^Installing/, "Installed").replace(/^Removing/, "Removed")} ✓  The calculator restarts; plug it back in to see the change.`, "ok");
  else toast(`${d.label} failed: ${d.error}`, "bad");
});
window.store.state().then((s) => { S = s; render(); });
