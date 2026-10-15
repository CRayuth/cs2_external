# Game snapshot

`vesta.game.snapshot()` возвращает последний immutable frame или `nil` до первой
публикации. Тот же объект передаётся событию `frame`.

Основные поля: `sequence`, `timestamp_us`, `connected`, `menu_open`, `input_ready`,
`map`, `screen`, `camera`, `local_player`, `players`, `items`, `projectiles`,
`spectators`, `bomb`. `local_player.name` содержит фактический sanitized nickname
локального игрока. `menu_open` показывает, открыто ли меню Vesta, а `input_ready` —
принимается ли сейчас game input; `vesta.input.*` отклоняет новые нажатия, пока
оба условия не выполнены.

`vesta.game.radar_snapshot()` возвращает облегчённый вариант того же immutable
frame для фоновых экспортёров. Его основные поля: `sequence`, `timestamp_us`,
`connected`, `map`, `camera`, `local_player`, `players`, `items`, `projectiles`,
`bomb`. Из них исключены `screen`, `menu_open`, `input_ready`, spectators, bones,
hitboxes, `matrix` камеры, `model_path` и `thrower_handle` снаряда. В
`local_player` остаются только `alive`, `name`, `team`, `health`, `game_time`, а
в radar-игроке — `handle`, `name`, `health`, `armor`, `team`, `money`, `origin`,
`velocity`, `eye_angles`, `weapon`, `loadout` и флаги `invulnerable`, `helmet`,
`defuser`, `scoped`, `visible`, `spotted`. В отличие от подписки на `frame`,
функция не создаёт полную Lua-таблицу 60 раз в секунду: скрипт сам выбирает
частоту опроса из `tick`.
Web Radar использует 20 Гц.

Запрос данных ленивый. Lua runtime включает сбор players/items/projectiles/bomb
только после обращения к соответствующему snapshot API или подписки на игровое
событие. Поэтому включённый UI-only скрипт не активирует полный world sampler.

## Player

Игрок содержит `handle`, `controller_index`, `steam_id64`, `name`, `health`,
`armor`, `team`, `money`, `ping`, `rank`, `wins`, `rank_type`, `simulation_tick`,
`simulation_time`, `last_fired_time`, `emit_sound_time`, `origin`, `velocity`,
`collision_center`, `eye_angles`, `model_path`, `weapon`, `loadout`,
`bones[1..128]` и `hitboxes`, булевы флаги `invulnerable`, `helmet`, `defuser`,
`scoped`, `defusing`, `ducked`, `flashed`, `visible`, `spotted`, `legit_visible`
и битовое поле `spotted_by_mask`. `weapon` содержит `handle`, `item_definition`,
`name`, `ammo`, `max_ammo`, `pin_pulled`; элемент `hitboxes` — `index`, `bone`,
`name`, `mins`, `maxs`, `radius`. Адреса pawn/controller/VData намеренно
отсутствуют.

Projectile содержит `id`, `kind`, текущие `origin`, `velocity`, неизменяемые
стартовые `initial_position`, `initial_velocity`, `thrower_handle`, `bounces`,
`spawn_time`, `detonate_time`, `remaining_lifetime`, `expire_time`,
`effect_tick_begin`, флаги `detonated`, `smoke_active`, `smoke_volume_received`,
а также `smoke_position`, `smoke_voxel_size` и `fire_points` для фактической
границы inferno. `id` — стабильный opaque identity, а не адрес сущности.

`bomb` содержит `active`, `active_position`, `planted`, `position`, `site`,
`time_remaining`, `timer_length`, `being_defused`, `defuse_remaining`,
`defuse_length`, `defuse_success`, `predicted_damage`. `position` — мировая
позиция установленной бомбы; `active` и `active_position` описывают переносимую
либо выпавшую `C_C4`, включая случай, когда бомба находится у локального игрока.

`local_player` содержит `alive`, `name`, `team`, `health`, `tick_base`,
`game_time`, `flash_alpha`, `weapon_type`. Элементы `items` содержат `kind`,
`origin`, `ammo`, `max_ammo`; `spectators` — `name`, `steam_id64`, `mode`;
`screen` — `width`, `height`; `camera` — `origin`, `angles`, `fov` и
16-элементную row-major `matrix`.

```lua
for _, player in ipairs(vesta.game.snapshot().players) do
    vesta.log(string.format("%s: %d HP", player.name, player.health))
end
```

## Геометрические запросы

```lua
local screen = vesta.game.world_to_screen({x=0, y=0, z=0})
local hit = vesta.game.trace_ray(start_pos, end_pos)
local damage = vesta.game.bomb_damage(position, site, eye_angles, ducked)
```

`trace_ray` работает на уже построенном collision world и не читает память CS2;
возвращаются `hit`, `fraction`, `distance`, `position`, `normal`, `penetration`,
`surface_type`. `bomb_damage` использует запечённую карту урона проекта и
возвращает целое число.

```lua
local path = vesta.game.predict_grenade(origin, velocity, "he")
local pen = vesta.game.penetration_damage(eye, point, target.handle)
```

`predict_grenade` принимает `he`, `flash`, `smoke`, `molotov`, `incendiary` или
`decoy`, а также соответствующие имена `weapon_*` и числовой weapon handle, и
возвращает `valid`, `duration`, `end_tick`, `end_position`, `points`, `bounces`.
Расчёт использует тот же collision world, что штатный grenade prediction.

`penetration_damage` создаёт из опубликованного weapon context независимый
калькулятор и не изменяет общий ballistics state. С `target_handle` возвращаются
`damage`, `hitbox`, `distance` и `penetrated`; без цели возвращаются `damage` и
`penetrated` для первого прострела в направлении `finish - start`.

`radar_overview(map_name, output_stem)` один раз извлекает штатный radar overview
из локального VPK CS2, декодирует его в PNG внутри приватной директории
данных активного скрипта и возвращает `pos_x`, `pos_y`, `scale`, `width`,
`height`, `has_lower`, `lower_altitude_max`, `primary_path` и, когда нижний слой
есть, `lower_path`. В бинарник Vesta изображения карт не встраиваются.
