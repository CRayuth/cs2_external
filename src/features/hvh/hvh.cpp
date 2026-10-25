#include <stdafx.hpp>
#include <features/hvh/hvh.hpp>

#include <app/context.hpp>
#include <diagnostics/journal.hpp>
#include <platform/windows/storage.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>

namespace features::hvh
{
namespace
{
constexpr wchar_t k_dll_name[]{L"vesta_hvh.dll"};
constexpr wchar_t k_config_name[]{L"hvh.json"};
// The file is ours, but it sits in a folder the user can edit, so it gets the same treatment as a config
// preset: bounded size, no exceptions out of the parser, and every value clamped before it crosses into the
// game process.
constexpr std::uintmax_t k_max_config_bytes{64 * 1024};

constexpr auto k_remote_load_timeout{5'000u};
constexpr auto k_status_poll_timeout{3'000u};
constexpr auto k_heartbeat_stale_ms{2'000u};

using settings_field = std::int32_t vesta::hvh_shared::settings::*;

// One table describes the whole config half: what the file may hold, what the module may be given, and what
// the ranges mean. A field added to the contract without a row here simply never persists, which is the
// failure mode a silent 40-line switch would hide.
struct field_bound
{
    const char* key;
    settings_field field;
    std::int32_t minimum;
    std::int32_t maximum;
};

constexpr bool is_flag(const std::int32_t minimum, const std::int32_t maximum)
{
    return minimum == 0 && maximum == 1;
}

// clang-format off
constexpr std::array<field_bound, 41> k_fields{{
    {"enable_rage",        &vesta::hvh_shared::settings::enable_rage,        0, 1},
    {"rage_aim_mode",      &vesta::hvh_shared::settings::rage_aim_mode,      0, 2},
    {"rage_hitbox",        &vesta::hvh_shared::settings::rage_hitbox,        0, 4},
    {"rage_priority",      &vesta::hvh_shared::settings::rage_priority,      0, 2},
    {"rage_fov",           &vesta::hvh_shared::settings::rage_fov,           1, 180},
    {"rage_smoothing",     &vesta::hvh_shared::settings::rage_smoothing,     1, 100},
    {"rage_autofire",      &vesta::hvh_shared::settings::rage_autofire,      0, 1},
    {"rage_autowall",      &vesta::hvh_shared::settings::rage_autowall,      0, 1},
    {"rage_autoscope",     &vesta::hvh_shared::settings::rage_autoscope,     0, 1},
    {"rage_silent",        &vesta::hvh_shared::settings::rage_silent,        0, 1},
    {"rage_min_damage",    &vesta::hvh_shared::settings::rage_min_damage,    1, 100},
    {"rage_visible_only",  &vesta::hvh_shared::settings::rage_visible_only,  0, 1},
    {"rage_target_key",    &vesta::hvh_shared::settings::rage_target_key,    0, 255},
    {"enable_silent",      &vesta::hvh_shared::settings::enable_silent,      0, 1},
    {"silent_hitbox",      &vesta::hvh_shared::settings::silent_hitbox,      0, 4},
    {"silent_priority",    &vesta::hvh_shared::settings::silent_priority,    0, 2},
    {"silent_fov",         &vesta::hvh_shared::settings::silent_fov,         1, 180},
    {"silent_autofire",    &vesta::hvh_shared::settings::silent_autofire,    0, 1},
    {"silent_psilent",     &vesta::hvh_shared::settings::silent_psilent,     0, 1},
    {"silent_min_damage",  &vesta::hvh_shared::settings::silent_min_damage,  1, 100},
    {"enable_nospread",    &vesta::hvh_shared::settings::enable_nospread,    0, 1},
    {"enable_norecoil",    &vesta::hvh_shared::settings::enable_norecoil,    0, 1},
    {"enable_thirdperson", &vesta::hvh_shared::settings::enable_thirdperson, 0, 1},
    {"enable_trigger",     &vesta::hvh_shared::settings::enable_trigger,     0, 1},
    {"trigger_delay_ms",   &vesta::hvh_shared::settings::trigger_delay_ms,   0, 500},
    {"trigger_hitchance",  &vesta::hvh_shared::settings::trigger_hitchance,  0, 100},
    {"trigger_key",        &vesta::hvh_shared::settings::trigger_key,        0, 255},
    {"enable_antiaim",     &vesta::hvh_shared::settings::enable_antiaim,     0, 1},
    {"aa_pitch",           &vesta::hvh_shared::settings::aa_pitch,           0, 4},
    {"aa_yaw_base",        &vesta::hvh_shared::settings::aa_yaw_base,        0, 1},
    {"aa_yaw_mode",        &vesta::hvh_shared::settings::aa_yaw_mode,        0, 3},
    {"aa_yaw_offset",      &vesta::hvh_shared::settings::aa_yaw_offset,      -180, 180},
    {"aa_spin_speed",      &vesta::hvh_shared::settings::aa_spin_speed,      1, 180},
    {"aa_jitter_min",      &vesta::hvh_shared::settings::aa_jitter_min,      -180, 180},
    {"aa_jitter_max",      &vesta::hvh_shared::settings::aa_jitter_max,      -180, 180},
    {"aa_jitter_speed",    &vesta::hvh_shared::settings::aa_jitter_speed,    1, 64},
    {"aa_fake_lag",        &vesta::hvh_shared::settings::aa_fake_lag,        0, 1},
    {"aa_desync",          &vesta::hvh_shared::settings::aa_desync,          0, 58},
    {"aa_lby_mode",        &vesta::hvh_shared::settings::aa_lby_mode,        0, 3},
    {"enable_bhop",        &vesta::hvh_shared::settings::enable_bhop,        0, 1},
    {"enable_auto_stop",   &vesta::hvh_shared::settings::enable_auto_stop,   0, 1},
}};
// clang-format on

static_assert(k_fields.size() == 41, "every field of the config half needs a row in the table");

// Copies a signature pattern into the fixed-width field the module reads, without ever writing past it. A
// pattern longer than the field is rejected rather than truncated: a half-copied byte pattern would scan for
// something nobody wrote, and land on a random function.
void store_signature(char (&destination)[vesta::hvh_shared::k_signature_length], std::string_view value)
{
    if (value.size() >= vesta::hvh_shared::k_signature_length)
    {
        value = {};
    }
    std::copy_n(value.begin(), static_cast<std::ptrdiff_t>(value.size()), destination);
    destination[value.size()] = '\0';
}

[[nodiscard]] std::filesystem::path config_file(const std::filesystem::path& directory)
{
    return directory / k_config_name;
}
} // namespace

void controller_t::refresh_paths()
{
    wchar_t buffer[MAX_PATH]{};
    const auto length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        m_directory = std::filesystem::current_path();
    }
    else
    {
        m_directory = std::filesystem::path(std::wstring(buffer, length)).parent_path();
    }
    m_dll_path = m_directory / k_dll_name;
    // The payload belongs beside the executable because that is where the packaging puts it. Its settings do
    // not: they live with every other configuration this product writes, in the data folder.
    m_config_directory = platform::windows::runtime_storage::paths().configs;
}

bool controller_t::ensure_mapping()
{
    if (m_view.load(std::memory_order_acquire))
    {
        return true;
    }

    const auto bytes = static_cast<DWORD>(sizeof(vesta::hvh_shared::shared_state));
    m_mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes,
                                     vesta::hvh_shared::k_mapping_name);
    if (!m_mapping)
    {
        m_last_error = "CreateFileMapping failed for the HvH shared block.";
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::win32, static_cast<std::uint32_t>(::GetLastError()),
                          diagnostics::subject::hvh);
        return false;
    }

