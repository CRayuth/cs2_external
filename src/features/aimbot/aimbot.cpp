#include <stdafx.hpp>
#include <features/aimbot/aimbot_internal.hpp>
#include <features/hvh/hvh.hpp>

namespace features::aimbot
{
namespace
{
// HvH rage lock. When the injected module's panel asks for rage, this aimbot runs even though its own switch
// may be off, and it runs on the rage parameters rather than the profile's: always active, a fixed cone, every
// hitgroup, and the wall policy the rage autowall asks for. The rest of the profile still decides the humanising,
// the smoothing and the local checks, because rage is a target-selection demand, not an excuse to stop
// behaving like the player configured.
void apply_rage_lock(config::combat_profile::resolved_config& cfg)
{
    const auto& settings = features::hvh::controller().settings;
    if (settings.enable_rage == 0)
        return;

    cfg.aimbot.enabled = true;
    cfg.aimbot.activation_mode = config::combat_profile::activation::always;
    cfg.aimbot.fov_config.selection = config::combat_profile::fov_settings::fixed;
    if (settings.rage_fov > 0)
        cfg.aimbot.fov = settings.rage_fov;
    if (settings.rage_min_damage > 0)
        cfg.aimbot.min_damage = static_cast<float>(settings.rage_min_damage);
    cfg.aimbot.lethal_only = false;
    cfg.aimbot.hitbox_parts = config::combat_profile::aim_part::all;
    cfg.aimbot.checks.walls = settings.rage_autowall != 0 ? config::combat_profile::wall_policy::penetration
                              : config::combat_profile::wall_policy::block;
    // The module steering silently is the rage setting; the transport that carries it is chosen per pass.
    cfg.aimbot.silent = cfg.aimbot.silent || settings.rage_silent != 0;
}
} // namespace

// The overlay's exit key calls this on the render thread while the combat worker is inside tick(); the worker
// also calls it itself when the pass goes inactive. The serialises against tick() so neither can observe the
// other halfway through a state change, which is what used to lose a trigger release.
void aimbot_t::reset()
{
    std::scoped_lock lock(m_state_mutex);
    reset_state();
}

void aimbot_t::reset_state()
{
    // Nothing is aiming any more: a held silent-aim request must not keep steering commands through a death,
    // a map change or the menu.
    command_steering().clear();
    // Idle is the right moment to forgive a ring that would not resolve: the next acquisition gets a fresh
    // silent trial instead of being locked to the mouse for the rest of the session.
    command_steering().rearm();
    if (m_revolver_pre_cock_down)
    {
        platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_revolver_pre_cock_down = false;
    }
    if (m_trigger_held)
    {
        auto released = true;
        if (m_seed_held_secondary)
        {
            platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::secondary_up);
        }
        else if (m_seed_held_proxy)
        {
            platform::windows::bound_input().key(VK_F24, false);
        }
        else
        {
            released = platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        }

        if (released)
            m_trigger_held = false;
    }
    m_trigger_press_shots = -1;
    m_seed_press_active = false;
    m_seed_held_secondary = false;
    m_seed_held_proxy = false;
    m_revolver_committed = false;
    m_revolver_weapon = 0;

    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_cooldown_until = {};
    m_trigger_target_valid = false;
    m_seed_last_shots = -1;
    m_seed_pending_target_tick = -1;
    m_seed_pending_time = {};
    m_seed_phase_receipt = {};
    m_seed_memo_sequence = -1;
    m_seed_memo_pitch = {};
    m_seed_memo_yaw = {};
    m_last_time = 0.0f;
    m_aim_pawn = 0;
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_selection_view = {};
    m_aim_selection_weapon = 0;
    m_aim_selection_at = {};
    m_aim_last_bone = -1;
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_last_camera_angles = {};
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_virtual_angles = {};
    m_aim_virtual_angles_valid = false;
    m_aim_degrees_per_pixel = {};
    m_aim_degrees_candidate = {};
    m_aim_degrees_confirmations = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_previous_relative_velocity = {};
    m_aim_relative_acceleration = {};
    m_aim_velocity_valid = false;
    m_aim_simulation_tick = -1;
    m_aim_simulation_time = 0.0f;
    m_aim_velocity_samples = 0;
    m_rcs_raw = {};
    m_rcs_target = {};
    m_rcs_velocity = {};
    m_rcs_applied = {};
    m_rcs_mouse_error = {};
    m_rcs_pending_mouse = {};
    m_rcs_last_input_sequence = -1;
    m_rcs_last_input_time = {};
    m_rcs_input_step = 24;
    m_rcs_active = false;

    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

void aimbot_t::on_render(render::draw::draw_list& draw_list)
{
    refresh_runtime_config();
    const auto eye_pos = game::camera().origin();
    const auto view_angles = game::camera().angles();

    const auto& ctx = ballistics().ctx();
    const auto cfg = runtime_config()->combat.get(ctx.weapon_type);

    if (!ctx.valid)
    {
        return;
    }

    const auto valid_weapon = game::rules::is_firearm(ctx.weapon_type);

    if (valid_weapon && cfg.other.penetration_crosshair)
    {
        const auto global_vars = game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().global_vars);
        const auto current_time = global_vars ? game::bound_memory().process.load<float>(global_vars + 0x30) : 0.0f;
        draw_penetration_crosshair(draw_list, eye_pos, view_angles, cfg, current_time);
    }

