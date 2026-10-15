import json
import os
import re
import glob
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
MENU = os.path.join(HERE, "..", "src", "render", "menu")
CFG = os.path.join(HERE, "..", "configs", "legit.cfg")

tree = json.load(open(CFG, encoding="utf-8-sig"))

EXCLUDE = ()


def excluded(path):
    low = path.lower()
    return any(token in low for token in EXCLUDE)


def node_at(path):
    cur = tree
    for seg in path.split("."):
        if isinstance(cur, list):
            cur = cur[int(seg)]
        elif isinstance(cur, dict) and seg in cur:
            cur = cur[seg]
        else:
            return None
    return cur


def leaf_paths(node, prefix=""):
    out = []
    if isinstance(node, dict):
        for k, v in node.items():
            out += leaf_paths(v, (prefix + "." + k) if prefix else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            out += leaf_paths(v, (prefix + "." + str(i)) if prefix else str(i))
    else:
        out.append(prefix)
    return out


# ---- native label extraction: label -> first field referenced in the call ----
ROW = re.compile(
    r'(toggle_row|slider_row|slider_percent_row|select_row|keybind_row|toggle_popup_row|'
    r'settings_popup_row|toggle_color_row|color_row|multiselect_row)\(\s*(?:tr\(\s*)?"([^"]+)"'
    r'[^;]*?;\s*\}', re.S)
CALL = re.compile(
    r'(toggle_row|slider_row|slider_percent_row|select_row|keybind_row|toggle_popup_row|'
    r'settings_popup_row|toggle_color_row|color_row|multiselect_row)\(', re.S)
FIELD = re.compile(r'(?:global|p|s|general|group|cfg|checks|marker|tracers|sound|no_flash|no_smoke|fov|humanizer|prediction|rcs)\.([a-z_][a-z0-9_]*)')

label_field = {}
field_label = collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(MENU, "*.cpp"))):
    if "visual_editor" in f:
        continue
    txt = open(f, encoding="utf-8", errors="ignore").read()
    for m in CALL.finditer(txt):
        label_m = re.match(r'.{0,4}?\(\s*(?:tr\(\s*)?"([^"]+)"', txt[m.start():m.start() + 260], re.S)
        if not label_m:
            continue
        label = label_m.group(1)
        body = txt[m.end():m.end() + 420]
        fields = [x.group(1) for x in FIELD.finditer(body)
                  if not x.group(1).startswith(("m_", "k_"))]
        if fields:
            field_label[fields[0]].append(label)

NATIVE = {}
for fld, labels in field_label.items():
    counts = collections.Counter(labels)
    NATIVE.setdefault(fld, counts.most_common(1)[0][0])

EXTRA = {
    "aimbot_enabled": "Master Switch", "aimbot_key": "Key", "aimbot_activation_mode": "Mode",
    "aimbot_fov": "Radius", "aimbot_smoothing": "Smoothing", "aimbot_humanize": "Amount",
    "aimbot_autowall": "Aim Through Walls", "aimbot_min_damage": "Min Damage",
    "aimbot_lethal_only": "Lethal Only", "aimbot_hitbox_parts": "Hitboxes",
    "aimbot_multipoint": "Multi-Point", "aimbot_visible_only": "Only Visible",
    "aimbot_draw_fov": "Visualization", "aimbot_predictive": "Predictive",
    "aimbot_recoil_sync": "Recoil Sync", "aimbot_checks": "Checks",
    "aimbot_humanizer": "Humanizer Profile", "aimbot_multipoint_config": "Multi-Point",
    "aimbot_prediction": "Prediction", "aimbot_rcs": "RCS", "aimbot_fov_config": "FOV",
    "triggerbot_enabled": "Master Switch", "triggerbot_key": "Key",
    "triggerbot_activation_mode": "Mode", "triggerbot_checks": "Checks",
    "triggerbot_seed_type": "Seed Type", "triggerbot_hitbox_parts": "Hitboxes",
    "triggerbot_hitchance": "Hitchance", "triggerbot_predictive": "Predictive",
    "triggerbot_lethal_only": "Lethal Only", "triggerbot_autostop": "Auto Stop",
    "triggerbot_autowall": "Aim Through Walls", "triggerbot_min_damage": "Min Damage",
    "triggerbot_reaction_time": "Reaction Time (ms)", "triggerbot_delay": "Delay Before",
    "triggerbot_randomize_ms": "Randomize", "triggerbot_outlier_chance": "Outlier Chance",
    "triggerbot_outlier_delay_ms": "Outlier Delay", "triggerbot_delay_after_ms": "Delay After",
    "triggerbot_revolver_pre_cock": "Revolver Pre-Cock",
    "triggerbot_revolver_release_margin_ms": "Revolver Release Margin",
    "triggerbot_min_damage_override_enabled": "Damage Override",
    "triggerbot_min_damage_override": "Override Damage",
    "triggerbot_min_damage_override_mode": "Mode", "triggerbot_min_damage_override_key": "Key",
    "aimbot_min_damage_override_enabled": "Damage Override",
    "aimbot_min_damage_override": "Override Damage",
    "aimbot_min_damage_override_mode": "Mode", "aimbot_min_damage_override_key": "Key",
    "airborne": "Jump", "flashed": "Flash", "smoke": "Smoke", "walls": "Only Visible",
    "flash_threshold": "Flash Threshold", "use_global": "Inherit Global Settings",
    "start_bullet": "Start Bullet", "pitch": "Pitch Strength", "yaw": "Yaw Strength",
    "response_ms": "Correction Time", "smoothness": "Smoothness",
    "randomness": "Strength Variation", "drift": "Path Drift",
    "max_horizon_ms": "Max Horizon", "acceleration": "Acceleration",
    "gravity": "Gravity", "wind": "Wind", "max_step": "Max Step", "damping": "Damping",
    "reaction_min_ms": "Reaction Min", "reaction_max_ms": "Reaction Max",
    "curve": "Curve", "overshoot_chance": "Overshoot Chance",
    "overshoot_amount": "Overshoot Amount", "jitter": "Jitter", "deadzone": "Deadzone",
    "caps": "Cap Points", "sides": "Side Points", "head_scale": "Head Scale",
    "body_scale": "Body Scale", "limb_scale": "Limbs Scale",
    "selection": "Mode", "near_distance_m": "Full Size At", "near_fov": "Max Radius",
    "far_distance_m": "Min Size At", "far_fov": "Min Radius", "distance_curve": "Falloff",
    "target_near_distance_m": "Full Size At", "target_near_fov": "Max Radius",
    "target_far_distance_m": "Min Size At", "target_far_fov": "Min Radius",
    "target_distance_curve": "Falloff", "visualization": "Indicator",
    "enabled": "Enabled", "key": "Key", "fov": "Target FOV", "smoothing": "Smoothing",
}
NATIVE = dict(list(EXTRA.items()) + list(NATIVE.items()))


