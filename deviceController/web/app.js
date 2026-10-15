const ICON_IDS = { aimbot: "icon-aimbot", triggerbot: "icon-triggerbot", visuals: "icon-visuals", misc: "icon-misc" };

const VK_NAMES = {
    1: "MOUSE1", 2: "MOUSE2", 4: "MOUSE3", 5: "MOUSE4", 6: "MOUSE5",
    8: "BACK", 9: "TAB", 13: "ENTER", 16: "SHIFT", 17: "CTRL", 18: "ALT",
    19: "PAUSE", 20: "CAPS", 27: "ESC", 32: "SPACE", 33: "PGUP", 34: "PGDN",
    35: "END", 36: "HOME", 37: "LEFT", 38: "UP", 39: "RIGHT", 40: "DOWN",
    45: "INSERT", 46: "DELETE", 91: "WIN", 106: "NUM*", 110: "NUM.", 111: "NUM/",
    144: "NUMLOCK", 145: "SCROLL", 186: ";", 187: "=", 188: ",", 189: "-", 190: ".", 191: "/", 192: "`",
};

const CODE_VK = {};
for (let i = 0; i < 26; i += 1) CODE_VK["Key" + String.fromCharCode(65 + i)] = 65 + i;
for (let i = 0; i < 10; i += 1) CODE_VK["Digit" + i] = 48 + i;
for (let i = 1; i <= 24; i += 1) CODE_VK["F" + i] = 111 + i;
Object.assign(CODE_VK, {
    Space: 32, Enter: 13, NumpadEnter: 13, Escape: 27, Tab: 9, Backspace: 8, Delete: 46,
    Insert: 45, Home: 36, End: 35, PageUp: 33, PageDown: 34, ArrowLeft: 37, ArrowUp: 38,
    ArrowRight: 39, ArrowDown: 40, ShiftLeft: 16, ShiftRight: 160, ControlLeft: 17,
    ControlRight: 163, AltLeft: 18, AltRight: 164, MetaLeft: 91, MetaRight: 92,
    Semicolon: 186, Equal: 187, Comma: 188, Minus: 189, Period: 190, Slash: 191,
    Backquote: 192, Backslash: 220, BracketLeft: 219, BracketRight: 221, Quote: 222,
});

function vkName(vk) {
    if (!vk) return "None";
    if (VK_NAMES[vk]) return VK_NAMES[vk];
    if (vk >= 65 && vk <= 90) return String.fromCharCode(vk);
    if (vk >= 48 && vk <= 57) return String(vk - 48);
    return "VK" + vk;
}

const els = {
    nav: document.getElementById("nav"),
    tabs: document.getElementById("tabs"),
    cards: document.getElementById("cards"),
    dot: document.getElementById("dot"),
    statText: document.getElementById("stat-text"),
    mini: document.getElementById("mini"),
    search: document.getElementById("search"),
    start: document.getElementById("btn-start"),
    stop: document.getElementById("btn-stop"),
    reinstall: document.getElementById("btn-reinstall"),
    auto: document.getElementById("auto-toggle"),
    sync: document.getElementById("sync"),
    notice: document.getElementById("notice"),
};

const app = {
    spec: null,
    values: new Map(),
    pending: new Map(),
    page: "aimbot",
    tab: {},
    open: new Set(),
    registry: new Map(),
    query: "",
    connected: false,
    listening: null,
    popover: null,
};

let structure = "";

async function api(path, body) {
    const response = await fetch(path, {
        method: body ? "POST" : "GET",
        headers: body ? { "Content-Type": "application/json" } : {},
        body: body ? JSON.stringify(body) : undefined,
    });
    if (!response.ok) throw new Error("HTTP " + response.status);
    return response.json();
}

function applyEdits(edits) {
    const clean = (edits || []).filter((e) => e && e.path && !isExcluded(e.path));
    if (!clean.length) return Promise.resolve(null);
    return api("/api/apply", { edits: clean }).catch(() => null);
}

function isExcluded(path) {
    return false;
}

function valueAt(path) {
    const pending = app.pending.get(path);
    if (pending) {
        if (Date.now() - pending.at > 6000) app.pending.delete(path);
        else if (app.values.get(path) !== pending.value) return pending.value;
        else app.pending.delete(path);
    }
    return app.values.get(path);
}

function setValue(path, value) {
    app.values.set(path, value);
    app.pending.set(path, { value, at: Date.now() });
}

