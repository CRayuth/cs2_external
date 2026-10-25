#pragma once

#include <foundation/math/vector.hpp>
#include <features/aimbot/command_ring.hpp>
#include <diagnostics/rate_gate.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace features::aimbot
{
// External silent aim. The aim pass publishes the view angle the next user command should carry, and this
// service writes it into the newest command of the client's command ring. The mouse is never touched, so the
// player's camera stays exactly where they left it while the shot leaves along the aim direction.
//
// Only one command is steered, and only once: a request that is not refreshed goes stale on its own instead
// of stamping a frozen angle onto every command until the aim pass happens to clear it.
class command_steering_t
{
public:
    // A command arrives roughly every 15.6 ms; two ticks of aim without a refresh means the target is gone.
    static constexpr auto k_request_ttl = std::chrono::milliseconds(40);
    // Two writes per command period at most: the command being built gets the freshest angle without the
    // worker hammering the game process between commands.
    static constexpr auto k_min_write_interval = std::chrono::milliseconds(8);
    // Consecutive failed writes before the caller is told to put the aim back on the mouse.
    static constexpr std::uint32_t k_unhealthy_writes{4};

    // Called by the aim pass on the combat worker for a shot it approved. Validated here, never trusted.
    void publish(const foundation::vec3& angles) noexcept;
    void clear() noexcept;

    // The angle currently held for steering, when one is present and not expired.
    [[nodiscard]] std::optional<foundation::vec3> peek() const noexcept;

    // Called after the aim pass on the same worker: steers the newest command when it is newer than the last
    // one steered and a fresh request is held.
    void tick();

    [[nodiscard]] bool available() const noexcept;

    // Whether the steered angle actually reaches the game. Silent aim is only a transport, and a transport
    // that never delivers is worse than the mouse it replaced: the aim pass would move nothing and still
    // believe it aimed, and the trigger bot would judge its hitchance by an angle the command never got.
    [[nodiscard]] bool healthy() const noexcept
    {
        return available() && m_write_failures.load(std::memory_order_acquire) < k_unhealthy_writes;
    }

    // Forget the failure count so a new acquisition tries silent aim again. Called when the aim pass goes
    // idle, never from clear(): clear() runs on every mouse-aimed pass, and resetting the count there would
    // make the transport flap back to a broken ring once a second.
    void rearm() noexcept;

    [[nodiscard]] bool armed() const noexcept;

    // Whether a silent shot can be carried at all this pass. The command ring is the transport this service
    // owns, and the injected HvH module is a second one for exactly the same approved angle, so a broken ring
    // does not have to put the shot back on the mouse while the module is still taking every command. Aiming
    // falls back to the mouse only when neither can deliver: a transport that suppresses the move and lands
    // nothing is worse than the move it replaced.
    [[nodiscard]] bool live() const noexcept;

    [[nodiscard]] std::uint64_t steered() const noexcept
    {
        return m_steered.load(std::memory_order_relaxed);
    }
    // Writes that could not be resolved or did not read back; a growing count means the ring moved under us.
    [[nodiscard]] std::uint64_t failed() const noexcept
    {
        return m_failed.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] std::int64_t now_ns() const noexcept;
    [[nodiscard]] bool take_stamped(std::uint64_t& out_stamp, foundation::vec3& out_angles) const noexcept;
    // One journal line per second while a request is held, so the writes can be confirmed from disk: the
    // overlay is deliberately invisible to screen capture whenever OBS bypass is on.
    void report(const std::chrono::steady_clock::time_point now, bool writable, bool written) noexcept;

    // pitch and yaw packed into one 64-bit store so the pair can never be observed half-updated; publishing
    // the two floats separately is how a silent write ends up aiming between two different targets.
    std::atomic<std::uint64_t> m_angles{};
    // Steady clock nanoseconds of the last publish, zero when nothing is held. Doubles as the validity flag
    // and as the stamp that tells a refreshed request from a repeat of the same one.
    std::atomic<std::int64_t> m_published_at{};

    command_ring_reader m_commands{}; // this service owns its ring search state
    // tick() runs on the combat worker while clear() also reaches here from the overlay's exit key on the
    // render thread, so the members below are shared after all and are guarded by this mutex. publish(),
    // healthy() and take_stamped() stay lock-free: they touch only the atomics.
    std::mutex m_state_mutex{};
    std::uint64_t m_last_stamp{};
    std::chrono::steady_clock::time_point m_last_write_at{};
    std::atomic<std::uint32_t> m_write_failures{};
    std::uint64_t m_reported_steered{};
    std::uint64_t m_reported_failed{};
    diagnostics::rate_gate m_note{std::chrono::milliseconds(1000)};
    std::atomic<std::uint64_t> m_steered{};
    std::atomic<std::uint64_t> m_failed{};
    bool m_reported_readonly{};
    bool m_reported_unavailable{};
};

inline command_steering_t& command_steering()
{
    static command_steering_t value{};
    return value;
}

} // namespace features::aimbot
