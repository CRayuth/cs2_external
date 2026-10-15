# Game snapshot

`vesta.game.snapshot()` returns the latest immutable frame, or `nil` before the
first frame is published. The same object is passed to the `frame` event.

Top-level fields are `sequence`, `timestamp_us`, `connected`, `menu_open`,
`input_ready`, `map`, `screen`, `camera`, `local_player`, `players`, `items`,
`projectiles`, `spectators`, and `bomb`. `local_player.name` contains the
sanitized nickname of the local player. `menu_open` reports whether the Vesta
menu is open and `input_ready` reports whether game input is currently accepted;
`vesta.input.*` refuses new presses unless both allow it.

`vesta.game.radar_snapshot()` returns a lightweight version of the same frame
for background exporters. Its top-level fields are `sequence`, `timestamp_us`,
`connected`, `map`, `camera`, `local_player`, `players`, `items`, `projectiles`,
and `bomb`. It omits `screen`, `menu_open`, `input_ready`, spectators, bones,
hitboxes, the camera `matrix`, `model_path`, projectile `thrower_handle`, and
other heavy ESP data. Its `local_player` keeps only `alive`, `name`, `team`,
`health`, and `game_time`, and a radar player keeps `handle`, `name`, `health`,
`armor`, `team`, `money`, `origin`, `velocity`, `eye_angles`, `weapon`,
`loadout`, and the `invulnerable`, `helmet`, `defuser`, `scoped`, `visible`,
and `spotted` flags. Unlike a `frame`
subscription, the function does not build a complete Lua table 60 times per
second; the script chooses its polling rate from `tick`. Web Radar uses 20 Hz.

Data collection is lazy. The runtime enables player, item, projectile, and bomb
collection only after a corresponding snapshot API is called or a game event is
subscribed. A UI-only script does not activate the complete world sampler.

## Player

A player contains `handle`, `controller_index`, `steam_id64`, `name`, `health`,
`armor`, `team`, `money`, `ping`, `rank`, `wins`, `rank_type`, `simulation_tick`,
`simulation_time`, `last_fired_time`, `emit_sound_time`, `origin`, `velocity`,
`collision_center`, `eye_angles`, `model_path`, `weapon`, `loadout`,
`bones[1..128]`, and `hitboxes`, plus the boolean flags `invulnerable`, `helmet`,
`defuser`, `scoped`, `defusing`, `ducked`, `flashed`, `visible`, `spotted`, and
`legit_visible`, and the `spotted_by_mask` bit set. `weapon` contains `handle`,
`item_definition`, `name`, `ammo`, `max_ammo`, and `pin_pulled`; a `hitboxes`
entry contains `index`, `bone`, `name`, `mins`, `maxs`, and `radius`. Pawn,
controller, and VData addresses are intentionally not exposed.

A projectile contains `id`, `kind`, its current `origin` and `velocity`,
immutable `initial_position` and `initial_velocity`, `thrower_handle`, `bounces`,
`spawn_time`, `detonate_time`, `remaining_lifetime`, `expire_time`,
`effect_tick_begin`, the `detonated`, `smoke_active`, and `smoke_volume_received`
flags, `smoke_position`, `smoke_voxel_size`, and `fire_points` for the actual
inferno boundary. `id` is a stable opaque identity, not an entity address.

`bomb` contains `active`, `active_position`, `planted`, `position`, `site`,
`time_remaining`, `timer_length`, `being_defused`, `defuse_remaining`,
`defuse_length`, `defuse_success`, and `predicted_damage`. `position` is the
planted bomb's world position, while `active` and `active_position` describe a
carried or dropped `C_C4`, including when the local player carries it.

`local_player` contains `alive`, `name`, `team`, `health`, `tick_base`,
`game_time`, `flash_alpha`, and `weapon_type`. `items` entries contain `kind`,
`origin`, `ammo`, and `max_ammo`; `spectators` entries contain `name`,
`steam_id64`, and `mode`; `screen` contains `width` and `height`; `camera`
contains `origin`, `angles`, `fov`, and a 16-entry row-major `matrix`.

```lua
for _, player in ipairs(vesta.game.snapshot().players) do
    vesta.log(string.format("%s: %d HP", player.name, player.health))
end
```

## Geometry queries

```lua
local screen = vesta.game.world_to_screen({x=0, y=0, z=0})
local hit = vesta.game.trace_ray(start_pos, end_pos)
local damage = vesta.game.bomb_damage(position, site, eye_angles, ducked)
```

`trace_ray` uses the already-built collision world and returns `hit`, `fraction`,
`distance`, `position`, `normal`, `penetration`, and `surface_type`. `bomb_damage`
uses Vesta's baked bomb-damage map and returns an integer damage value.

```lua
local path = vesta.game.predict_grenade(origin, velocity, "he")
local pen = vesta.game.penetration_damage(eye, point, target.handle)
```

`predict_grenade` accepts `he`, `flash`, `smoke`, `molotov`, `incendiary`, or
`decoy`, also accepts the matching `weapon_*` names and a numeric weapon handle,
and returns `valid`, `duration`, `end_tick`, `end_position`, `points`, and
`bounces`. It shares the same collision world as Vesta's grenade prediction.

`penetration_damage` creates an independent calculator from the published
weapon context. With `target_handle` it returns `damage`, `hitbox`, `distance`,
and `penetrated`; without a target it returns `damage` and `penetrated` for the
first penetration along `finish - start`.

`radar_overview(map_name, output_stem)` extracts the game's radar overview from
the local CS2 VPK once, decodes it to PNG inside the active script's private data
directory, and returns `pos_x`, `pos_y`, `scale`, `width`, `height`, `has_lower`,
`lower_altitude_max`, `primary_path`, and, when a lower layer exists, `lower_path`.
Map images are not embedded in Vesta.
