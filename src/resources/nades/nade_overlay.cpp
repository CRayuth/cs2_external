#include <stdafx.hpp>
#include <resources/nades/nade_overlay.hpp>

#include <diagnostics/log.hpp>
#include <resources/nades/nade_match.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace resources::nades
{
namespace
{
using json = nlohmann::json;

// Everything about reading user files is deliberately bounded: a malformed or enormous document must
// cost the helper a rejected file, never a stall or an allocation of the same size.
constexpr std::size_t k_max_files{64};
constexpr std::uintmax_t k_max_file_bytes{4u * 1024u * 1024u};
constexpr std::size_t k_max_total_rows{8192};
constexpr std::size_t k_max_text_bytes{96};
constexpr std::size_t k_max_kept_sets{32};

// The engine only ever reaches three charge levels, because they come from which buttons are held.
// prime_throw() then waits for the live charge to come within this much of the recorded one.
constexpr float k_strength_tolerance{0.08f};

[[nodiscard]] bool reachable_strength(const float strength) noexcept
{
    constexpr std::array<float, 3> standard{0.0f, 0.5f, 1.0f};
    return std::any_of(standard.begin(), standard.end(),
                       [strength](const float target)
                       { return std::abs(strength - target) <= k_strength_tolerance; });
}

[[nodiscard]] bool ci_name_equal(const std::string_view left, const std::string_view right) noexcept
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](const char a, const char b) { return ascii_lower(a) == ascii_lower(b); });
}