    const bool fresh = ::GetLastError() != ERROR_ALREADY_EXISTS;
    void* view = ::MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (!view)
    {
        m_last_error = "MapViewOfFile failed for the HvH shared block.";
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::win32, static_cast<std::uint32_t>(::GetLastError()),
                          diagnostics::subject::hvh);
        ::CloseHandle(m_mapping);
        m_mapping = nullptr;
        return false;
    }

    auto* block = static_cast<vesta::hvh_shared::shared_state*>(view);
    if (fresh || !vesta::hvh_shared::valid(*block))
    {
        *block = vesta::hvh_shared::shared_state{};
    }
    block->config = settings;
    block->sigs = signatures;
    block->aim = {};
    block->state.unload_request = 0;
    // Published before the mirror can arm, so the aim path never writes through a pointer that is not mapped.
    m_view.store(block, std::memory_order_release);
    return true;
}

void controller_t::release_mapping()
{
    // Disarm first: the combat worker checks this before it touches the view, so once it is clear no new aim
    // write starts, and the ones already in flight finished before this call ran.
    m_aim_mirror.store(false, std::memory_order_release);
    if (const auto* view = m_view.exchange(nullptr, std::memory_order_acq_rel))
    {
        ::UnmapViewOfFile(view);
    }
    if (m_mapping)
    {
        ::CloseHandle(m_mapping);
        m_mapping = nullptr;
    }
}