    const auto draw_aim_visualization = cfg.aimbot.enabled && cfg.aimbot.draw_fov;
    if (!draw_aim_visualization)
    {
        m_fov_alpha.snap(0.0f);
        return;
    }
    m_fov_alpha.set_target(valid_weapon && draw_aim_visualization ? 1.0f : 0.0f);
    m_fov_alpha.update(render::draw::get_delta_time());

    if (m_fov_alpha.value() <= 0.01f)
    {
        return;
    }

    draw_fov_ring(draw_list, eye_pos, view_angles, cfg.aimbot);
}

// Revolver pre-cock: holds the trigger while the hammer is cocking and lets go shortly before it would fire, so the
// trigger bot's own press fires at once.
void aimbot_t::update_revolver_pre_cock(const ballistics_t::context& ctx, const config::combat_profile::triggerbot& cfg)
{
    constexpr std::uint16_t revolver_id{64};
    auto& input = platform::windows::bound_input();
    const auto revolver_available = ctx.valid && ctx.item_def_idx == revolver_id && cfg.enabled &&
                                    cfg.revolver_pre_cock && !ctx.is_reloading && ctx.clip != 0;
    if (m_revolver_pre_cock_down && (!revolver_available || m_revolver_weapon != ctx.weapon))
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    if (!revolver_available || m_trigger_held)
        return;

    m_revolver_weapon = ctx.weapon;
    const auto ready_tick = ctx.postpone_fire_ready_tick;
    const auto ticks_left = ready_tick > 0 ? ready_tick - ctx.player_tick : std::numeric_limits<int>::max();
    const auto ready_fraction = std::isfinite(ctx.postpone_fire_ready_fraction)
                                    ? std::clamp(ctx.postpone_fire_ready_fraction, 0.0f, 1.0f)
                                    : 0.0f;
    const auto milliseconds_left =
        ticks_left == std::numeric_limits<int>::max()
            ? std::numeric_limits<float>::max()
            : (static_cast<float>(ticks_left) + ready_fraction) * game::rules::simulation_step * 1000.0f;
    const auto release_margin = static_cast<float>(std::clamp(cfg.revolver_release_margin_ms, 1, 60));
    if (m_revolver_pre_cock_down && ready_tick > 0 && milliseconds_left <= release_margin)
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    else if (!m_revolver_pre_cock_down && (ready_tick <= 0 || ready_tick <= ctx.player_tick))
    {
        m_revolver_pre_cock_down = input.pointer(0, 0, platform::windows::pointer_action::primary_down);
    }
}

