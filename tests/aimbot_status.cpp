#include <render/overlay/activation_status.hpp>
#include "test_support.hpp"

#include <chrono>
#include <cstdio>

using tracker = render::hud::activation_tracked_state;
using namespace std::chrono_literals;

int main()
{
    const auto start = tracker::clock::time_point{};
    tracker tracked{};

    VESTA_CHECK(!tracked.observed());
    VESTA_CHECK(!tracked.observe(false, start));
    VESTA_CHECK(tracked.observed());
    VESTA_CHECK(!tracked.active());
    VESTA_CHECK(tracked.flash(start, 280.0f) == 0.0f);

    VESTA_CHECK(!tracked.observe(false, start + 10ms));
    VESTA_CHECK(tracked.flash(start + 10ms, 280.0f) == 0.0f);

    VESTA_CHECK(tracked.observe(true, start + 20ms));
    VESTA_CHECK(tracked.active());
    VESTA_CHECK(tracked.flash(start + 20ms, 280.0f) == 1.0f);
    const auto half = tracked.flash(start + 160ms, 280.0f);
    VESTA_CHECK(half > 0.49f && half < 0.51f);
    VESTA_CHECK(tracked.flash(start + 300ms, 280.0f) == 0.0f);
    VESTA_CHECK(tracked.flash(start + 30s, 280.0f) == 0.0f);

    VESTA_CHECK(!tracked.observe(true, start + 40s));
    VESTA_CHECK(tracked.flash(start + 40s, 280.0f) == 0.0f);

    VESTA_CHECK(tracked.observe(false, start + 42s));
    VESTA_CHECK(!tracked.active());
    const auto disabled = tracked.flash(start + 42s + 140ms, 280.0f);
    VESTA_CHECK(disabled > 0.49f && disabled < 0.51f);
    VESTA_CHECK(tracked.flash(start + 42s - 5ms, 280.0f) == 0.0f);
    VESTA_CHECK(tracked.flash(start + 42s, 0.0f) == 0.0f);

    VESTA_CHECK(!tracked.observe(false, start + 60s));
    VESTA_CHECK(tracked.observe(true, start + 61s));
    VESTA_CHECK(tracked.observe(false, start + 62s));
    VESTA_CHECK(tracked.flash(start + 62s + 279ms, 280.0f) > 0.0f);
    VESTA_CHECK(tracked.flash(start + 62s + 280ms, 280.0f) == 0.0f);

    std::puts("PASS activation status flashes once per edge and decays inside its window");
    return 0;
}