[[nodiscard]] bool parse_kind(const std::string_view text, kind& out) noexcept
{
    static constexpr std::pair<std::string_view, kind> names[]{
        { "smoke", kind::smoke },        { "flash", kind::flash },      { "molotov", kind::molotov },
        { "incendiary", kind::molotov }, { "inferno", kind::molotov },  { "he", kind::he },
        { "grenade", kind::he },         { "highexplosive", kind::he }, { "decoy", kind::decoy },
    };
    for (const auto& [spelling, value] : names)
    {
        if (ci_name_equal(text, spelling))
        {
            out = value;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool parse_actions(const json& value, std::uint16_t& out) noexcept
{
    if (value.is_number_integer())
    {
        const auto raw = value.get<std::int64_t>();
        if (raw < 0 || raw > std::numeric_limits<std::uint16_t>::max())
            return false;
        out = static_cast<std::uint16_t>(raw);
        return true;
    }
    if (!value.is_array())
        return false;
    static constexpr std::pair<std::string_view, std::uint16_t> flags[]{
        { "crouch", action_crouch },
        { "run", action_run },
        { "forward", action_run },
        { "jump", action_jump },
        { "walk", action_walk },
        { "release_after", action_release_movement_after_throw },
        { "release_movement_after_throw", action_release_movement_after_throw },
        { "always_run", action_always_run },
    };
    auto bits = std::uint16_t{};
    for (const auto& entry : value)
    {
        if (!entry.is_string())
            return false;
        const auto text = entry.get<std::string>();
        auto known = false;
        for (const auto& [spelling, flag] : flags)
        {
            if (ci_name_equal(text, spelling))
            {
                bits |= flag;
                known = true;
                break;
            }
        }
        if (!known)
            return false;
    }
    out = bits;
    return true;
}

// A parsed row with its own text, before it is placed into the set that owns it for good.
struct draft_record
{
    std::string name;
    std::string action;
    entry row{};
};

[[nodiscard]] bool read_record(const json& value, draft_record& out)
{
    if (!value.is_object())
        return false;

    // A wrong type in any field rejects the record instead of throwing into the frame loop.
    const auto fetch = [&value](const char* key, auto& target) -> bool
    {
        const auto found = value.find(key);
        if (found == value.end() || found->is_null())
            return false;
        try
        {
            found->get_to(target);
        }
        catch (const json::exception&)
        {
            return false;
        }
        return true;
    };

    auto name = std::string{};
    auto action = std::string{};
    auto type = std::string{};
    if (!fetch("name", name) || !fetch("action", action) || !fetch("type", type))
        return false;
    if (name.empty() || name.size() > k_max_text_bytes || action.size() > k_max_text_bytes)
        return false;

    auto parsed_type = kind{};
    if (!parse_kind(type, parsed_type))
        return false;

    auto x = 0.0;
    auto y = 0.0;
    auto z = 0.0;
    auto pitch = 0.0;
    auto yaw = 0.0;
    auto strength = 0.0;
    if (!fetch("x", x) || !fetch("y", y) || !fetch("z", z) || !fetch("pitch", pitch) || !fetch("yaw", yaw) ||
        !fetch("throw_strength", strength))
        return false;

    // A non-finite or out-of-range angle is worse than no lineup at all: the helper would aim at it.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(pitch) || !std::isfinite(yaw) ||
        !std::isfinite(strength))
        return false;
    if (std::abs(x) > 1000000.0 || std::abs(y) > 1000000.0 || std::abs(z) > 1000000.0)
        return false;
    if (std::abs(pitch) > 89.0 || std::abs(yaw) > 180.0 || strength < 0.0 || strength > 1.0)
        return false;

    auto bits = std::uint16_t{};
    if (value.contains("actions") && !parse_actions(value.at("actions"), bits))
        return false;

    auto run_ticks = std::int64_t{};
    if (value.contains("run_ticks") && !fetch("run_ticks", run_ticks))
        return false;
    auto after_jump_ticks = std::int64_t{};
    if (value.contains("after_jump_ticks") && !fetch("after_jump_ticks", after_jump_ticks))
        return false;
    if (run_ticks < 0 || run_ticks > std::numeric_limits<std::uint16_t>::max())
        return false;
    if (after_jump_ticks < 0 || after_jump_ticks > std::numeric_limits<std::uint8_t>::max())
        return false;
    // A run-up the helper never presses is a throw that can never finish: only the +forward action drives
    // the run phase, so run_ticks without it is a contradictory record rather than an unreadable file.
    if (run_ticks > 0 && (bits & action_run) == 0u)
        return false;

    auto manual = false;
    static_cast<void>(fetch("manual", manual));

    out.name = std::move(name);
    out.action = std::move(action);
    // Filled in once the owning set has its final layout; see reload_for().
    out.row.name = nullptr;
    out.row.action = nullptr;
    out.row.x = static_cast<float>(x);
    out.row.y = static_cast<float>(y);
    out.row.z = static_cast<float>(z);
    out.row.pitch = static_cast<float>(pitch);
    out.row.yaw = static_cast<float>(yaw);
    out.row.type = parsed_type;
    out.row.actions = bits;
    out.row.run_ticks = static_cast<std::uint16_t>(run_ticks);
    out.row.after_jump_ticks = static_cast<std::uint8_t>(after_jump_ticks);
    out.row.throw_strength = static_cast<float>(strength);
    // A charge the game cannot hold is shown but never driven automatically, exactly as the generated
    // table marks its own odd records.
    out.row.manual = manual || !reachable_strength(out.row.throw_strength);
    return true;
}

// A file belongs to a map when its own "map" field, or else its filename, resolves to that table key.
[[nodiscard]] bool describes_map(const json& document, const std::filesystem::path& path,
                                 const std::string_view canonical_map)
{
    auto declared = std::string{};
    if (document.is_object())
    {
        const auto found = document.find("map");
        if (found != document.end() && found->is_string())
            declared = found->get<std::string>();
    }
    const auto candidate = declared.empty() ? path.stem().string() : declared;
    const auto resolution = resolve_map(std::string_view{candidate});
    return resolution.map && resolution.name == canonical_map;
}
} // namespace

void overlay_store::reload_for(const std::string_view canonical_map, const std::filesystem::path& folder)
{
    using clock = std::chrono::steady_clock;
    if (canonical_map.empty())
        return;

    std::scoped_lock lock(m_mutex);
    const auto now = clock::now();
    const auto changed_map = m_map != canonical_map;
    if (!changed_map && now - m_last_check < rescan_period)
        return;
    m_last_check = now;

    // Reading a folder whose content has not changed would publish an identical table under new
    // addresses for no reason, and every published set is kept alive on purpose.
    auto fingerprint = std::string{};
    try
    {
        fingerprint = folder_fingerprint(folder);
        if (!changed_map && fingerprint == m_fingerprint)
            return;
        // A refusal is remembered by level and folder content, because publishing cannot be undone: without
        // this the same folder would be read and parsed again on every rescan. A change to either is retried.
        if (canonical_map == m_refused_map && fingerprint == m_refused_fingerprint)
            return;

        auto rebuilt = std::make_shared<lineup_set>();
        auto rejected = std::size_t{};
        auto files = std::size_t{};
        auto skipped = std::size_t{};
        const auto listing = std::filesystem::directory_iterator(folder,
                                                                std::filesystem::directory_options::skip_permission_denied);
        for (const auto& file : listing)
        {
            std::error_code ignored{};
            if (!file.is_regular_file(ignored))
                continue;
            auto extension = file.path().extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](const char c) { return static_cast<char>(ascii_lower(c)); });
            if (extension != ".json")
                continue;
            if (++files > k_max_files || rebuilt->rows.size() >= k_max_total_rows)
            {
                ++skipped;
                continue;
            }
            read_file(file.path(), canonical_map, *rebuilt, rejected);
        }

        // Rows point into `records`, so both vectors are frozen here: nothing appends after this loop,
        // which is what makes the addresses stable for the lifetime of the process.
        if (rebuilt->rows.size() != rebuilt->records.size())
        {
            m_refused_map.assign(canonical_map);
            m_refused_fingerprint = fingerprint;
            return;
        }
        for (std::size_t index = 0; index < rebuilt->rows.size(); ++index)
        {
            rebuilt->rows[index].name = rebuilt->records[index].name.c_str();
            rebuilt->rows[index].action = rebuilt->records[index].action.c_str();
        }

        if (m_kept.size() >= k_max_kept_sets)
        {
            diagnostics::process_log().warning("[lineups] custom lineup reload limit reached; keeping the last set");
            m_refused_map.assign(canonical_map);
            m_refused_fingerprint = fingerprint;
            return;
        }

        // Publish, and only then record which level and which folder content the published rows belong to.
        // Committing the identity beforehand let a refused reload name a level whose rows were never published,
        // and the guard above then blocked every later retry for the rest of the session - leaving the helper
        // matching and executing the markers of a different map.
        m_kept.push_back(rebuilt);
        m_published.store(&rebuilt->rows, std::memory_order_release);
        m_map.assign(canonical_map);
        m_fingerprint = std::move(fingerprint);
        m_loaded_rows = rebuilt->rows.size();
        m_rejected_files = rejected + skipped;
        m_refused_map.clear();
        m_refused_fingerprint.clear();
    }
    catch (const std::exception& error)
    {
        // A hostile or half-written file must not reach the caller: the helper keeps its previous table.
        diagnostics::process_log().warning("[lineups] custom lineup read failed: {}", error.what());
    }
}