function currentSig() {
    let keys = "";
    app.values.forEach((_value, path) => { keys += path + "|"; });
    return [app.page, app.tab[app.page] || "", app.query, app.values.size, keys].join("#");
}

function childrenOf(row) {
    return (row.children || []).filter((child) => !isExcluded(child.path || ""));
}

function rowVisible(row) {
    if (isExcluded(row.path || "")) return false;
    const kind = (row.control || {}).kind;
    if (kind === "group") return true;
    if (kind === "multiselect") {
        return (row.control.bits || []).some((bit) => app.values.has(row.path + "." + bit.field));
    }
    if (kind === "color") {
        return app.values.has(row.path + ".r") || app.values.has(row.path + ".a");
    }
    return row.path != null && app.values.has(row.path);
}

function claimedLeaves(rows, sink) {
    (rows || []).forEach((row) => {
        const control = row.control || {};
        const path = row.path;
        if (control.kind === "multiselect") {
            (control.bits || []).forEach((bit) => sink.add(path + "." + bit.field));
        } else if (control.kind === "color") {
            ["r", "g", "b", "a"].forEach((c) => sink.add(path + "." + c));
        } else if (control.kind !== "group" && path) {
            sink.add(path);
        }
        claimedLeaves(row.children, sink);
    });
    return sink;
}

function pretty(name) {
    const cleaned = String(name).replace(/^m_/, "").replace(/[_\-]+/g, " ").trim();
    return cleaned.replace(/\b\w/g, (c) => c.toUpperCase());
}

function inferKind(path, raw) {
    if (/(_key|activation_key|key)$/.test(path)) return "keybind";
    if (typeof raw === "boolean") return "toggle";
    if (typeof raw === "number") return "number";
    return "text";
}

function matchesQuery(row) {
    if (!app.query) return true;
    const haystack = ((row.label || "") + " " + (row.path || "")).toLowerCase();
    if (haystack.includes(app.query)) return true;
    const kids = childrenOf(row);
    if (kids.some(matchesQuery)) {
        if ((row.control || {}).kind === "group") app.open.add(row.path);
        return true;
    }
    return false;
}

function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text != null) node.textContent = text;
    return node;
}

function closePopover() {
    if (app.popover) {
        app.popover.remove();
        app.popover = null;
    }
}

function openPopover(anchor, options) {
    closePopover();
    const pop = el("div", "pop");
    options.forEach((option) => {
        const item = el("div", "opt");
        item.append(el("span", null, option.label));
        if (option.checked) item.append(el("span", "tick", "✓"));
        item.addEventListener("click", (event) => {
            event.stopPropagation();
            option.onPick();
            if (!option.keepOpen) {
                closePopover();
                render();
            }
        });
        pop.append(item);
    });
    document.body.append(pop);
    const box = anchor.getBoundingClientRect();
    pop.style.left = Math.max(8, Math.min(box.left, window.innerWidth - pop.offsetWidth - 8)) + "px";
    pop.style.top = (box.bottom + pop.offsetHeight > window.innerHeight ? box.top - pop.offsetHeight - 4 : box.bottom + 4) + "px";
    app.popover = pop;
}

window.addEventListener("resize", closePopover);
document.addEventListener("click", closePopover);

function makeToggle(path) {
    const button = el("button", "switch");
    button.type = "button";
    const update = () => button.classList.toggle("on", !!valueAt(path));
    button.addEventListener("click", () => {
        const next = !button.classList.contains("on");
        setValue(path, next);
        button.classList.toggle("on", next);
        applyEdits([{ path, value: next }]);
    });
    return { node: button, update };
}

function makeWallPolicy(path) {
    const button = el("button", "switch");
    button.type = "button";
    const update = () => button.classList.toggle("on", Number(valueAt(path)) === 0);
    button.addEventListener("click", () => {
        const next = !button.classList.contains("on");
        setValue(path, next ? 0 : 1);
        button.classList.toggle("on", next);
        applyEdits([{ path, value: next ? 0 : 1 }]);
    });
    return { node: button, update };
}

