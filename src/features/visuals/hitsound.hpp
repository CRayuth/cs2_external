#pragma once

#include <memory>
#include <string>
#include <vector>

namespace features::visuals
{

struct sound_device
{
    std::string id{};
    std::string name{};
    bool system_default{};
};

class hitsound_player
{
public:
    hitsound_player();
    ~hitsound_player();
    hitsound_player(const hitsound_player&) = delete;
    hitsound_player& operator=(const hitsound_player&) = delete;

    void play(int style, float volume);
    [[nodiscard]] std::vector<sound_device> devices() const;
    void set_device(const std::string& id);
    [[nodiscard]] std::string device() const;

private:
    struct implementation;
    std::unique_ptr<implementation> m_impl;
};

inline hitsound_player& hitsounds()
{
    static hitsound_player value{};
    return value;
}

} // namespace features::visuals
