#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <foundation/color.hpp>
#include <config/game_controls.hpp>
#include <platform/windows/activation.hpp>

namespace config
{

struct general_profile
{
    enum class screen_anchor : std::uint8_t
    {
        top_left,
        top_right,
        bottom_left,
        bottom_right
    };

    struct screen_layout
    {
        constexpr screen_layout(const float x = 40.0f, const float y = 40.0f)
            : offset_x(x)
            , offset_y(y)
            , legacy_position_x(x)
            , legacy_position_y(y)
        {
        }

        screen_anchor anchor{screen_anchor::top_left};
        float offset_x{40.0f};
        float offset_y{40.0f};
        float scale{1.0f};
        int version{1};

        // Read only while migrating configs that stored absolute position_x/y.
        float legacy_position_x{40.0f};
        float legacy_position_y{40.0f};
    };

    struct bullet_tracers
    {
        bool enabled{false};
        foundation::rgba color{192, 203, 229, 200};
        float duration{2.0f};
        float thickness{1.5f};
        bool bloom{true};
        int max_count{40};
        bool draw_cubes{true};
        bool draw_line{true};
        float cube_half{1.25f};
        foundation::rgba cube_edge_color{192, 203, 229, 230};
        float cube_face_alpha{38.0f};
        float fade_near{45.0f};
        float fade_far{160.0f};
    } m_bullet_tracers{};

    struct hitmarker
    {
        bool enabled{true};
        foundation::rgba color{245, 247, 250, 255};
        float size{8.0f};
        float gap{3.0f};
        float thickness{1.5f};
        float duration{0.55f};
    } m_hitmarker{};

    struct hitsound
    {
        bool enabled{true};
        int style{4}; // 0 soft, 1 glass, 2 pluck, 3 crisp, 4 flesh
        float volume{0.55f};
        std::array<char, 256> device{};
        [[nodiscard]] std::string device_id() const { return std::string(device.data()); }
        void assign_device(const std::string& id)
        {
            const auto length = std::min<std::size_t>(id.size(), device.size() - 1);
            std::copy_n(id.data(), static_cast<std::ptrdiff_t>(length), device.begin());
            device[length] = '\0';
        }
        bool kill_enabled{true};
        int kill_style{3}; // 0 soft, 1 glass, 2 pluck, 3 crisp, 4 flesh
        float kill_volume{0.8f};
        bool show_damage{true};
        foundation::rgba damage_color{255, 108, 112, 255};
        float damage_size{20.0f};
        float damage_duration{0.75f};
        float damage_rise{30.0f};
    } m_hitsound{};

    struct grenades
    {
        bool enabled{true};
        bool local_only{false};
        foundation::rgba color{255, 255, 255, 200};
        float thickness{2.0f};
        bool bloom{true};
        foundation::rgba bloom_color{120, 170, 255, 58};
        float bloom_radius{3.0f};
        bool show_bounces{true};
        foundation::rgba bounce_color{120, 170, 255, 235};
        float bounce_size{4.0f};
        bool show_endpoint{true};
        foundation::rgba endpoint_color{255, 255, 255, 235};
        float endpoint_size{8.0f};
    } m_grenades{};

    struct nade_helper
    {
        bool enabled{false};

        // 800 was narrower than most site-to-throw distances on the larger maps, so a fresh profile
        // looked like a helper with almost no lineups on it.
        float draw_distance{1400.0f};
        float stand_distance{220.0f};
        float stand_radius{22.0f};
        // Broad radius keeps the marker easy to acquire; automatic execution uses
        // the tighter horizontal/vertical tolerances below.
        float release_radius{6.0f};
        float height_tolerance{8.0f};

        bool show_action{true};
        bool show_distance{true};
        // Standing on one spot hides every other plaque by default. Some players use the helper to
        // decide where to go next, and want the rest of the map to stay visible.
        bool show_other_spots{};

        foundation::rgba plaque_background{12, 13, 18, 205};
        foundation::rgba plaque_text{245, 245, 250, 255};
        foundation::rgba plaque_accent{120, 170, 255, 255};
        foundation::rgba stand_marker{120, 170, 255, 235};
        foundation::rgba stand_marker_active{110, 235, 140, 245};
        foundation::rgba aim_marker{245, 245, 250, 235};

        bool aim_assist{true};
        bool auto_release{true};
        int aim_key{VK_XBUTTON2};
        int aim_smoothing{18};
        // Angular error (degrees) at which the aim counts as converged and the
        // marker switches from "aim here" to "throw now".
        float aim_threshold{0.35f};
        int lock_time_ms{45};
    } m_nade_helper{};

    struct watermark
    {
        bool enabled{true};
        screen_layout layout{40.0f, 40.0f};
        bool vertical{};
        bool show_ping{true};
        bool show_loss{true};
        bool show_cpu{true};
        bool show_fps{true};
    } m_watermark{};

    // Movable panel (like the watermark) listing the players currently
    // spectating the local player, with their observer mode.
    struct spectator_list
    {
        bool enabled{false};
        bool show_avatars{true};
        screen_layout layout{40.0f, 320.0f};
    } m_spectator_list{};