function makeSlider(path, control, label) {
    const wrap = el("div", "row stack");
    const head = el("div", "slider-head");
    const value = el("span", "value");
    head.append(el("span", "label", label == null ? "" : label), value);
    const input = el("input");
    input.type = "range";
    input.min = control.min;
    input.max = control.max;
    input.step = control.step;
    const isInt = Number.isInteger(control.step);
    const update = () => {
        if (document.activeElement === input) return;
        const raw = valueAt(path);
        if (raw == null) return;
        input.value = raw;
        const shown = isInt ? Math.round(Number(raw)) : Number(raw);
        value.textContent = shown + (control.suffix || "");
        const min = Number(control.min);
        const max = Number(control.max);
        const pct = max > min ? ((Number(raw) - min) / (max - min)) * 100 : 0;
        input.style.background = "linear-gradient(90deg, var(--accent) " + Math.max(0, Math.min(100, pct)) +
            "%, var(--track) " + Math.max(0, Math.min(100, pct)) + "%)";
    };
    input.addEventListener("input", () => {
        const next = isInt ? parseInt(input.value, 10) : parseFloat(input.value);
        setValue(path, next);
        value.textContent = next + (control.suffix || "");
        applyEdits([{ path, value: next }]);
    });
    update();
    wrap.append(head, input);
    return { node: wrap, update, head };
}

function makeNumber(path, control) {
    const input = el("input", "field-input");
    input.type = "number";
    input.step = control.step || 1;
    const update = () => {
        if (document.activeElement === input) return;
        const raw = valueAt(path);
        input.value = raw == null ? "" : String(raw);
    };
    input.addEventListener("change", () => {
        if (input.value === "") return;
        const next = Number(input.value);
        setValue(path, next);
        applyEdits([{ path, value: next }]);
    });
    update();
    return { node: input, update };
}

function makeText(path) {
    const input = el("input", "field-input");
    input.type = "text";
    const update = () => {
        const raw = valueAt(path);
        if (document.activeElement !== input) input.value = raw == null ? "" : String(raw);
    };
    input.addEventListener("change", () => {
        setValue(path, input.value);
        applyEdits([{ path, value: input.value }]);
    });
    update();
    return { node: input, update };
}

function makeSelect(path, control) {
    const select = el("select");
    control.options.forEach((option, index) => {
        const item = el("option", null, option);
        item.value = String(index);
        select.append(item);
    });
    const update = () => {
        const raw = valueAt(path);
        if (raw != null && document.activeElement !== select) select.value = String(raw);
    };
    select.addEventListener("change", () => {
        const next = parseInt(select.value, 10);
        setValue(path, next);
        applyEdits([{ path, value: next }]);
    });
    update();
    return { node: select, update };
}

function makeKeybind(path) {
    const button = el("button", "keybind");
    button.type = "button";
    const update = () => {
        button.textContent = "[" + vkName(valueAt(path)) + "]";
    };
    const finish = (event) => {
        let vk = null;
        if (event.type === "keydown") vk = CODE_VK[event.code] || null;
        else if (event.type === "mousedown") vk = [1, 2, 4, 5, 6][event.button] ?? null;
        window.removeEventListener("keydown", finish, true);
        window.removeEventListener("mousedown", finish, true);
        button.classList.remove("listening");
        app.listening = null;
        if (vk != null) {
            setValue(path, vk);
            applyEdits([{ path, value: vk }]);
            update();
        }
        event.preventDefault();
        event.stopPropagation();
    };
    button.addEventListener("click", (event) => {
        event.stopPropagation();
        if (app.listening) return;
        app.listening = path;
        button.classList.add("listening");
        button.textContent = "press...";
        window.addEventListener("keydown", finish, true);
        window.addEventListener("mousedown", finish, true);
    });
    update();
    return { node: button, update };
}

function makeMultiselect(path, control) {
    const button = el("button", "keybind");
    button.type = "button";
    const bits = control.bits || [];
    const read = () => {
        let mask = 0;
        bits.forEach((bit) => {
            if (valueAt(path + "." + bit.field)) mask |= bit.bit;
        });
        return mask;
    };
    const update = () => {
        const mask = read();
        const chosen = bits.filter((bit) => mask & bit.bit).map((bit) => bit.label);
        button.textContent = chosen.length === 0 ? "None" :
            (chosen.length === bits.length ? "All" : chosen.join(", "));
    };
    button.addEventListener("click", (event) => {
        event.stopPropagation();
        openPopover(button, bits.map((bit) => ({
            label: bit.label,
            checked: !!(read() & bit.bit),
            keepOpen: true,
            onPick: () => {
                const next = !app.values.get(path + "." + bit.field);
                setValue(path + "." + bit.field, next);
                applyEdits([{ path: path + "." + bit.field, value: next }]);
                update();
                const mask = read();
                const chosen = bits.filter((b) => mask & b.bit).map((b) => b.label);
                button.textContent = chosen.length === 0 ? "None" :
                    (chosen.length === bits.length ? "All" : chosen.join(", "));
            },
        })));
    });
    update();
    return { node: button, update };
}