void controller_t::initialize()
{
    std::scoped_lock lock(m_state_mutex);
    if (m_initialized.load(std::memory_order_acquire))
    {
        return;
    }
    refresh_paths();
    load();
    ensure_mapping();
    m_initialized.store(true, std::memory_order_release);
}

void controller_t::publish_locked()
{
    auto* view = m_view.load(std::memory_order_acquire);
    if (!view)
    {
        return;
    }
    // Only the config and signature halves are written so the module's status half is preserved, and the aim
    // half is left to its own writer rather than resurrecting an angle the combat worker already withdrew.
    view->config = settings;
    view->sigs = signatures;
}

void controller_t::publish()
{
    // Established outside the lock: initialize() takes it, and this must not re-enter it while held.
    if (!m_initialized.load(std::memory_order_acquire))
    {
        initialize();
    }
    std::scoped_lock lock(m_state_mutex);
    publish_locked();
    m_changed.store(false, std::memory_order_relaxed);
}

void controller_t::tick()
{
    if (!m_initialized.load(std::memory_order_acquire))
    {
        initialize();
    }

    {
        std::scoped_lock lock(m_state_mutex);
        if (m_changed.exchange(false, std::memory_order_relaxed))
        {
            publish_locked();
        }
    }
    refresh_status();
}

void controller_t::refresh_status()
{
    const auto* view = m_view.load(std::memory_order_acquire);
    const bool mirror = view != nullptr && settings.enable_silent != 0 && dll_active();
    // Armed only while the module is actually alive and was told to take silent shots. A module that failed to
    // load, or one whose signature never matched, cannot deliver an angle, so the aim path must not treat it as
    // a second transport: it would suppress the mouse move and steer nothing.
    m_aim_mirror.store(mirror, std::memory_order_release);
}

void controller_t::publish_aim(const float pitch, const float yaw) noexcept
{
    if (!aim_mirror_armed())
    {
        return;
    }
    auto* view = m_view.load(std::memory_order_relaxed);
    if (!view)
    {
        return;
    }
    // Written as one half of the block by this thread alone. The module reads pitch and yaw without a sequence
    // guard, which is the same tolerance the contract was designed with: an angle torn between two targets is
    // caught by the module's own finite range check, and the next command carries a fresh one within a tick.
    view->aim.pitch = pitch;
    view->aim.yaw = yaw;
    view->aim.valid = 1;
}

void controller_t::clear_aim() noexcept
{
    if (auto* view = m_view.load(std::memory_order_relaxed))
    {
        view->aim.valid = 0;
    }
}