def humanize(name):
    name = re.sub(r"^m_", "", name)
    name = re.sub(r"[_\-]+", " ", name).strip()
    words = []
    for w in name.split(" "):
        words.append(w.upper() if len(w) == 1 else w.capitalize())
    return " ".join(words)


def label_for(path):
    leaf = path.split(".")[-1]
    if leaf.isdigit():
        leaf = path.split(".")[-2]
    return NATIVE.get(leaf) or humanize(leaf)


RANGES = {
    "combat_global.aimbot_fov": (1, 360, 1, "°"),
    "combat_global.aimbot_smoothing": (0, 50, 1, ""),
    "combat_global.aimbot_humanize": (0, 100, 1, "%"),
    "combat_global.aimbot_min_damage": (1, 100, 1, ""),
    "combat_global.aimbot_min_damage_override": (1, 100, 1, ""),
    "combat_global.triggerbot_hitchance": (0, 100, 1, "%"),
    "combat_global.triggerbot_delay": (0, 500, 1, " ms"),
    "combat_global.triggerbot_randomize_ms": (0, 100, 1, " ms"),
    "combat_global.triggerbot_outlier_chance": (0, 25, 0.5, "%"),
    "combat_global.triggerbot_outlier_delay_ms": (0, 500, 1, " ms"),
    "combat_global.triggerbot_delay_after_ms": (0, 1000, 1, " ms"),
    "combat_global.triggerbot_reaction_time": (0, 400, 1, " ms"),
    "combat_global.triggerbot_min_damage": (1, 100, 1, ""),
    "combat_global.triggerbot_min_damage_override": (1, 100, 1, ""),
    "combat_global.grenade_aim.fov": (1, 180, 1, ""),
    "combat_global.grenade_aim.smoothing": (0, 50, 1, ""),
}

SELECTS = {
    ".aimbot_activation_mode": ["Hold", "Always On", "Toggle"],
    ".triggerbot_activation_mode": ["Hold", "Always On", "Toggle"],
    ".aimbot_min_damage_override_mode": ["Hold", "Always On", "Toggle"],
    ".triggerbot_min_damage_override_mode": ["Hold", "Always On", "Toggle"],
    ".aimbot_fov_config.selection": ["Fixed", "Distance", "Target"],
    ".triggerbot_seed_type": ["None", "Restricted", "Unrestricted"],
    ".activation_mode": ["Always On", "Hold", "Toggle"],
    ".display_mode": ["Indicator", "Text Only"],
    ".m_box.style": ["Full", "Cornered"],
    ".hitsound.style": ["Soft", "Glass", "Pluck", "Crisp", "Flesh"],
    ".hitsound.kill_style": ["Soft", "Glass", "Pluck", "Crisp", "Flesh"],
    ".language": ["English", "Русский"],
    ".menu_scale": ["50%", "75%", "100%", "125%", "150%"],
}