function makeColor(path) {
    const wrap = el("div", "color-wrap");
    const swatch = el("input", "swatch");
    swatch.type = "color";
    const alpha = el("input", "field-input");
    alpha.type = "number";
    alpha.min = 0;
    alpha.max = 255;
    alpha.style.width = "62px";
    alpha.style.minWidth = "62px";
    const hex = el("span", "hex");
    const channels = () => ({
        r: Number(valueAt(path + ".r") || 0),
        g: Number(valueAt(path + ".g") || 0),
        b: Number(valueAt(path + ".b") || 0),
        a: Number(valueAt(path + ".a") || 0),
    });
    const update = () => {
        const c = channels();
        const rgb = "#" + ["r", "g", "b"].map((k) => c[k].toString(16).padStart(2, "0")).join("");
        swatch.value = rgb;
        hex.textContent = rgb.toUpperCase();
        if (document.activeElement !== alpha) alpha.value = String(c.a);
    };
    swatch.addEventListener("input", () => {
        const v = swatch.value;
        const edits = [
            { path: path + ".r", value: parseInt(v.slice(1, 3), 16) },
            { path: path + ".g", value: parseInt(v.slice(3, 5), 16) },
            { path: path + ".b", value: parseInt(v.slice(5, 7), 16) },
        ];
        edits.forEach((edit) => setValue(edit.path, edit.value));
        applyEdits(edits);
        update();
    });
    alpha.addEventListener("change", () => {
        const next = Math.max(0, Math.min(255, parseInt(alpha.value, 10) || 0));
        setValue(path + ".a", next);
        applyEdits([{ path: path + ".a", value: next }]);
    });
    update();
    wrap.append(swatch, hex, alpha);
    return { node: wrap, update };
}

function makeBitmask(path, control) {
    const button = el("button", "keybind");
    button.type = "button";
    const bits = control.bits || [];
    const current = () => Number(valueAt(path)) || 0;
    const label = (mask) => {
        const chosen = bits.filter((bit) => mask & bit.bit).map((bit) => bit.label);
        return chosen.length === 0 ? "None" : (chosen.length === bits.length ? "All" : chosen.join(", "));
    };
    const update = () => { button.textContent = label(current()); };
    button.addEventListener("click", (event) => {
        event.stopPropagation();
        const mask = current();
        openPopover(button, bits.map((bit) => ({
            label: bit.label,
            checked: !!(mask & bit.bit),
            keepOpen: true,
            onPick: () => {
                const all = bits.reduce((sum, b) => sum | b.bit, 0);
                const next = (current() ^ bit.bit) & all;
                setValue(path, next);
                applyEdits([{ path, value: next }]);
                update();
            },
        })));
    });
    update();
    return { node: button, update };
}

function buildControl(row) {
    const control = row.control || { kind: "toggle" };
    switch (control.kind) {
        case "toggle": return makeToggle(row.path);
        case "wallpolicy": return makeWallPolicy(row.path);
        case "slider": return makeSlider(row.path, control, row.label);
        case "number": return makeNumber(row.path, control);
        case "text": return makeText(row.path);
        case "select": return makeSelect(row.path, control);
        case "keybind": return makeKeybind(row.path);
        case "multiselect": return makeMultiselect(row.path, control);
        case "bitmask": return makeBitmask(row.path, control);
        case "color": return makeColor(row.path);
        default: return makeToggle(row.path);
    }
}

