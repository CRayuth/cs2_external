#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <game/state/constants.hpp>
#include <game/state/pose.hpp>
#include <simulation/collision.hpp>
#include <features/visuals/visuals.hpp>
#include <chrono>
#include <cmath>

namespace features::visuals
{

namespace
{
// Seconds since the first frame. Only the distance between two samples matters, so a monotonic clock is enough.
[[nodiscard]] double frame_seconds()
{
    static const auto epoch = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
}

// Rotates the hue of `base` around the clock while keeping its saturation and brightness, so a saturated colour
// stays saturated and a pastel stays pastel.
[[nodiscard]] foundation::rgba hue_rotated(const foundation::rgba base, const double seconds, const float speed)
{
    const auto high = std::max({base.r, base.g, base.b});
    const auto low = std::min({base.r, base.g, base.b});
    if (high == 0 || base.a == 0)
        return base;

    const auto value = static_cast<float>(high) / 255.0f;
    const auto saturation = static_cast<float>(high - low) / static_cast<float>(high);

    auto hue = static_cast<float>(std::fmod(seconds * static_cast<double>(speed), 1.0));
    if (hue < 0.0f)
        hue += 1.0f;

    const auto sector = hue * 6.0f;
    const auto index = std::min(5, static_cast<int>(sector));
    const auto fraction = sector - std::floor(sector);
    const auto p = value * (1.0f - saturation);
    const auto q = value * (1.0f - saturation * fraction);
    const auto t = value * (1.0f - saturation * (1.0f - fraction));

    float red{}, green{}, blue{};
    switch (index)
    {
        case 0: red = value, green = t, blue = p; break;
        case 1: red = q, green = value, blue = p; break;
        case 2: red = p, green = value, blue = t; break;
        case 3: red = p, green = q, blue = value; break;
        case 4: red = t, green = p, blue = value; break;
        default: red = value, green = p, blue = q; break;
    }

    const auto channel = [](const float component)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
    };
    return foundation::rgba{channel(red), channel(green), channel(blue), base.a};
}
} // namespace

void crosshair_t::on_render(render::draw::draw_list& draw_list)
{
    auto cfg = config::visual_settings.m_crosshair;
    if (!cfg.enabled)
    {
        return;
    }
    const auto [sw, sh] = render::draw::get_display_size();
    if (sw <= 0 || sh <= 0)
        return;

    const auto pawn = game::local_player().pawn();
    const auto scoped = pawn != 0 &&
                        game::bound_memory().process.load<bool>(
                            pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_id));
    if (scoped && cfg.hide_when_scoped)
        return;

    auto crosshair_color = cfg.color;
    bool penetration_override{};
    if (cfg.penetration_enabled)
    {
        const auto& ctx = simulation::ballistics().ctx();
        if (ctx.valid && game::rules::is_firearm(ctx.weapon_type))
        {
            penetration_override = true;
            const auto eye_pos = game::camera().origin();
            const auto view_angles = game::camera().angles();
            foundation::vec3 forward{};
            view_angles.to_directions(&forward, nullptr, nullptr);

            const auto range = simulation::ballistics().pen().get_weapon_data().range;
            const auto first_hit = simulation::collision().trace_ray(eye_pos, eye_pos + forward * range);
            if (first_hit.hit)
            {
                // A wall is in the way: green only if it can be shot through for
                // enough damage, red otherwise.
                auto pen_damage{0.0f};
                const auto can_pen = simulation::ballistics().pen().can(eye_pos, forward, pen_damage);
                crosshair_color = (can_pen && pen_damage >= cfg.penetration_min_damage) ? cfg.penetration_color_yes
                                                                                        : cfg.penetration_color_no;
            }
            else
            {
                // Clear line of sight - a normal shot lands, so show the "yes" colour.
                crosshair_color = cfg.penetration_color_yes;
            }
        }
    }

    const auto center_x = static_cast<float>(sw) * 0.5f;
    const auto center_y = static_cast<float>(sh) * 0.5f;

    if (cfg.rgb && !penetration_override)
        crosshair_color = hue_rotated(cfg.color, frame_seconds(), cfg.rgb_speed);

    // Through the scope the game hides its own crosshair and draws one thin line; mirror that instead of the arms.
    if (scoped && cfg.sniper_mode)
    {
        const auto width = std::max(1.0f, std::round(cfg.sniper_width));
        const auto height = std::round(static_cast<float>(sh) * 0.45f);
        draw_list.add_rect_filled(center_x - width * 0.5f, center_y - height, width, height * 2.0f, crosshair_color);
        return;
    }

    const auto bar = std::max(1.0f, std::round(cfg.thickness));
    const auto center_lo_x = center_x - bar * 0.5f;
    const auto center_hi_x = center_lo_x + bar;
    const auto center_lo_y = center_y - bar * 0.5f;
    const auto center_hi_y = center_lo_y + bar;

    auto gap_offset = 0.0f;
    if (cfg.dynamic && pawn != 0)
    {
        const auto velocity = game::bound_memory().process.load<foundation::vec3>(
            pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
        if (std::isfinite(velocity.x) && std::isfinite(velocity.y))
        {
            const auto speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
            const auto ratio = std::clamp(speed / std::max(cfg.dynamic_speed, 1.0f), 0.0f, 1.0f);
            gap_offset = std::round(cfg.dynamic_gap * ratio);
        }
    }

    const auto gap = std::clamp(std::round(cfg.gap) + gap_offset, -128.0f, 256.0f);
    const auto length = std::max(0.0f, std::round(cfg.length));
    const auto outline =
        (cfg.outline && cfg.outline_color.a > 0) ? std::max(1.0f, std::round(cfg.outline_thickness)) : 0.0f;

    struct rect
    {
        float x0, y0, x1, y1;
    };
    std::array<rect, 5> pieces{};
    std::size_t count{};

    const auto push = [&](float x0, float y0, float x1, float y1)
    {
        if (x1 > x0 && y1 > y0 && count < pieces.size())
        {
            pieces[count++] = {x0, y0, x1, y1};
        }
    };

    if (cfg.lines && length > 0.0f)
    {
        // Arms are measured outward from the centre bar's own edges, so the
        // pattern stays symmetric about the centre pixel at any thickness.
        push(center_lo_x - gap - length, center_lo_y, center_lo_x - gap, center_hi_y);
        push(center_hi_x + gap, center_lo_y, center_hi_x + gap + length, center_hi_y);

        push(center_lo_x, center_hi_y + gap, center_hi_x, center_hi_y + gap + length);
        if (!cfg.t_style)
        {
            push(center_lo_x, center_lo_y - gap - length, center_hi_x, center_lo_y - gap);
        }
    }

    if (cfg.dot)
    {
        push(center_lo_x, center_lo_y, center_hi_x, center_hi_y);
    }

    if (outline > 0.0f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& p = pieces[i];
            draw_list.add_rect_filled(p.x0 - outline, p.y0 - outline, (p.x1 - p.x0) + outline * 2.0f,
                                      (p.y1 - p.y0) + outline * 2.0f, cfg.outline_color);
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& p = pieces[i];
        draw_list.add_rect_filled(p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0, crosshair_color);
    }
}

} // namespace features::visuals