MULTI = {
    ".aimbot_checks": [("airborne", "Jump", 1), ("smoke", "Smoke", 2), ("flashed", "Flash", 4)],
    ".triggerbot_checks": [("airborne", "Jump", 1), ("smoke", "Smoke", 2), ("flashed", "Flash", 4)],
}

BITMASK = {
    ".aimbot_hitbox_parts": [("Head", 1), ("Body", 2), ("Arms", 4), ("Legs", 8)],
    ".triggerbot_hitbox_parts": [("Head", 1), ("Body", 2), ("Arms", 4), ("Legs", 8)],
}

KEYBINDS = [".aimbot_key", ".triggerbot_key", ".activation_key", ".aimbot_min_damage_override_key",
            ".triggerbot_min_damage_override_key", ".aim_key", ".key", ".m_game_controls.forward",
            ".m_game_controls.back", ".m_game_controls.left", ".m_game_controls.right",
            ".m_game_controls.walk", ".m_game_controls.duck", ".m_game_controls.jump",
            ".m_game_controls.attack", ".m_game_controls.attack2"]


def control_for(path, value):
    if isinstance(value, dict) and set(value.keys()) == {"r", "g", "b", "a"}:
        return {"kind": "color"}
    for suffix, opts in SELECTS.items():
        if path.endswith(suffix):
            return {"kind": "select", "options": opts}
    for suffix, items in MULTI.items():
        if path.endswith(suffix):
            return {"kind": "multiselect", "bits": [{"field": f, "label": l, "bit": b} for f, l, b in items]}
    for suffix, items in BITMASK.items():
        if path.endswith(suffix):
            return {"kind": "bitmask", "bits": [{"label": l, "bit": b} for l, b in items]}
    for suffix in KEYBINDS:
        if path.endswith(suffix):
            return {"kind": "keybind"}
    if isinstance(value, (dict, list)):
        return {"kind": "group"}
    rng = RANGES.get(path)
    if rng:
        return {"kind": "slider", "min": rng[0], "max": rng[1], "step": rng[2], "suffix": rng[3]}
    if isinstance(value, bool):
        return {"kind": "toggle"}
    if isinstance(value, int):
        return {"kind": "number", "step": 1}
    if isinstance(value, float):
        return {"kind": "number", "step": 0.01}
    return {"kind": "text"}


def is_color(value):
    return isinstance(value, dict) and set(value.keys()) == {"r", "g", "b", "a"}


def rows_from(prefix, order=None):
    node = node_at(prefix)
    if node is None:
        return []
    leaves = [lp for lp in leaf_paths(node, prefix) if not excluded(lp)]
    rows = []
    top = {}
    for lp in leaves:
        rel = lp[len(prefix) + 1:] if prefix else lp
        head = rel.split(".")[0]
        top.setdefault(head, []).append(lp)
    keys = [k for k in (order or list(top.keys())) if k in top]
    for key in keys:
        group = top[key]
        first = group[0]
        rel = first[len(prefix) + 1:]
        nested = rel.count(".") > 0
        sub = node_at(prefix + "." + key)
        control = control_for(prefix + "." + key, sub)
        if control.get("kind") == "group":
            rows.append({"label": label_for(key), "path": prefix + "." + key,
                         "control": control, "children": rows_from(prefix + "." + key)})
        elif nested:
            rows.append({"label": label_for(key), "path": prefix + "." + key, "control": control})
        else:
            value = node[key] if isinstance(node, dict) else None
            rows.append({"label": label_for(key), "path": first,
                         "control": control_for(first, value)})
    return rows


def card(title, prefix, order=None):
    return {"title": title, "rows": rows_from(prefix, order)}


WEAPON_TABS = ["Global", "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy"]