void aimbot_t::cancel_scheduled_trigger()
{
    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_target_valid = false;
    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

void aimbot_t::reset_aim_tracking()
{
    command_steering().clear();
    command_steering().rearm();
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_virtual_angles_valid = false;
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_pawn = 0;
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_velocity_valid = false;
}

// Ends a trigger press once the game consumed the shot (a newer command tick for a seed press, or a higher shot
// count), once the press timed out, or when a seed press lost its weapon context. A seed press is released
// unconditionally; a normal press only when the release went through.
void aimbot_t::release_finished_press(const ballistics_t::context& ctx, const std::chrono::steady_clock::time_point now)
{
    const auto shots_fired = [](const std::uintptr_t pawn)
    {
        return pawn ? game::bound_memory().process.load<std::int32_t>(pawn +
                                                                      SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id))
                    : -1;
    };
    bool command_consumed = false;
    if (ctx.valid && m_seed_press_active && m_seed_pending_target_tick >= 0)
    {
        const auto pawn = game::local_player().pawn();
        const auto command_tick = read_seed_tick();
        const auto shots = shots_fired(pawn);
        command_consumed =
            command_tick > m_seed_pending_target_tick || (m_seed_last_shots >= 0 && shots > m_seed_last_shots);
    }
    else if (!m_seed_press_active && m_trigger_press_shots >= 0)
    {
        command_consumed = shots_fired(game::local_player().pawn()) > m_trigger_press_shots;
    }

    const auto context_lost = !ctx.valid && m_seed_press_active;
    if (!context_lost && !command_consumed && now < m_trigger_release_time)
        return;
    if (command_consumed)
        features::visuals::event_log().mark_latest_trigger_consumed();
    auto& input = platform::windows::bound_input();
    if (m_seed_press_active)
    {
        input.pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_trigger_held = false;
        m_seed_press_active = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
    else if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
    {
        m_trigger_held = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
}

// A shot count past the one recorded for the pending seed press means that press already fired.
void aimbot_t::observe_shots_fired(const std::uintptr_t pawn)
{
    if (!pawn)
        return;
    const auto shots_fired =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
    if (m_seed_last_shots >= 0 && shots_fired > m_seed_last_shots && m_seed_pending_target_tick >= 0)
        m_seed_pending_target_tick = -1;
    m_seed_last_shots = shots_fired;
}

// Remembers the target the FOV indicator follows (drawn by on_render).
void aimbot_t::publish_indicator(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                                 const target& picked)
{
    if (!picked.player)
        return;
    auto point = picked.aim_point;
    if (picked.bones.covers(picked.bone) && picked.bones.is_valid())
    {
        const auto& bone = picked.bones.bones[picked.bone];
        point = bone.position + bone.rotation.apply(picked.aim_offset);
    }
    std::scoped_lock lock(m_indicator_mutex);
    m_indicator_point = point;
    m_indicator_fov = selection_fov(cfg, eye, point);
    m_indicator_time = std::chrono::steady_clock::now();
    m_indicator_pawn = picked.player->pawn;
    m_indicator_bone_cache = picked.player->bone_cache;
    m_indicator_bone = picked.bone;
    m_indicator_offset = picked.aim_offset;

    // The ring is drawn around the angle the shots travel to. While the aim goes into the command rather than
    // to the mouse the camera never follows that angle, so a ring centred on the screen would frame whatever
    // the player is looking at instead of the target. Anchor it on the threat: the head joint itself for a
    // head lock, so the ring sits exactly on the head and not on whichever multipoint the aim chose, and the
    // aim point for any other hitgroup, because a chest lock genuinely shoots at the chest.
    m_indicator_silent_ring = false;
    if (cfg.silent && command_steering().live())
    {
        auto centre = point;
        const auto head_joint = static_cast<int>(game::rules::joint_id::head);
        // The aim path records the chosen hitbox, not a hitgroup, so read the group back off the threat's own
        // hitbox table before deciding whether this is a head lock.
        const auto hitgroup = picked.hitbox >= 0 ? game::hitbox_data().hitgroup_from_hitbox(picked.hitbox)
                                                 : picked.hitgroup;
        if (hitgroup == game::rules::hitgroup::head && picked.bones.covers(head_joint) &&
            picked.bones.is_valid())
            centre = picked.bones.get_position(static_cast<std::uint32_t>(head_joint));
        if (std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z) &&
            centre.length_sqr() > 0.0f)
        {
            m_indicator_ring_center = centre;
            m_indicator_silent_ring = true;
        }
    }
}

// Chooses the aim target, reusing the last choice for the same world sample within 16 ms, and steers towards it.
// Returns false when the aimbot's local checks block this frame.
bool aimbot_t::select_and_aim(const combat_frame& frame, const config::combat_profile::resolved_config& cfg,
                              const bool visual_scan_requested, const bool visual_scan_due)
{
    if (local_checks_blocked(cfg.aimbot.checks))
    {
        // A blocked frame aims nothing, so the request from the last one must not ride out its TTL: a jump or
        // a flash would otherwise fire the next commands along the angle chosen on the ground.
        command_steering().clear();
        m_aim_pawn = 0;
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
        return false;
    }
    constexpr auto reuse_window = std::chrono::milliseconds(16);
    constexpr auto indicator_period = std::chrono::milliseconds(33);
    const auto selection_now = std::chrono::steady_clock::now();
    const auto view_delta = std::hypot(frame.view_angles.x - m_aim_selection_view.x,
                                       foundation::wrap_yaw(frame.view_angles.y - m_aim_selection_view.y));
    const auto can_reuse = m_aim_selection_world == frame.targets && m_aim_selection_weapon == frame.weapon.weapon &&
                           selection_now - m_aim_selection_at < reuse_window &&
                           (m_aim_selection_target.player || view_delta < 0.5f);
    // Silent aim keeps the camera exactly where the player left it, so a reference taken from the view drops
    // the target as soon as it walks out of a crosshair the aim pass no longer moves. Selection and the aim
    // pass then disagree, and the pair cycles acquire and release for every tick of a burst. Judge selection by
    // the angle the shot actually leaves at while a target is already held, and by the camera to acquire the
    // first one, so silent aim still cannot reach outside the view cone to start with.
    const auto max_silent_turn = std::clamp(cfg.aimbot.silent_max_turn, 5.0f, 35.0f);
    auto selection_reference = frame.view_angles;
    if (cfg.aimbot.silent && command_steering().live() && m_aim_pawn != 0 && m_aim_virtual_angles_valid)
    {
        const auto turned = std::hypot(m_aim_virtual_angles.x - frame.view_angles.x,
                                       foundation::wrap_yaw(m_aim_virtual_angles.y - frame.view_angles.y));
        if (std::isfinite(turned) && turned <= max_silent_turn)
            selection_reference = m_aim_virtual_angles;
    }
    auto selected_target =
        can_reuse ? m_aim_selection_target : choose_target(frame.eye, selection_reference, *frame.targets, cfg);
    if (selected_target.player)
    {
        selected_target.fov = get_fov(selection_reference, frame.eye, selected_target.aim_point);
        if (selected_target.fov > selection_fov(cfg.aimbot, frame.eye, selected_target.aim_point))
            selected_target = {};
    }
    if (!can_reuse)
    {
        m_aim_selection_world = frame.targets;
        m_aim_selection_target = selected_target;
        m_aim_selection_view = frame.view_angles;
        m_aim_selection_weapon = frame.weapon.weapon;
        m_aim_selection_at = std::chrono::steady_clock::now();
    }
    if (selected_target.player && visual_scan_requested)
    {
        publish_indicator(cfg.aimbot, frame.eye, selected_target);
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    else if (!selected_target.player && visual_scan_due &&
             cfg.aimbot.fov_config.selection == config::combat_profile::fov_settings::target_distance)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(cfg.aimbot, frame.eye,
                          choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg, false));
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    if (selected_target.player)
        aimbot(frame.eye, frame.view_angles, selected_target, cfg.aimbot);
    else
        // The aim pass is not reached without a target, so the request is withdrawn here rather than left
        // to expire: commands built after the target is gone must carry the player's own angles.
        command_steering().clear();
    return true;
}

void aimbot_t::tick()
{
    std::scoped_lock lock(m_state_mutex);
    tick_state();
}

void aimbot_t::tick_state()
{
    refresh_runtime_config();
    const auto ctx = ballistics().ctx();
    if (ctx.valid)
    {
        // The game time went backwards: a new match or map.
        if (ctx.current_time < m_last_time - 0.5f)
            reset_state();
        m_last_time = ctx.current_time;
    }

    if (!m_rng_seeded)
    {
        m_rng.seed(static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff));
        m_rng_seeded = true;
    }

    const auto now = std::chrono::steady_clock::now();
    auto requested_cfg = runtime_config()->combat.get(game::local_player().weapon_type());
    apply_rage_lock(requested_cfg);
    const auto trigger_activation_active =
        requested_cfg.triggerbot.enabled && config::combat_profile::activation_active(
                                                requested_cfg.triggerbot.activation_mode, requested_cfg.triggerbot.key);

    update_revolver_pre_cock(ctx, requested_cfg.triggerbot);

    // The trigger key was let go during a normal press: release it now.
    if (m_trigger_held && !m_seed_press_active && !trigger_activation_active)
    {
        if (platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up))
        {
            m_trigger_held = false;
            m_trigger_press_shots = -1;
            m_trigger_release_time = {};
            m_revolver_committed = false;
        }
        cancel_scheduled_trigger();
    }
    if (m_trigger_held)
        release_finished_press(ctx, now);

    const auto aimbot_requested =
        requested_cfg.aimbot.enabled &&
        config::combat_profile::activation_active(requested_cfg.aimbot.activation_mode, requested_cfg.aimbot.key);
    if (!aimbot_requested)
    {
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
    }
    // With an independent seed mode the seed thread presses; this trigger bot stays idle.
    const auto independent_seed = requested_cfg.triggerbot.seed_type != config::combat_profile::seed_mode::none;
    const auto trigger_requested = requested_cfg.triggerbot.enabled && !independent_seed &&
                                   config::combat_profile::activation_active(requested_cfg.triggerbot.activation_mode,
                                                                             requested_cfg.triggerbot.key);
    const auto dynamic_fov_visualization_requested =
        requested_cfg.aimbot.enabled && requested_cfg.aimbot.draw_fov &&
        requested_cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;

    // Nothing else refreshes the weapon context while only the FOV ring needs it.
    if (dynamic_fov_visualization_requested && now >= m_next_visual_scan && !aimbot_requested && !trigger_requested)
        simulation::ballistics().tick();

    recoil_control(requested_cfg.aimbot, aimbot_requested && ctx.valid);
    if (!aimbot_requested && !trigger_requested && !m_trigger_held && !dynamic_fov_visualization_requested)
    {
        reset_aim_tracking();
        cancel_scheduled_trigger();
        return;
    }

    // A shot scheduled by the trigger bot fires on time even without a fresh target list.
    if (trigger_requested && m_trigger_shot_scheduled)
    {
        static const std::vector<game::player_snapshot> no_targets{};
        triggerbot({}, {}, no_targets, requested_cfg.triggerbot);
    }

    if (!ctx.valid)
    {
        command_steering().clear();
        return;
    }

    auto cfg = runtime_config()->combat.get(ctx.weapon_type);
    apply_rage_lock(cfg);
    const auto pawn = game::local_player().pawn();
    observe_shots_fired(pawn);
