#pragma once

#include <chrono>
#include <cmath>

namespace ui::hud
{
class activation_tracked_state
{
public:
    using clock = std::chrono::steady_clock;

    bool observe(const bool next, const clock::time_point now)
    {
        if (!m_observed)
        {
            m_observed = true;
            m_active = next;
            return false;
        }
        if (m_active == next)
            return false;
        m_active = next;
        m_changed_at = now;
        m_has_edge = true;
        return true;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return m_active;
    }

    [[nodiscard]] bool observed() const noexcept
    {
        return m_observed;
    }

    [[nodiscard]] float flash(const clock::time_point now, const float window_ms) const
    {
        if (!m_has_edge || !(window_ms > 0.0f))
            return 0.0f;
        const auto age = std::chrono::duration<float, std::milli>(now - m_changed_at).count();
        if (!std::isfinite(age) || age < 0.0f || age >= window_ms)
            return 0.0f;
        return 1.0f - age / window_ms;
    }

private:
    bool m_observed{};
    bool m_active{};
    bool m_has_edge{};
    clock::time_point m_changed_at{};
};

} // namespace ui::hud
