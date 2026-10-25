#pragma once

#include <hvh_shared/hvh_shared.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace features::hvh
{
enum class inject_status
{
    not_injected,
    injected,
    failed,
};

// Owns the cross-process shared block and the loader/ejector for the injected vesta_hvh.dll. `settings` is the
// live, externally-editable mirror of the mapping's config half.
//
// The block has two writers and each half has exactly one of them. The config and signature halves belong to
// this thread of callers: whoever edits the menu, plus the worker that keeps the module's view of them fresh.
// The aim half belongs to the combat worker alone, because the angle it carries is chosen per command and has
// to reach the module in the same instant it was chosen — batching it behind a status poll would have the
// module steer an angle the target left three commands ago.
class controller_t
{
public:
    void initialize();
    // Writes the config and signature halves. Cheap and idempotent, so a caller that just changed a setting
    // can call it directly instead of waiting for the worker.
    void publish();
    // Called on the HvH worker: republish anything outstanding and refresh what the module is reporting.
    void tick();
    void shutdown();

    bool inject();
    bool eject();

    [[nodiscard]] inject_status status() const;
    [[nodiscard]] bool dll_active() const;
    [[nodiscard]] bool signature_found() const;
    [[nodiscard]] bool hook_ready() const;
    [[nodiscard]] int hook_calls() const;
    [[nodiscard]] int targets_found() const;
    [[nodiscard]] std::string_view last_error() const;
    [[nodiscard]] const std::filesystem::path& dll_path() const;

    // The silent-aim seam. The external command steering owns the decision of whether a shot goes silent, and
    // the module is a second transport for the same approved angle: publishing here mirrors it into the block
    // for the CreateMove hook to stamp, and clearing withdraws it. `aim_mirror_armed` is the combination the
    // aim path asks about — a live module that was told to take silent shots — so a passive or absent module
    // never makes the aim pass believe it steered something it did not.
    void publish_aim(float pitch, float yaw) noexcept;
    void clear_aim() noexcept;
    [[nodiscard]] bool aim_mirror_armed() const noexcept
    {
        return m_aim_mirror.load(std::memory_order_acquire);
    }

    void load();
    void save();

    vesta::hvh_shared::settings settings{};
    vesta::hvh_shared::signatures signatures{};

private:
    bool ensure_mapping();
    void release_mapping();
    void refresh_paths();
    void mark_changed();
    // Both halves of the writer contract in one place: the config side under the lock, the aim side without it.
    void publish_locked();
    void refresh_status();

    void* m_mapping{nullptr};
    // The aim path reads this from the combat worker while the cold paths establish and drop it, so the pointer
    // itself is the handoff: it is published before the mirror is armed and cleared before the view is unmapped.
    std::atomic<vesta::hvh_shared::shared_state*> m_view{nullptr};
    std::filesystem::path m_directory{};
    std::filesystem::path m_config_directory{};
    std::filesystem::path m_dll_path{};
    std::string m_last_error{};
    inject_status m_status{inject_status::not_injected};
    // Read on the fast paths before the lock is taken, so it has to be an atomic; the value it guards is still
    // established under the mutex.
    std::atomic<bool> m_initialized{false};
    // Cold-path state: menu edits, injection, the worker's poll and shutdown can reach here from different
    // threads. The aim path below stays out of this lock on purpose; it only ever touches the atomics and the
    // already-established view.
    std::mutex m_state_mutex{};
    std::atomic<bool> m_changed{false};
    std::atomic<bool> m_aim_mirror{false};
};

[[nodiscard]] controller_t& controller();

} // namespace features::hvh