function renderRow(row, depth) {
    const control = row.control || {};
    const isGroup = control.kind === "group";
    const kids = childrenOf(row);
    const hasKids = kids.length > 0;

    if (isGroup && !app.open.has(row.path) && app.query === "") {
        const collapsed = el("div", "row");
        const label = el("span", "label", row.label);
        const dots = el("button", "dots", "···");
        dots.type = "button";
        dots.addEventListener("click", (event) => {
            event.stopPropagation();
            app.open.add(row.path);
            render();
        });
        collapsed.append(label, dots);
        collapsed.dataset.path = row.path || "";
        collapsed.dataset.search = ((row.label || "") + " " + (row.path || "")).toLowerCase();
        return collapsed;
    }

    if (control.kind === "slider") {
        const built = makeSlider(row.path, control, row.label);
        const container = el("div");
        if (hasKids) built.head.append(dotsFor(row));
        container.append(built.node);
        if (hasKids && app.open.has(row.path)) container.append(renderChildren(row, kids, depth));
        container.dataset.path = row.path || "";
        container.dataset.search = ((row.label || "") + " " + (row.path || "")).toLowerCase();
        app.registry.set(row.path, built.update);
        return container;
    }

    const line = el("div", "row");
    const labelWrap = el("span", "label");
    labelWrap.append(document.createTextNode(row.label || ""));
    if (isGroup) labelWrap.append(el("span", "hint", kids.length + " settings"));
    line.append(labelWrap);

    const right = el("span", null);
    right.style.display = "inline-flex";
    right.style.alignItems = "center";
    right.style.gap = "4px";

    if (!isGroup) {
        const built = buildControl(row);
        right.append(built.node);
        if (built.update) app.registry.set(row.path, built.update);
    }
    if (hasKids) right.append(dotsFor(row));
    line.append(right);
    line.dataset.path = row.path || "";
    line.dataset.search = ((row.label || "") + " " + (row.path || "")).toLowerCase();

    const holder = el("div");
    holder.append(line);
    if (hasKids && app.open.has(row.path)) holder.append(renderChildren(row, kids, depth));
    holder.dataset.search = line.dataset.search;
    return holder;
}

function dotsFor(row) {
    const dots = el("button", "dots" + (app.open.has(row.path) ? " open" : ""), "···");
    dots.type = "button";
    dots.title = "More settings";
    dots.addEventListener("click", (event) => {
        event.stopPropagation();
        if (app.open.has(row.path)) app.open.delete(row.path);
        else app.open.add(row.path);
        render();
    });
    return dots;
}

function renderChildren(row, kids, depth) {
    const box = el("div", "children");
    kids.forEach((child) => box.append(renderRow(child, depth + 1)));
    return box;
}

function renderCard(card) {
    const node = el("section", "card");
    node.append(el("h3", null, card.title));
    const rows = (card.rows || []).filter(rowVisible);
    rows.forEach((row) => node.append(renderRow(row, 0)));
    node.dataset.search = (card.title || "").toLowerCase();
    return node;
}

function activeTabs() {
    const page = app.spec.pages.find((p) => p.id === app.page) || app.spec.pages[0];
    return page;
}

function orphanTab(page) {
    const covered = claimedLeaves(
        app.spec.pages.flatMap((each) => each.tabs || [])
            .flatMap((tab) => tab.cards || [])
            .flatMap((card) => card.rows || []),
        new Set());

    const owners = page.owners || [];
    const orphans = [];
    app.values.forEach((_value, path) => {
        if (isExcluded(path) || covered.has(path)) return;
        const scoped = owners.some((owner) =>
            path === owner || path.startsWith(owner + ".") || owner.startsWith(path + "."));
        if (!scoped && page.id !== "misc") return;
        orphans.push(path);
    });
    if (!orphans.length) return null;
    const groups = new Map();
    orphans.sort().forEach((path) => {
        const parts = path.split(".");
        const owner = parts.length > 2 ? parts.slice(0, 2).join(".") : parts[0];
        if (!groups.has(owner)) groups.set(owner, []);
        groups.get(owner).push(path);
    });
    const cards = [];
    groups.forEach((paths, owner) => {
        const rows = paths.map((path) => ({
            label: pretty(path.split(".").pop()),
            path,
            control: { kind: inferKind(path, app.values.get(path)) },
        }));
        cards.push({ title: pretty(owner.split(".").pop()), rows });
    });
    return { tab: "More", cards };
}