void controller_t::mark_changed()
{
    m_changed.store(true, std::memory_order_release);
}

inject_status controller_t::status() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    if (view && vesta::hvh_shared::valid(*view) && view->state.dll_loaded)
    {
        return inject_status::injected;
    }
    return m_status == inject_status::failed ? inject_status::failed : inject_status::not_injected;
}

bool controller_t::dll_active() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    if (!view || !vesta::hvh_shared::valid(*view) || !view->state.dll_loaded)
    {
        return false;
    }
    // Unsigned tick arithmetic on purpose: the wrap is 49.7 days of uptime and a stale heartbeat only has to
    // be recognised as stale, not dated.
    const auto now = static_cast<std::uint32_t>(::GetTickCount());
    return now - view->state.heartbeat < k_heartbeat_stale_ms;
}

bool controller_t::signature_found() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    return view && vesta::hvh_shared::valid(*view) && view->state.signature_found != 0;
}

bool controller_t::hook_ready() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    return view && vesta::hvh_shared::valid(*view) && view->state.hook_ready != 0;
}

int controller_t::hook_calls() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    return view && vesta::hvh_shared::valid(*view) ? view->state.hook_calls : 0;
}

int controller_t::targets_found() const
{
    const auto* view = m_view.load(std::memory_order_acquire);
    return view && vesta::hvh_shared::valid(*view) ? view->state.targets_found : 0;
}

std::string_view controller_t::last_error() const
{
    return m_last_error;
}

const std::filesystem::path& controller_t::dll_path() const
{
    return m_dll_path;
}