def combat_page(kind):
    pages = []
    g = "combat_global"
    if kind == "aimbot":
        pages.append({"tab": "Global", "cards": [
            {"title": "TARGETING", "rows": [
                {"label": "Master Switch", "path": g + ".aimbot_enabled", "control": {"kind": "toggle"},
                 "children": [{"label": "Mode", "path": g + ".aimbot_activation_mode",
                               "control": control_for(g + ".aimbot_activation_mode", 0)}]},
                {"label": "Key", "path": g + ".aimbot_key", "control": {"kind": "keybind"}},
                {"label": "Checks", "path": g + ".aimbot_checks", "control": {"kind": "multiselect",
                 "bits": [{"field": "airborne", "label": "Jump", "bit": 1},
                          {"field": "smoke", "label": "Smoke", "bit": 2},
                          {"field": "flashed", "label": "Flash", "bit": 4}]}},
                {"label": "Hitboxes", "path": g + ".aimbot_hitbox_parts", "control": {"kind": "bitmask",
                 "bits": [{"label": "Head", "bit": 1},
                          {"label": "Body", "bit": 2},
                          {"label": "Arms", "bit": 4},
                          {"label": "Legs", "bit": 8}]}},
                {"label": "FOV", "path": g + ".aimbot_fov", "control": control_for(g + ".aimbot_fov", 2),
                 "children": rows_from(g + ".aimbot_fov_config") + [
                     {"label": "Visualization", "path": g + ".aimbot_draw_fov", "control": {"kind": "toggle"}},
                     {"label": "FOV Color", "path": g + ".aimbot_fov_color", "control": {"kind": "color"}}]},
                {"label": "Humanizer Profile", "path": g + ".aimbot_humanize",
                 "control": control_for(g + ".aimbot_humanize", 35),
                 "children": rows_from(g + ".aimbot_humanizer")},
                {"label": "Multi-Point", "path": g + ".aimbot_multipoint", "control": {"kind": "toggle"},
                 "children": rows_from(g + ".aimbot_multipoint_config")},
                {"label": "Prediction", "path": g + ".aimbot_prediction.enabled", "control": {"kind": "toggle"},
                 "children": rows_from(g + ".aimbot_prediction")},
                {"label": "Lethal Only", "path": g + ".aimbot_lethal_only", "control": {"kind": "toggle"}},
            ]},
            {"title": "RECOIL CONTROL", "rows": [
                {"label": "RCS", "path": g + ".aimbot_rcs.enabled", "control": {"kind": "toggle"},
                 "children": rows_from(g + ".aimbot_rcs")}]},
            {"title": "PENETRATION", "rows": [
                {"label": "Only Visible", "path": g + ".aimbot_checks.walls",
                 "control": {"kind": "wallpolicy"}},
                {"label": "Min Damage", "path": g + ".aimbot_min_damage",
                 "control": control_for(g + ".aimbot_min_damage", 65)},
                {"label": "Damage Override", "path": g + ".aimbot_min_damage_override_enabled",
                 "control": {"kind": "toggle"},
                 "children": [
                     {"label": "Mode", "path": g + ".aimbot_min_damage_override_mode",
                      "control": control_for(g + ".aimbot_min_damage_override_mode", 0)},
                     {"label": "Key", "path": g + ".aimbot_min_damage_override_key",
                      "control": {"kind": "keybind"}},
                     {"label": "Override Damage", "path": g + ".aimbot_min_damage_override",
                      "control": control_for(g + ".aimbot_min_damage_override", 10)}]}]},
            {"title": "EXTRA", "rows": [
                {"label": "Aim Through Walls", "path": g + ".aimbot_autowall", "control": {"kind": "toggle"}},
                {"label": "Smoothing", "path": g + ".aimbot_smoothing",
                 "control": control_for(g + ".aimbot_smoothing", 5)},
                {"label": "Only Visible", "path": g + ".aimbot_visible_only", "control": {"kind": "toggle"}},
                {"label": "Recoil Sync", "path": g + ".aimbot_recoil_sync", "control": {"kind": "toggle"}},
                {"label": "Predictive", "path": g + ".aimbot_predictive", "control": {"kind": "toggle"}}]},
            {"title": "PENETRATION CROSSHAIR", "rows": [
                {"label": "Penetration Crosshair", "path": g + ".penetration_crosshair",
                 "control": {"kind": "toggle"}},
                {"label": "Can Penetrate", "path": g + ".penetration_color_yes",
                 "control": {"kind": "color"}},
                {"label": "Cannot Penetrate", "path": g + ".penetration_color_no",
                 "control": {"kind": "color"}}]},
        ]})
    else:
        pages.append({"tab": "Global", "cards": [
            {"title": "TRIGGERBOT CORE", "rows": [
                {"label": "Master Switch", "path": g + ".triggerbot_enabled", "control": {"kind": "toggle"},
                 "children": [{"label": "Mode", "path": g + ".triggerbot_activation_mode",
                               "control": control_for(g + ".triggerbot_activation_mode", 0)}]},
                {"label": "Key", "path": g + ".triggerbot_key", "control": {"kind": "keybind"}},
                {"label": "Checks", "path": g + ".triggerbot_checks", "control": {"kind": "multiselect",
                 "bits": [{"field": "airborne", "label": "Jump", "bit": 1},
                          {"field": "smoke", "label": "Smoke", "bit": 2},
                          {"field": "flashed", "label": "Flash", "bit": 4}]}},
                {"label": "Seed Type", "path": g + ".triggerbot_seed_type",
                 "control": control_for(g + ".triggerbot_seed_type", 0)},
                {"label": "Hitboxes", "path": g + ".triggerbot_hitbox_parts", "control": {"kind": "bitmask",
                 "bits": [{"label": "Head", "bit": 1},
                          {"label": "Body", "bit": 2},
                          {"label": "Arms", "bit": 4},
                          {"label": "Legs", "bit": 8}]}},
                {"label": "Hitchance", "path": g + ".triggerbot_hitchance",
                 "control": control_for(g + ".triggerbot_hitchance", 75)},
                {"label": "Shot Timing", "path": g + ".triggerbot_delay",
                 "control": control_for(g + ".triggerbot_delay", 10),
                 "children": [
                     {"label": "Randomize", "path": g + ".triggerbot_randomize_ms",
                      "control": control_for(g + ".triggerbot_randomize_ms", 5)},
                     {"label": "Outlier Chance", "path": g + ".triggerbot_outlier_chance",
                      "control": control_for(g + ".triggerbot_outlier_chance", 3)},
                     {"label": "Outlier Delay", "path": g + ".triggerbot_outlier_delay_ms",
                      "control": control_for(g + ".triggerbot_outlier_delay_ms", 20)},
                     {"label": "Delay After", "path": g + ".triggerbot_delay_after_ms",
                      "control": control_for(g + ".triggerbot_delay_after_ms", 100)}]},
                {"label": "Reaction Time (ms)", "path": g + ".triggerbot_reaction_time",
                 "control": control_for(g + ".triggerbot_reaction_time", 0)},
                {"label": "Predictive", "path": g + ".triggerbot_predictive", "control": {"kind": "toggle"}},
                {"label": "Lethal Only", "path": g + ".triggerbot_lethal_only", "control": {"kind": "toggle"}},
            ]},
            {"title": "PENETRATION", "rows": [
                {"label": "Only Visible", "path": g + ".triggerbot_checks.walls",
                 "control": {"kind": "wallpolicy"}},
                {"label": "Min Damage", "path": g + ".triggerbot_min_damage",
                 "control": control_for(g + ".triggerbot_min_damage", 90)},
                {"label": "Damage Override", "path": g + ".triggerbot_min_damage_override_enabled",
                 "control": {"kind": "toggle"},
                 "children": [
                     {"label": "Mode", "path": g + ".triggerbot_min_damage_override_mode",
                      "control": control_for(g + ".triggerbot_min_damage_override_mode", 0)},
                     {"label": "Key", "path": g + ".triggerbot_min_damage_override_key",
                      "control": {"kind": "keybind"}},
                     {"label": "Override Damage", "path": g + ".triggerbot_min_damage_override",
                      "control": control_for(g + ".triggerbot_min_damage_override", 1)}]}]},
            {"title": "EXTRA", "rows": [
                {"label": "Aim Through Walls", "path": g + ".triggerbot_autowall", "control": {"kind": "toggle"}},
                {"label": "Auto Stop", "path": g + ".triggerbot_autostop", "control": {"kind": "toggle"}},
                {"label": "Revolver Pre-Cock", "path": g + ".triggerbot_revolver_pre_cock",
                 "control": {"kind": "toggle"}},
                {"label": "Revolver Release Margin", "path": g + ".triggerbot_revolver_release_margin_ms",
                 "control": {"kind": "slider", "min": 0, "max": 200, "step": 1, "suffix": " ms"}}]},
        ]})

    for index, tab in enumerate(WEAPON_TABS[1:]):
        o = "combat_overrides.%d" % index
        rows = [{"label": "Inherit Global Settings", "path": o + ".use_global", "control": {"kind": "toggle"}}]
        cards = [{"title": "OVERRIDE SETTINGS", "rows": rows}]
        cards.append({"title": "TARGETING" if kind == "aimbot" else "TRIGGERBOT CORE",
                      "rows": rows_from(o, [k for k in (node_at(o) or {}).keys()
                                            if k.startswith("aimbot_" if kind == "aimbot" else "triggerbot_")
                                            and k != "use_global"])})
        pages.append({"tab": tab, "cards": cards,
                      "prefixes": ["combat_overrides.%d.%s" % (index, "aimbot" if kind == "aimbot" else "triggerbot"),
                                   "combat_overrides.%d.use_global" % index]})
    pages[0]["prefixes"] = (["combat_global.aimbot", "combat_global.penetration"]
                            if kind == "aimbot" else ["combat_global.triggerbot"])
    return pages