#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
    research::impact_probe_poll(pawn, now);
    research::seed_stats_flush(now);
#endif

    if (!game::rules::is_firearm(ctx.weapon_type))
    {
        // The same withdrawal the no-target case makes, for the case that matters most: the aim key is still
        // held while the player switches to a grenade or the knife, and the throw command is built from here on.
        command_steering().clear();
        return;
    }
    const auto visualization_only =
        dynamic_fov_visualization_requested && !aimbot_requested && !trigger_requested && !m_trigger_held;
    if (visualization_only && now < m_next_visual_scan)
    {
        command_steering().clear();
        return;
    }

    combat_frame frame{};
    frame.sequence = ++m_combat_sequence;
    frame.timestamp = now;
    frame.camera = game::camera().matrix();
    frame.eye = game::camera().origin();
    frame.view_angles = game::camera().angles();
    static_cast<void>(game::camera().sample(frame.eye, frame.view_angles));
    frame.local_pawn = pawn;
    frame.flash_alpha = game::local_player().flash_alpha();
    frame.weapon = ctx;
    frame.targets = game::world().players();
    if (!frame.targets)
    {
        command_steering().clear();
        flush_recoil_input();
        return;
    }

    const auto visual_scan_requested = cfg.aimbot.enabled && cfg.aimbot.draw_fov &&
                                       cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;
    const auto visual_scan_due = visual_scan_requested && now >= m_next_visual_scan;
    const auto aim_can_select = aimbot_requested && cfg.aimbot.enabled &&
                                config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key) &&
                                !m_trigger_held && !ctx.is_reloading && ctx.clip != 0 &&
                                (!visual_scan_due || !local_checks_blocked(cfg.aimbot.checks));
    // The FOV indicator follows the would-be target even while the aimbot is not selecting.
    if (visual_scan_due && !aim_can_select)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(
            cfg.aimbot, frame.eye,
            choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg,
                          cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::target_distance));
        m_next_visual_scan = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
    }
    if (visualization_only)
    {
        command_steering().clear();
        return;
    }
    if (ctx.is_reloading || ctx.clip == 0)
    {
        command_steering().clear();
        cancel_scheduled_trigger();
        flush_recoil_input();
        return;
    }

    if (cfg.triggerbot.enabled && cfg.triggerbot.seed_type == config::combat_profile::seed_mode::none)
        triggerbot(frame.eye, frame.view_angles, *frame.targets, cfg.triggerbot);

    if (cfg.aimbot.enabled && !m_trigger_held)
    {
        if (!config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key))
            reset_aim_tracking();
        else if (!select_and_aim(frame, cfg, visual_scan_requested, visual_scan_due))
        {
            flush_recoil_input();
            return;
        }
    }
    flush_recoil_input();
}


} // namespace features::aimbot