bool controller_t::inject()
{
    std::scoped_lock lock(m_state_mutex);
    m_last_error.clear();
    if (dll_active())
    {
        m_status = inject_status::injected;
        return true;
    }

    refresh_paths();
    if (!std::filesystem::exists(m_dll_path))
    {
        m_last_error = "vesta_hvh.dll was not found next to the Vesta executable.";
        m_status = inject_status::failed;
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::mode, 1, diagnostics::subject::hvh);
        return false;
    }

    if (!ensure_mapping())
    {
        m_status = inject_status::failed;
        return false;
    }
    auto* view = m_view.load(std::memory_order_relaxed);
    view->state.unload_request = 0;
    view->config = settings;
    view->sigs = signatures;
    view->aim = {};

    const auto pid = static_cast<DWORD>(app::context().process.process_id());
    if (pid == 0)
    {
        m_last_error = "the game process is not available.";
        m_status = inject_status::failed;
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::mode, 2, diagnostics::subject::hvh);
        return false;
    }

    // The memory service opens the game for reading and writing only. Creating a thread is a separate right,
    // deliberately not part of that handle, so the loader asks for exactly what it needs here.
    constexpr auto rights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                            PROCESS_VM_WRITE | PROCESS_VM_READ;
    const HANDLE process = ::OpenProcess(rights, FALSE, pid);
    if (!process)
    {
        m_last_error = "OpenProcess failed; run Vesta with elevated rights.";
        m_status = inject_status::failed;
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::win32, static_cast<std::uint32_t>(::GetLastError()),
                          diagnostics::subject::hvh);
        return false;
    }

    diagnostics::emit(diagnostics::event::hvh_inject_begin, diagnostics::severity::information,
                      diagnostics::code_domain::none, 0, diagnostics::subject::hvh);

    const auto path = m_dll_path.wstring();
    const auto path_bytes = static_cast<SIZE_T>((path.size() + 1) * sizeof(wchar_t));
    void* remote = ::VirtualAllocEx(process, nullptr, path_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        m_last_error = "VirtualAllocEx failed in the game process.";
        ::CloseHandle(process);
        m_status = inject_status::failed;
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::win32, static_cast<std::uint32_t>(::GetLastError()),
                          diagnostics::subject::hvh);
        return false;
    }

    bool ok = ::WriteProcessMemory(process, remote, path.c_str(), path_bytes, nullptr) != FALSE;
    if (!ok)
    {
        m_last_error = "WriteProcessMemory failed in the game process.";
    }
    else
    {
        const auto kernel32 = ::GetModuleHandleW(L"kernel32.dll");
        const auto load_library = kernel32 ? ::GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
        if (!load_library)
        {
            m_last_error = "failed to resolve LoadLibraryW.";
            ok = false;
        }
        else
        {
            const HANDLE thread = ::CreateRemoteThread(process, nullptr, 0,
                                                       reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote, 0,
                                                       nullptr);
            if (!thread)
            {
                m_last_error = "CreateRemoteThread failed in the game process.";
                ok = false;
            }
            else
            {
                const auto wait = ::WaitForSingleObject(thread, k_remote_load_timeout);
                DWORD exit_code{};
                ::GetExitCodeThread(thread, &exit_code);
                ::CloseHandle(thread);
                if (wait != WAIT_OBJECT_0 || exit_code == 0)
                {
                    // A zero exit code is LoadLibrary returning null: the game refused the module. That is the
                    // result an anti-cheat update shows up as, so it is worth a record of its own.
                    m_last_error = "the game refused to load vesta_hvh.dll.";
                    ok = false;
                }
            }
        }
    }

    ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    ::CloseHandle(process);

    if (!ok)
    {
        m_status = inject_status::failed;
        diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                          diagnostics::code_domain::mode, 3, diagnostics::subject::hvh);
        return false;
    }

    // Give the module a moment to map the shared block and report its heartbeat.
    const auto deadline = ::GetTickCount64() + k_status_poll_timeout;
    while (::GetTickCount64() < deadline)
    {
        if (dll_active())
        {
            m_status = inject_status::injected;
            m_last_error.clear();
            refresh_status();
            diagnostics::emit(diagnostics::event::hvh_inject_ready, diagnostics::severity::information,
                               diagnostics::code_domain::mode, hook_ready() ? 1u : 0u,
                               diagnostics::subject::hvh);
            return true;
        }
        ::Sleep(25);
    }

    m_last_error = "vesta_hvh.dll loaded but did not report a heartbeat.";
    m_status = inject_status::failed;
    diagnostics::emit(diagnostics::event::hvh_inject_failed, diagnostics::severity::error,
                      diagnostics::code_domain::mode, 4, diagnostics::subject::hvh);
    return false;
}

bool controller_t::eject()
{
    std::scoped_lock lock(m_state_mutex);
    m_last_error.clear();
    auto* view = m_view.load(std::memory_order_acquire);
    if (!view)
    {
        m_status = inject_status::not_injected;
        return true;
    }

    // The worker keeps running and takes the hook down itself; unloading the module from another thread would
    // free the code a game thread may be inside right now.
    view->state.unload_request = 1;
    const auto deadline = ::GetTickCount64() + k_status_poll_timeout;
    while (::GetTickCount64() < deadline)
    {
        if (view->state.dll_loaded == 0)
        {
            break;
        }
        ::Sleep(25);
    }
    view->state.unload_request = 0;
    m_aim_mirror.store(false, std::memory_order_release);
    m_status = inject_status::not_injected;
    diagnostics::emit(diagnostics::event::hvh_ejected, diagnostics::severity::information,
                      diagnostics::code_domain::mode, view->state.dll_loaded == 0 ? 1u : 0u,
                      diagnostics::subject::hvh);
    return true;
}

void controller_t::shutdown()
{
    {
        std::scoped_lock lock(m_state_mutex);
        if (auto* view = m_view.load(std::memory_order_acquire))
        {
            // Ask for the unload before anything else leaves, and never wait for it: a game that has already
            // gone would leave this holding the exit up.
            view->state.unload_request = 1;
        }
        m_aim_mirror.store(false, std::memory_order_release);
        save();
        release_mapping();
        m_initialized.store(false, std::memory_order_release);
    }
}