VISUAL_TABS = [("Players", ["esp.m_player"]), ("Items", ["esp.m_item"]),
               ("Projectiles", ["esp.m_projectile"]), ("Bomb", ["esp.m_bomb"]),
               ("Radar", ["esp.m_radar"]),
               ("Effects", ["misc.m_bullet_tracers", "misc.m_hitmarker", "misc.m_hitsound",
                            "esp.m_no_flash", "esp.m_chams", "esp.m_sound"]),
               ("Crosshair", ["esp.m_crosshair"])]

MISC_TABS = [("General", ["misc.language", "misc.menu_scale", "misc.obs_bypass", "misc.limit_fps",
                          "misc.fps_limit", "misc.auto_accept", "misc.m_spectator_list",
                          "misc.m_event_log", "misc.m_keybind_list", "misc.m_game_controls"]),
             ("Grenades", ["misc.m_grenades", "misc.m_nade_helper"]),
             ("Movement", ["misc.m_bunny_hop", "misc.m_edge_jump", "misc.m_auto_stop"]),
             ("Overlay", ["misc.m_watermark", "misc.m_aimbot_status", "misc.m_flash_status", "misc.m_bomb_info"]),
             ("Lua API", ["misc.lua_enabled"])]

P = "esp.m_player."
I = "esp.m_item."
PR = "esp.m_projectile."
B = "esp.m_bomb."
R = "esp.m_radar."
C = "esp.m_crosshair."
M = "misc."