function render() {
    if (!app.spec) return;
    closePopover();
    app.registry.clear();
    structure = currentSig();

    els.nav.textContent = "";
    app.spec.pages.forEach((page) => {
        const button = el("button", "nav" + (page.id === app.page ? " active" : ""));
        button.type = "button";
        const template = document.getElementById(ICON_IDS[page.id]);
        if (template) button.append(template.content.cloneNode(true));
        button.append(el("span", null, page.label));
        button.addEventListener("click", () => {
            app.page = page.id;
            render();
        });
        els.nav.append(button);
    });

    const page = activeTabs();
    if (!page) return;
    const tabs = page.tabs.slice();
    const extra = orphanTab(page);
    if (extra) tabs.push(extra);
    if (!app.tab[page.id] || !tabs.some((t) => t.tab === app.tab[page.id])) app.tab[page.id] = tabs[0].tab;

    els.tabs.textContent = "";
    tabs.forEach((tab) => {
        const button = el("button", "tab" + (tab.tab === app.tab[page.id] ? " active" : ""));
        button.type = "button";
        button.textContent = tab.tab;
        button.addEventListener("click", () => {
            app.tab[page.id] = tab.tab;
            render();
        });
        els.tabs.append(button);
    });

    const active = tabs.find((t) => t.tab === app.tab[page.id]) || tabs[0];
    els.cards.textContent = "";

    if (app.query) {
        let shown = 0;
        tabs.forEach((tab) => tab.cards.forEach((card) => {
            const rows = (card.rows || []).filter((row) => rowVisible(row) && matchesQuery(row));
            if (!rows.length) return;
            const node = el("section", "card wide");
            node.append(el("h3", null, tab.tab + " · " + card.title));
            rows.forEach((row) => node.append(renderRow(row, 0)));
            els.cards.append(node);
            shown += rows.length;
        }));
        if (!shown) els.cards.append(el("div", "empty", "No settings match \"" + app.query + "\""));
        return;
    }

    active.cards.forEach((card) => {
        const rows = (card.rows || []).filter(rowVisible);
        if (!rows.length) return;
        const node = renderCard(card);
        if (node.querySelectorAll(".row").length === 0) return;
        els.cards.append(node);
    });
    if (!els.cards.children.length) {
        els.cards.append(el("div", "empty",
            app.connected ? "No live values for this tab yet." : "Waiting for the live bridge — start vesta.exe."));
    }
}

function refreshValues() {
    app.registry.forEach((update) => {
        try {
            update();
        } catch (error) { /* keep the panel alive on a single bad control */ }
    });
}

function renderStatus(data) {
    app.connected = !!data.connected;
    els.dot.classList.toggle("live", app.connected);
    els.statText.textContent = app.connected ? (data.synced ? "live" : "applying") : "offline";
    els.mini.textContent = "CS2 " + (data.game_running ? "running" : "off") +
        "  ·  Vesta " + (data.vesta_running ? "running" : "off") +
        "  ·  " + (data.values || []).length + " settings";
    els.sync.textContent = (data.bridge && data.bridge.applied ? "rev " + data.bridge.applied : "") +
        (data.connected ? (data.synced ? "  ·  synced" : "  ·  pending") : "");
    els.auto.classList.toggle("on", !!data.auto_launch);

    const warnings = [];
    if (!data.connected && data.vesta_running) {
        warnings.push("vesta.exe is running but the bridge is silent — enable the 'Vesta Device Controller' script in Misc → Lua API.");
    } else if (!data.connected) {
        warnings.push("Start vesta.exe (or open CS2 with auto-launch) to bring the live bridge online.");
    }
    if (!data.vesta_path_ok) warnings.push("vesta.exe path not found — edit config.json.");
    els.notice.hidden = warnings.length === 0;
    els.notice.textContent = warnings.join("   •   ");
}

async function poll() {
    try {
        const data = await api("/api/state");
        const next = new Map();
        (data.values || []).forEach((item) => {
            if (!isExcluded(item.path)) next.set(item.path, item.value);
        });
        app.values = next;
        renderStatus(data);
        if (!app.spec) return;
        if (currentSig() !== structure) {
            const scroll = els.cards.scrollTop;
            render();
            els.cards.scrollTop = scroll;
        }
        refreshValues();
    } catch (error) {
        app.connected = false;
        els.dot.classList.remove("live");
        els.statText.textContent = "server unreachable";
    }
}

els.search.addEventListener("input", () => {
    app.query = els.search.value.trim().toLowerCase();
    render();
});
els.start.addEventListener("click", async () => { await api("/api/vesta", { action: "start" }); poll(); });
els.stop.addEventListener("click", async () => { await api("/api/vesta", { action: "stop" }); poll(); });
els.reinstall.addEventListener("click", async () => { await api("/api/install", {}); poll(); });
els.auto.addEventListener("click", async () => {
    const next = !els.auto.classList.contains("on");
    els.auto.classList.toggle("on", next);
    await api("/api/auto", { enabled: next });
});

(async function boot() {
    try {
        app.spec = await api("/api/spec");
    } catch (error) {
        app.spec = { pages: [] };
    }
    render();
    poll();
    setInterval(poll, 1000);
}());