void controller_t::load()
{
    if (m_directory.empty())
    {
        refresh_paths();
    }
    const auto file = config_file(m_config_directory);

    std::error_code size_error{};
    const auto size = std::filesystem::file_size(file, size_error);
    if (size_error)
    {
        return; // No file yet: the defaults in the contract stand.
    }
    if (size > k_max_config_bytes)
    {
        m_last_error = "hvh.json is too large; using defaults.";
        diagnostics::emit(diagnostics::event::hvh_config_failed, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 1, diagnostics::subject::hvh);
        return;
    }

    std::ifstream input(file, std::ios::binary);
    if (!input)
    {
        return;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
    {
        return;
    }

    const auto document = nlohmann::json::parse(bytes, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        m_last_error = "hvh.json is malformed; using defaults.";
        diagnostics::emit(diagnostics::event::hvh_config_failed, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 2, diagnostics::subject::hvh);
        return;
    }

    for (const auto& bound : k_fields)
    {
        const auto entry = document.find(bound.key);
        if (entry == document.end())
        {
            continue;
        }
        // Written back the way every other setting in this folder stores a switch, so read both spellings:
        // a hand-made file may carry 1 where the menu wrote true.
        std::int64_t value{};
        if (entry->is_boolean())
        {
            value = entry->get<bool>() ? 1 : 0;
        }
        else if (entry->is_number_integer())
        {
            value = entry->get<std::int64_t>();
        }
        else
        {
            continue;
        }
        // A value outside the field's meaning is clamped rather than rejected, so one bad edit in a hand-made
        // file cannot hand the module a spin speed of two billion degrees a tick.
        value = std::clamp(value, static_cast<std::int64_t>(bound.minimum), static_cast<std::int64_t>(bound.maximum));
        settings.*(bound.field) = static_cast<std::int32_t>(value);
    }

    if (document.contains("signatures") && document["signatures"].is_object())
    {
        const auto& sigs = document["signatures"];
        const auto read_sig = [&sigs](const char* key, char (&destination)[vesta::hvh_shared::k_signature_length])
        {
            const auto entry = sigs.find(key);
            if (entry != sigs.end() && entry->is_string())
            {
                store_signature(destination, entry->get<std::string>());
            }
        };
        read_sig("create_move", signatures.create_move);
        read_sig("input", signatures.input);
        read_sig("entity_list", signatures.entity_list);
    }
}

void controller_t::save()
{
    if (m_directory.empty())
    {
        refresh_paths();
    }

    nlohmann::ordered_json document{};
    for (const auto& bound : k_fields)
    {
        const auto value = settings.*(bound.field);
        // A switch is written as true or false the way the rest of this folder writes them; the block still
        // receives the int32 the contract declares. Spelled out because the conditional operator would have
        // promoted the bool back to an int and quietly written 1.
        if (is_flag(bound.minimum, bound.maximum))
        {
            document[bound.key] = value != 0;
        }
        else
        {
            document[bound.key] = value;
        }
    }
    document["signatures"] = {
        {"create_move", std::string(signatures.create_move)},
        {"input", std::string(signatures.input)},
        {"entity_list", std::string(signatures.entity_list)},
    };

    const auto target = config_file(m_config_directory);
    if (const auto error = platform::windows::write_file_atomically(target, document.dump(4) + "\n"); error)
    {
        m_last_error = "hvh.json could not be written.";
        diagnostics::emit(diagnostics::event::hvh_config_failed, diagnostics::severity::warning,
                          diagnostics::code_domain::system, static_cast<std::uint32_t>(error.value()),
                          diagnostics::subject::hvh);
    }
}

controller_t& controller()
{
    static controller_t instance{};
    return instance;
}

} // namespace features::hvh