GROUPS = {
    ("visuals", "Players"): [
        ("MASTER", [P + "enabled", P + "activation_mode", P + "activation_key",
                    P + "spectator_sync", P + "m_legit_sync"]),
        ("BOUNDING BOX", [P + "m_box"]),
        ("DETAILS", [P + "m_name", P + "m_weapon", P + "m_health_bar", P + "m_armor_bar",
                     P + "m_info_flags"]),
        ("SKELETON & HITBOXES", [P + "m_skeleton", P + "m_head_circle", P + "m_view_line",
                                 P + "m_offscreen_arrows", P + "m_threat_module", P + "m_hitboxes"]),
        ("LAYOUT", [P + "m_layout"]),
    ],
    ("visuals", "Items"): [
        ("SETTINGS", [I + "enabled", I + "max_distance", I + "m_icon", I + "m_name", I + "m_ammo"]),
        ("FILTERS", [I + "m_filters"]),
    ],
    ("visuals", "Projectiles"): [
        ("VISUAL ELEMENTS", [PR + "enabled", PR + "show_icon", PR + "show_timer_ring",
                             PR + "show_inferno_bounds", PR + "display_mode"]),
        ("COLORS", [PR + "color_he", PR + "color_flash", PR + "color_smoke", PR + "color_molotov",
                    PR + "color_decoy", PR + "default_color"]),
        ("INDICATOR", [PR + "timer_high_color", PR + "timer_low_color", PR + "indicator_background"]),
        ("INFERNO GRADIENT", [PR + "inferno_gradient_width", PR + "inferno_gradient_opacity"]),
    ],
    ("visuals", "Bomb"): [
        ("STATES", [B + "enabled", B + "display_mode", B + "show_active_bomb", B + "show_planted_bomb",
                    B + "show_timer", B + "show_info_panel"]),
        ("COLORS", [B + "active_bomb_color", B + "bomb_color_t", B + "bomb_color_ct",
                    B + "panel_background", B + "timer_text_color"]),
        ("SAFE ZONE (BAKED)", [B + "show_safe_zone", B + "safe_zone_color", B + "safe_zone_bands",
                               B + "safe_zone_band_step", B + "safe_zone_draw_radius"]),
    ],
    ("visuals", "Radar"): [
        ("RADAR PLAYERS", [R + "enabled", R + "activation_mode", R + "activation_key", R + "show_names",
                           R + "show_health", R + "show_armor", R + "show_weapon", R + "name_color",
                           R + "health_color", R + "armor_color", R + "weapon_color"]),
        ("PLAYER STYLE", [R + "enemy_color", R + "direction_color", R + "status_color", R + "marker_scale",
                          R + "text_outline", R + "text_outline_color", R + "text_outline_thickness"]),
        ("RADAR GRENADES", [R + "show_projectiles", R + "show_trajectories", R + "show_grenade_zones",
                            R + "he_color", R + "flash_color", R + "smoke_color", R + "molotov_color",
                            R + "decoy_color", R + "information_scale", R + "trajectory_thickness",
                            R + "trajectory_endpoint_size", R + "zone_fill_alpha",
                            R + "zone_outline_alpha", R + "zone_outline_thickness"]),
    ],
    ("visuals", "Effects"): [
        ("BULLET EFFECTS", [M + "m_bullet_tracers"]),
        ("WORLD HITMARKER", [M + "m_hitmarker"]),
        ("HIT SOUND", [M + "m_hitsound"]),
        ("NO FLASH", ["esp.m_no_flash"]),
        ("CHAMS", ["esp.m_chams"]),
        ("SOUND ESP", ["esp.m_sound"]),
    ],
    ("visuals", "Crosshair"): [
        ("CROSSHAIR SETTINGS", [C + "enabled", C + "dot", C + "lines", C + "t_style", C + "length",
                                C + "gap", C + "thickness", C + "sync", C + "copy_game"]),
        ("COLORS", [C + "color", C + "outline", C + "outline_color", C + "outline_thickness"]),
        ("PENETRATION", [C + "penetration_enabled", C + "penetration_color_yes",
                         C + "penetration_color_no", C + "penetration_min_damage"]),
    ],
    ("misc", "General"): [
        ("INTERFACE", [M + "language", M + "menu_scale", M + "obs_bypass"]),
        ("ENGINE", [M + "limit_fps", M + "fps_limit"]),
        ("AUTOMATION", [M + "auto_accept"]),
        ("SPECTATOR LIST", [M + "m_spectator_list"]),
        ("EVENT LOG", [M + "m_event_log"]),
        ("ACTIVE BINDS", [M + "m_keybind_list"]),
        ("GAME KEY BINDS", [M + "m_game_controls"]),
    ],
    ("misc", "Grenades"): [
        ("TRAJECTORY", [M + "m_grenades"]),
        ("LINEUP HELPER", [M + "m_nade_helper"]),
    ],
    ("misc", "Movement"): [
        ("MOVEMENT", [M + "m_bunny_hop", M + "m_edge_jump", M + "m_auto_stop"]),
    ],
    ("misc", "Overlay"): [
        ("WATERMARK", [M + "m_watermark"]),
        ("AIMBOT STATUS", [M + "m_aimbot_status"]),
        ("FLASH STATUS", [M + "m_flash_status"]),
        ("BOMB INFO PANEL", [M + "m_bomb_info"]),
    ],
    ("misc", "Lua API"): [
        ("LUA RUNTIME", [M + "lua_enabled"]),
    ],
}