    struct event_log
    {
        bool enabled{false};
        screen_layout layout{40.0f, 410.0f};
        float duration{5.0f};
        int max_entries{5};
        bool show_shots{true};
        bool show_hits{true};
        bool show_kills{true};
        bool show_misses{true};
        bool show_blocked{true};
        bool show_info{true};
    } m_event_log{};

    struct keybind_list
    {
        bool enabled{false};
        screen_layout layout{40.0f, 260.0f};
        bool show_always{true};
        bool show_hold{true};
        bool show_toggle{true};
    } m_keybind_list{};

    struct aimbot_status
    {
        bool enabled{true};
        screen_layout layout{40.0f, 150.0f};
        bool show_mode{true};
    } m_aimbot_status{};

    struct flash_status
    {
        bool enabled{true};
        screen_layout layout{40.0f, 210.0f};
        bool show_local{true};
        bool show_enemies{true};
        int max_enemies{3};
    } m_flash_status{};

    struct bomb_info
    {
        screen_layout layout{40.0f, 200.0f};
    } m_bomb_info{};

    struct plant_alert
    {
        screen_layout layout{900.0f, 168.0f};
    } m_plant_alert{};

    struct bunny_hop
    {
        bool enabled{false};
        int activation_key{VK_SPACE};
    } m_bunny_hop{};

    struct edge_jump
    {
        bool enabled{false};
        int activation_key{VK_XBUTTON1};
    } m_edge_jump{};

    // Speed Hack: while active, the local pawn's m_flMaxspeed is scaled by `multiplier`. The engine
    // recomputes m_flMaxspeed every tick (walk / duck / weapon / air), so the tick reads the fresh
    // game value and rewrites it; when activation ends, the multiplier is no longer applied and the
    // next tick's engine value takes over unchanged.
    //
    // Activation matches the same tri-state used by the visual profiles: always on, hold the key, or
    // toggle with each press. The key slot is ignored in always-on mode.
    struct speed_hack
    {
        enum activation_type : int
        {
            always_on = 0,
            hold = 1,
            toggle = 2
        };
        bool enabled{false};
        int activation_mode{hold};
        int activation_key{VK_SHIFT};
        float multiplier{1.2f};
        [[nodiscard]] bool active() const noexcept
        {
            return enabled &&
                   platform::windows::binding_active(activation_mode, activation_key, always_on, hold, toggle);
        }
    } m_speed_hack{};

    game_controls m_game_controls{};

    struct auto_stop
    {
        bool enabled{false};
        bool physical_fire{true};
        bool predictive_trigger{true};
        float stop_speed{8.0f};
        float required_shoot_speed{34.0f};
    } m_auto_stop{};

    // Automation that presses the player's own bound keys, so it needs no key of its own and stops the moment the
    // menu is open, a text field has focus, or the user presses the same control himself.
    struct automation
    {
        struct auto_pistol
        {
            bool enabled{false};
            // One press is held this long; semi-automatic pistols fire on the press edge, so a short hold is enough.
            float hold_ms{4.0f};
            // Delay between the release and the next press.
            float interval_ms{60.0f};
        } auto_pistol{};

        struct anti_afk
        {
            bool enabled{false};
            float hold_ms{18.0f};
            // Idle time without any movement input before one tap is sent.
            float idle_seconds{45.0f};
        } anti_afk{};
    } m_automation{};

    // Spawn alert: the local player going from dead to alive is the only round-start edge the game exposes
    // without a round-phase field, so it is what tells the alert to fire.
    struct round_alert
    {
        bool enabled{false};
        int style{2}; // 0 soft, 1 glass, 2 pluck, 3 crisp, 4 flesh
        float volume{0.6f};
        bool when_unfocused{true};
    } m_round_alert{};

    struct keystrokes
    {
        bool enabled{false};
        screen_layout layout{1230.0f, 40.0f};
        float key_size{34.0f};
        bool show_mouse{true};
    } m_keystrokes{};

    // Interface language: 0 English, 1 Russian (render::localization::id).
    static constexpr int k_language_count = 2;
    int language{0};
    float menu_scale{1.0f};
    struct interface_palette
    {
        foundation::rgba background{13, 13, 18, 217};
        foundation::rgba panel{20, 20, 26, 179};
        foundation::rgba card{28, 28, 36, 153};
        foundation::rgba popup{13, 13, 18, 199};
        foundation::rgba accent{124, 58, 237, 255};
        foundation::rgba text{248, 248, 242, 255};
        foundation::rgba muted_text{161, 161, 170, 255};
        foundation::rgba border{255, 255, 255, 15};
        foundation::rgba hover{255, 255, 255, 20};
    } palette{};

    bool auto_accept{true};
    bool obs_bypass{false};
    // Lua runtime is local-only. The production binary exposes no network API;
    // scripts may explicitly launch an external helper through the OS library.
    bool limit_fps{true};
    int fps_limit{240};
    bool use_gpu{true};
};

} // namespace config