void overlay_store::read_file(const std::filesystem::path& path, const std::string_view canonical_map,
                              lineup_set& into, std::size_t& rejected) const
{
    std::error_code error{};
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > k_max_file_bytes)
    {
        ++rejected;
        diagnostics::process_log().warning("[lineups] {} is empty or too large to read", path.filename().string());
        return;
    }

    auto input = std::ifstream(path, std::ios::binary);
    if (!input)
    {
        ++rejected;
        diagnostics::process_log().warning("[lineups] cannot open {}", path.filename().string());
        return;
    }

    const auto document = json::parse(input, nullptr, false);
    if (document.is_discarded())
    {
        ++rejected;
        diagnostics::process_log().warning("[lineups] {} is not valid JSON", path.filename().string());
        return;
    }
    if (!describes_map(document, path, canonical_map))
        return;

    // Either {"lineups": [...]} for a documented file, or a bare array of records.
    const json* listed = nullptr;
    if (document.is_array())
        listed = &document;
    else if (document.is_object() && document.contains("lineups"))
        listed = &document.at("lineups");
    if (!listed || !listed->is_array())
    {
        ++rejected;
        diagnostics::process_log().warning("[lineups] {} has no \"lineups\" array", path.filename().string());
        return;
    }

    for (const auto& value : *listed)
    {
        auto draft = draft_record{};
        if (!read_record(value, draft))
        {
            ++rejected;
            diagnostics::process_log().warning("[lineups] {} contains an invalid record", path.filename().string());
            continue;
        }
        if (into.records.size() >= k_max_total_rows)
            break;
        // Kept index-aligned with `rows`, which is what lets reload_for() wire the text in afterwards.
        into.records.push_back(record{std::move(draft.name), std::move(draft.action)});
        into.rows.push_back(draft.row);
    }
}

std::string overlay_store::folder_fingerprint(const std::filesystem::path& folder) const
{
    auto entries = std::vector<std::string>{};
    std::error_code error{};
    for (const auto& file : std::filesystem::directory_iterator(folder,
                                                               std::filesystem::directory_options::skip_permission_denied,
                                                               error))
    {
        std::error_code ignored{};
        if (!file.is_regular_file(ignored))
            continue;
        const auto modified = file.last_write_time(ignored);
        entries.push_back(file.path().filename().string() + "|" + std::to_string(file.file_size(ignored)) + "|" +
                          std::to_string(modified.time_since_epoch().count()));
    }
    std::ranges::sort(entries);
    auto fingerprint = std::string{};
    for (const auto& entry : entries)
    {
        fingerprint += entry;
        fingerprint += ';';
    }
    return fingerprint;
}

std::size_t overlay_store::loaded_rows() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_loaded_rows;
}

std::size_t overlay_store::rejected_files() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_rejected_files;
}

std::string overlay_store::loaded_map() const
{
    std::scoped_lock lock(m_mutex);
    return m_map;
}

overlay_store& nade_overlay() noexcept
{
    static overlay_store value{};
    return value;
}

} // namespace resources::nades