CARD_TITLES = {
    "misc.m_bullet_tracers": "BULLET EFFECTS",
    "misc.m_hitmarker": "WORLD HITMARKER",
    "misc.m_hitsound": "HIT SOUND",
    "esp.m_no_flash": "NO FLASH",
    "esp.m_chams": "CHAMS",
    "esp.m_sound": "SOUND ESP",
}


def rows_for(path):
    node = node_at(path)
    if node is None or excluded(path):
        return []
    if isinstance(node, (dict, list)) and not is_color(node):
        return rows_from(path)
    return [{"label": label_for(path), "path": path, "control": control_for(path, node)}]


def grouped_cards(groups):
    cards = []
    for title, members in groups:
        rows = []
        for member in members:
            rows += rows_for(member)
        if rows:
            cards.append({"title": title, "rows": rows})
    return cards

GRENADE_ASSIST = {"title": "GRENADE ASSIST", "rows": [
    {"label": "Enemy Aim Assist", "path": "combat_global.grenade_aim_enabled",
     "control": {"kind": "toggle"}},
    {"label": "Activation Key", "path": "combat_global.grenade_aim_key", "control": {"kind": "keybind"}},
    {"label": "Target FOV", "path": "combat_global.grenade_aim_fov",
     "control": {"kind": "slider", "min": 1, "max": 180, "step": 1, "suffix": ""}},
    {"label": "Smoothing", "path": "combat_global.grenade_aim_smoothing",
     "control": {"kind": "slider", "min": 0, "max": 50, "step": 1, "suffix": ""}}]}


def card_title_for(prefix):
    leaf = prefix.split(".")[-1]
    return (NATIVE.get(leaf) or humanize(leaf)).upper()


def tab_cards(prefixes):
    cards = []
    for prefix in prefixes:
        node = node_at(prefix)
        if node is None or excluded(prefix):
            continue
        title = CARD_TITLES.get(prefix)
        if not isinstance(node, dict):
            cards.append({"title": title or card_title_for(prefix), "rows": [
                {"label": label_for(prefix), "path": prefix, "control": control_for(prefix, node)}]})
            continue
        flat = []
        subs = []
        for key, value in node.items():
            sub = prefix + "." + key
            if excluded(sub):
                continue
            if is_color(value):
                flat.append({"label": label_for(sub), "path": sub, "control": {"kind": "color"}})
            elif isinstance(value, (dict, list)):
                subs.append((key, sub))
            else:
                flat.append({"label": label_for(sub), "path": sub, "control": control_for(sub, value)})
        if flat:
            cards.append({"title": title or card_title_for(prefix), "rows": flat})
        for key, sub in subs:
            rows = rows_from(sub)
            if not rows:
                continue
            cards.append({"title": CARD_TITLES.get(sub) or (NATIVE.get(key) or humanize(key)).upper(),
                          "rows": rows})
    return cards


def build_tabs(page_id, tab_defs):
    tabs = []
    for name, prefixes in tab_defs:
        groups = GROUPS.get((page_id, name))
        cards = grouped_cards(groups) if groups else tab_cards(prefixes)
        tabs.append({"tab": name, "prefixes": prefixes, "cards": cards})
    return tabs


