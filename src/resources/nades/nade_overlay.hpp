#pragma once

#include <resources/nades/nade_data.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace resources::nades
{
// User-supplied lineup files, merged with the generated table so a map can gain lineups without the
// generated header being re-edited: one JSON document per map, `<data root>/nades/<map>.json`.
//
// A record repeats the generated table's fields:
//   name, action, x, y, z, pitch, yaw, type, actions, run_ticks, after_jump_ticks,
//   throw_strength, manual
// `type` is "smoke" | "flash" | "molotov" | "he" | "decoy"; `actions` is either a list of "crouch",
// "run", "jump", "walk", "release_after", "always_run" or the raw bit field.
//
// Nothing here can stop the helper: a file that does not parse, or a record that does not validate, is
// counted in rejected_files() and skipped.
class overlay_store
{
public:
    // How long a folder is trusted before it is looked at again, so an edited file is picked up without
    // a restart while a normal pass costs no file I/O at all.
    static constexpr std::chrono::seconds rescan_period{5};

    // Reads the files describing `canonical_map`. Called by both helper threads on every pass: the map
    // name and the rescan period decide whether anything is actually read.
    void reload_for(std::string_view canonical_map, const std::filesystem::path& folder);

    // Rows for the map that was read last, null when nothing was loaded. Loaded sets are never freed,
    // so these rows and the strings their `name` and `action` refer to stay valid for the whole
    // process: a lineup copied out of them can outlive a later map change or a reload mid-throw.
    [[nodiscard]] const std::vector<entry>* rows() const noexcept
    {
        return m_published.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t loaded_rows() const noexcept;
    [[nodiscard]] std::size_t rejected_files() const noexcept;
    // A copy, because the stored name moves when the next map is read.
    [[nodiscard]] std::string loaded_map() const;

private:
    // Owns the text that a published row points at.
    struct record
    {
        std::string name;
        std::string action;
    };
    struct lineup_set
    {
        std::vector<record> records;
        std::vector<entry> rows;
    };

    // Appends the file's valid records to `into`; the row text is only wired up later, once `into`
    // has its final layout. `rejected` counts the documents and records that were refused.
    void read_file(const std::filesystem::path& path, std::string_view canonical_map, lineup_set& into,
                   std::size_t& rejected) const;
    [[nodiscard]] std::string folder_fingerprint(const std::filesystem::path& folder) const;

    mutable std::mutex m_mutex;
    std::string m_map;
    std::string m_fingerprint;
    // The map and folder content of the last reload this store refused. A refusal leaves the published rows
    // alone, so without this memo the same folder would be read and parsed again on every rescan.
    std::string m_refused_map;
    std::string m_refused_fingerprint;
    // Sets stay alive forever, which is what makes the raw pointers handed to the helper safe. Bounded by
    // the fact that a reload only happens when the folder's content really changed.
    std::vector<std::shared_ptr<lineup_set>> m_kept;
    std::atomic<const std::vector<entry>*> m_published{};
    std::size_t m_loaded_rows{};
    std::size_t m_rejected_files{};
    std::chrono::steady_clock::time_point m_last_check{};
};

// The one overlay store the lineup helper reads through.
[[nodiscard]] overlay_store& nade_overlay() noexcept;

} // namespace resources::nades
