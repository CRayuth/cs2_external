# Vesta Device Controller

A LAN web panel that reproduces vesta's **in-game menu** in a browser and drives **every
feature live** — combat, per-weapon overrides, visuals/ESP, and misc — through vesta's own
config API.

The layout deliberately mirrors the native menu: the `YUT` sidebar (Aimbot · Triggerbot ·
Visuals · Misc), the tab pills across the top, bordered "cards" with a legend title, and the
same row controls — toggle pills, sliders with a value read-out, dropdowns, keybind chips,
multi-selects, colour swatches, and `···` popups for nested settings.

## How it works

vesta ships a sandboxed Lua runtime whose config API (`vesta.config.snapshot / schema / set /
patch`) is the same tree the native menu edits, and it applies changes live. So:

```
browser ──HTTP──▶ server.py ──files──▶ main.lua (inside vesta.exe) ──▶ every feature
                                       live.json / request.json
```

1. **`vesta-package/main.lua`** runs inside `vesta.exe`. Each pass it serialises the live
   config tree to `live.json`, and applies anything found in `request.json` via
   `vesta.config.set`. Serialising ~1300 settings is chunked across frames in a coroutine,
   because vesta enforces an **8 ms budget per Lua callback**.
2. **`server.py`** serves the panel, renders the spec against live values, and writes your
   edits back to the bridge. It also auto-launches `vesta.exe` when `cs2.exe` appears.

The bridge uses files under `%TEMP%\vesta\lua\data\vesta_device_controller\`, which both
processes can reach — so no input injection, no UIPI limits, and **the browser never needs
administrator rights**.

## Files

| Path | Role |
| --- | --- |
| `server.py` | LAN web server, panel API, CS2→vesta launcher. **Run this.** |
| `settings.py` | Config, data-dir paths, package install, autoload seeding, bridge I/O, path exclusion. |
| `procs.py` | Detect `cs2.exe`, launch/track `vesta.exe`. |
| `spec.json` | Generated layout: pages → tabs → cards → rows, with native labels and control types. |
| `gen_spec.py` | Regenerates `spec.json` from the config schema + the native menu's own labels. |
| `config.json` | Your settings. |
| `vesta-package/` | The in-vesta Lua bridge (`manifest.json` + `main.lua`). |
| `web/` | The panel UI (native look). |
| `run.bat` | Double-click to start in LAN mode. |

## Requirements

- Windows x64, Python 3.8+ (standard library only — nothing to install).
- `vesta.exe` with Lua scripting enabled (on by default).

## Quick start

1. Edit `config.json` → set `vesta_path` to your `vesta.exe`
   (default guess: `d:\cs2_c\vesta\build\bin\vesta.exe`).
2. Start the panel:

   ```powershell
   cd d:\cs2_c\vesta\deviceController
   python server.py --lan
   ```

3. On startup the server installs the Lua package into `%TEMP%\vesta\lua\scripts\` and enables
   its autoload, so vesta starts it automatically on launch.
4. Open a printed URL, then start `vesta.exe` (the Start button, or just open CS2). The panel
   fills with live values.

## What's exposed

| Page | Tabs |
| --- | --- |
| Aimbot | Global, Pistol, SMG, Rifle, Shotgun, Sniper, Heavy |
| Triggerbot | Global, Pistol, SMG, Rifle, Shotgun, Sniper, Heavy |
| Visuals | Players, Items, Projectiles, Bomb, Radar, Effects, Crosshair |
| Misc | General, Grenades, Movement, Overlay, Lua API |

Cards match the native grouping — `TARGETING`, `RECOIL CONTROL`, `PENETRATION`,
`TRIGGERBOT CORE`, `MASTER`, `BOUNDING BOX`, `DETAILS`, `SKELETON & HITBOXES`,
`INTERFACE`, `ENGINE`, `MOVEMENT`, and so on.

Coverage is verified against the real config tree: **1282/1282 settings reachable**, plus a
safety "More" tab that adopts any live-only setting the layout does not already place.

## Using the controls

- **Toggle pill** — flips a boolean.
- **Slider** — drag; the value and unit show at the right.
- **Dropdown** — enum values (activation mode, seed type, box style, hitsound style…).
- **Keybind chip** — click it, then press a key or mouse button; shows `[C]`, `[MOUSE4]`.
- **`···` popup** — nested settings (FOV curve, Humanizer, RCS, Multi-Point, per-feature
  appearance), exactly like the native popups.
- **Checks / Hitboxes** — multi-select of bits (Jump, Smoke, Flash / Head, Body, Arms, Legs).
- **Colour** — swatch for RGB plus an alpha 0–255 box; writes the four channels.

Edits post to `/api/apply` → `request.json` → `vesta.config.set`. The sidebar status dot reads
`live` when the bridge is streaming and `applying` while a change is in flight. Values changed
in the in-game menu appear here within about a second.

Use the **search box** to filter every setting on the current page across all its tabs.

## Auto-launch Vesta when CS2 opens

`auto_launch` defaults to `true`: while the server runs it watches for `cs2.exe` and starts
`vesta.exe` once when the game appears. Toggle it in the panel or `config.json`.

## LAN firewall

`--lan` binds `0.0.0.0:8760`. Allow it once (admin PowerShell):

```powershell
New-NetFirewallRule -DisplayName "Vesta Device Controller" -Direction Inbound -Protocol TCP -LocalPort 8760 -Action Allow
```

## API

| Method | Path | Body | Result |
| --- | --- | --- | --- |
| GET | `/api/state` | — | live values + status |
| GET | `/api/spec` | — | layout spec |
| POST | `/api/apply` | `{ "edits": [ { "path", "value" } ] }` | queue live changes |
| POST | `/api/vesta` | `{ "action": "start"\|"stop" }` | process control |
| POST | `/api/auto` | `{ "enabled": true\|false }` | toggle the CS2 watcher |
| POST | `/api/install` | — | reinstall Lua package + autoload |

## Regenerating the layout

`spec.json` is generated, not hand-maintained:

```powershell
python gen_spec.py
```

It reads the config tree and borrows the native menu's own row labels and option lists from
`src/render/menu/`, then reports any setting it failed to place. Re-run it after a vesta update
adds or renames settings.

## Troubleshooting

- **Status "offline" while vesta runs:** enable the *Vesta Device Controller* script in
  Misc → Lua API (and make sure Lua scripting is on).
- **A setting looks missing:** check the page's **More** tab; anything the layout does not
  place explicitly is still surfaced there.
- **Bridge script changed:** click **Reinstall bridge**, then restart vesta (or rely on Hot Reload).

## Security note

The panel has no login and edits the running tool — keep it on a trusted LAN only and never
expose port `8760` to the internet. Omit `--lan` for this-PC-only use.

## Design note

No vesta C++ source was modified. This uses vesta's supported extension surface (the Lua
config API and package system) plus an external process/file bridge — the same mechanism the
bundled Web Radar package uses.