spec = {
    "pages": [
        {"id": "aimbot", "label": "Aimbot", "tabs": combat_page("aimbot")},
        {"id": "triggerbot", "label": "Triggerbot", "tabs": combat_page("triggerbot")},
        {"id": "visuals", "label": "Visuals", "tabs": build_tabs("visuals", VISUAL_TABS)},
        {"id": "misc", "label": "Misc", "tabs": build_tabs("misc", MISC_TABS)},
    ]
}
spec["pages"][3]["tabs"][1]["cards"].append(GRENADE_ASSIST)

PAGE_OWNERS = {
    "aimbot": ["combat_global.aimbot", "combat_global.penetration"],
    "triggerbot": ["combat_global.triggerbot"],
    "visuals": ["esp.", "misc.m_bullet_tracers", "misc.m_hitmarker", "misc.m_hitsound"],
    "misc": ["misc.", "combat_global.grenade_aim"],
}


def collect_row_paths(rows, out):
    for row in rows:
        if row.get("path"):
            out.add(row["path"])
        collect_row_paths(row.get("children", []), out)


def expand_checks(cards):
    for card in cards:
        rows = card.get("rows") or []
        grown = []
        for row in rows:
            grown.append(row)
            control = row.get("control") or {}
            if control.get("kind") != "multiselect":
                expand_checks(row.get("children") or [])
                continue
            node = node_at(row["path"])
            if not isinstance(node, dict):
                continue
            claimed = {bit["field"] for bit in control.get("bits", [])}
            for key, value in node.items():
                if key in claimed or excluded(row["path"] + "." + key):
                    continue
                if key == "walls":
                    extra = {"label": "Only Visible", "path": row["path"] + ".walls",
                             "control": {"kind": "wallpolicy"}}
                else:
                    extra = {"label": label_for(key), "path": row["path"] + "." + key,
                             "control": control_for(row["path"] + "." + key, value)}
                if extra["control"].get("kind") == "group":
                    extra["children"] = rows_from(row["path"] + "." + key)
                grown.append(extra)
                claimed.add(key)
        card["rows"] = grown
        for row in card["rows"]:
            expand_checks(row.get("children") or [])


for page in spec["pages"]:
    for tab in page["tabs"]:
        expand_checks(tab["cards"])


for page in spec["pages"]:
    page["owners"] = PAGE_OWNERS[page["id"]]

live_leaves = set(lp for lp in leaf_paths(tree, "") if not excluded(lp))

global_covered = set()
for page in spec["pages"]:
    for tab in page["tabs"]:
        for c in tab["cards"]:
            collect_row_paths(c["rows"], global_covered)

for page in spec["pages"]:
    owners = PAGE_OWNERS[page["id"]]
    orphans = sorted(lp for lp in live_leaves
                     if any(lp.startswith(o) or lp == o for o in owners)
                     and not any(lp == cp or lp.startswith(cp + ".") or cp.startswith(lp + ".")
                                 for cp in global_covered))
    if not orphans:
        continue
    grouped = collections.OrderedDict()
    for lp in orphans:
        parts = lp.split(".")
        owner = ".".join(parts[:2]) if len(parts) > 2 else ".".join(parts[:1])
        grouped.setdefault(owner, []).append(lp)
    cards = []
    for owner, paths in grouped.items():
        rows = []
        for lp in paths:
            node = node_at(lp)
            if isinstance(node, (dict, list)) and not is_color(node):
                rows.extend(rows_from(lp))
            else:
                rows.append({"label": label_for(lp), "path": lp, "control": control_for(lp, node)})
        if rows:
            cards.append({"title": (CARD_TITLES.get(owner) or humanize(owner.split(".")[-1])).upper(),
                          "rows": rows})
    page["tabs"].append({"tab": "More", "prefixes": owners, "cards": cards})
    for c in cards:
        collect_row_paths(c["rows"], global_covered)
    print("More tab added to %s: %d cards" % (page["id"], len(cards)))


def collect_paths(rows, out):
    for row in rows:
        if row.get("path"):
            out.add(row["path"])
        collect_paths(row.get("children", []), out)


used = set()
for page in spec["pages"]:
    for tab in page["tabs"]:
        for c in tab["cards"]:
            collect_paths(c["rows"], used)

all_leaves = live_leaves
covered = {lp for lp in all_leaves for u in used if lp == u or lp.startswith(u + ".") or u.startswith(lp + ".")}
missing = sorted(lp for lp in all_leaves if lp not in covered and not any(lp.startswith(u + ".") for u in used))

print("spec rows:", len(used), "| tree leaves:", len(all_leaves))
print("uncovered:", len(missing))
for m in missing[:40]:
    print("   ~", m)

with open(os.path.join(HERE, "spec.json"), "w", encoding="utf-8") as fh:
    json.dump(spec, fh, ensure_ascii=False)
print("wrote spec.json", os.path.getsize(os.path.join(HERE, "spec.json")), "bytes")
