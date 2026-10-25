#include <stdafx.hpp>
#include <features/visuals/hitsound.hpp>
#include <resources/sounds/hitsounds.hpp>

#include <xaudio2.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <condition_variable>
#include <deque>
#include <utility>

namespace features::visuals
{

namespace
{

struct wav_view
{
    WAVEFORMATEX format{};
    const BYTE* samples{};
    UINT32 sample_bytes{};
};

[[nodiscard]] std::uint16_t read_u16(const unsigned char* value)
{
    return static_cast<std::uint16_t>(value[0] | value[1] << 8);
}

[[nodiscard]] std::uint32_t read_u32(const unsigned char* value)
{
    return static_cast<std::uint32_t>(value[0]) | static_cast<std::uint32_t>(value[1]) << 8 |
           static_cast<std::uint32_t>(value[2]) << 16 | static_cast<std::uint32_t>(value[3]) << 24;
}

[[nodiscard]] std::optional<wav_view> parse_wav(const unsigned char* bytes, std::size_t size)
{
    if (!bytes || size < 44 || std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0)
    {
        return std::nullopt;
    }

    wav_view result{};
    bool have_format{};
    for (std::size_t cursor = 12; cursor + 8 <= size;)
    {
        const auto chunk_size = static_cast<std::size_t>(read_u32(bytes + cursor + 4));
        const auto data = cursor + 8;
        if (data + chunk_size > size)
            break;
        if (std::memcmp(bytes + cursor, "fmt ", 4) == 0 && chunk_size >= 16)
        {
            result.format.wFormatTag = read_u16(bytes + data);
            result.format.nChannels = read_u16(bytes + data + 2);
            result.format.nSamplesPerSec = read_u32(bytes + data + 4);
            result.format.nAvgBytesPerSec = read_u32(bytes + data + 8);
            result.format.nBlockAlign = read_u16(bytes + data + 12);
            result.format.wBitsPerSample = read_u16(bytes + data + 14);
            result.format.cbSize = 0;
            have_format = result.format.wFormatTag == WAVE_FORMAT_PCM;
        }
        else if (std::memcmp(bytes + cursor, "data", 4) == 0)
        {
            result.samples = reinterpret_cast<const BYTE*>(bytes + data);
            result.sample_bytes = static_cast<UINT32>(chunk_size);
        }
        cursor = data + chunk_size + (chunk_size & 1u);
    }

    if (!have_format || !result.samples || !result.sample_bytes || !result.format.nChannels ||
        !result.format.nSamplesPerSec)
    {
        return std::nullopt;
    }
    return result;
}

struct embedded_sound
{
    const unsigned char* bytes{};
    std::size_t size{};
};

constexpr std::array sounds{embedded_sound{resources::sounds::soft, resources::sounds::soft_size},
                            embedded_sound{resources::sounds::glass, resources::sounds::glass_size},
                            embedded_sound{resources::sounds::pluck, resources::sounds::pluck_size},
                            embedded_sound{resources::sounds::crisp, resources::sounds::crisp_size},
                            embedded_sound{resources::sounds::flesh, resources::sounds::flesh_size}};

} // namespace

struct hitsound_player::implementation
{
    struct request
    {
        enum class kind
        {
            play,
            device
        } type{};
        int style{};
        float volume{};
        std::string device{};
    };
    struct voice
    {
        IXAudio2SourceVoice* source{};
    };

    std::mutex mutex{};
    std::condition_variable wake{};
    std::deque<request> requests{};
    bool stopping{};
    std::vector<sound_device> m_devices{};
    std::string m_selected{};
    IXAudio2* engine{};
    IXAudio2MasteringVoice* mastering{};
    std::vector<voice> voices{};
    std::thread worker{};
    std::string m_default_id{};
    std::string m_attached{};
    bool m_attached_resolved{};
    std::chrono::steady_clock::time_point m_next_scan{};

    implementation()
    {
        worker = std::thread(
            [this]
            {
                ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                run();
                ::CoUninitialize();
            });
    }

    ~implementation()
    {
        {
            std::scoped_lock lock(mutex);
            stopping = true;
        }
        wake.notify_one();
        if (worker.joinable())
            worker.join();
        for (auto& entry : voices)
        {
            if (entry.source)
                entry.source->DestroyVoice();
        }
        if (mastering)
            mastering->DestroyVoice();
        if (engine)
            engine->Release();
    }

    [[nodiscard]] static std::string to_utf8(const wchar_t* value)
    {
        if (!value)
            return {};
        const auto needed = ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 1)
            return {};
        std::string text(static_cast<std::size_t>(needed) - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), needed, nullptr, nullptr);
        return text;
    }

    [[nodiscard]] static std::wstring to_utf16(const std::string& value)
    {
        if (value.empty())
            return {};
        const auto needed =
            ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(needed), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), needed);
        return wide;
    }

    void scan()
    {
        std::vector<sound_device> listed{};
        std::string fallback{};
        IMMDeviceEnumerator* enumerator{};
        if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
        {
            IMMDevice* device{};
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
            {
                LPWSTR raw{};
                if (SUCCEEDED(device->GetId(&raw)))
                {
                    fallback = to_utf8(raw);
                    ::CoTaskMemFree(raw);
                }
                device->Release();
            }
            IMMDeviceCollection* collection{};
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
            {
                UINT count{};
                collection->GetCount(&count);
                for (UINT index = 0; index < count; ++index)
                {
                    IMMDevice* item{};
                    if (FAILED(collection->Item(index, &item)))
                        continue;
                    sound_device entry{};
                    LPWSTR raw{};
                    if (SUCCEEDED(item->GetId(&raw)))
                    {
                        entry.id = to_utf8(raw);
                        ::CoTaskMemFree(raw);
                    }
                    IPropertyStore* store{};
                    if (SUCCEEDED(item->OpenPropertyStore(STGM_READ, &store)))
                    {
                        PROPVARIANT value{};
                        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
                            value.pwszVal)
                        {
                            entry.name = to_utf8(value.pwszVal);
                            ::CoTaskMemFree(value.pwszVal);
                        }
                        store->Release();
                    }
                    entry.system_default = !entry.id.empty() && entry.id == fallback;
                    item->Release();
                    if (!entry.id.empty())
                        listed.push_back(std::move(entry));
                }
                collection->Release();
            }
            enumerator->Release();
        }

        std::scoped_lock lock(mutex);
        m_devices = std::move(listed);
        m_default_id = fallback;
    }

    [[nodiscard]] std::string selected()
    {
        std::scoped_lock lock(mutex);
        return m_selected;
    }

    [[nodiscard]] bool endpoint_exists(const std::string& id)
    {
        std::scoped_lock lock(mutex);
        return std::any_of(m_devices.begin(), m_devices.end(),
                           [&](const sound_device& entry) { return entry.id == id; });
    }

    [[nodiscard]] std::string resolve(const std::string& wanted)
    {
        if (!wanted.empty() && endpoint_exists(wanted))
            return wanted;
        std::scoped_lock lock(mutex);
        return m_default_id;
    }

    void drop_voices()
    {
        for (auto& entry : voices)
        {
            if (!entry.source)
                continue;
            entry.source->Stop(0);
            entry.source->DestroyVoice();
        }
        voices.clear();
    }

    bool attach_mastering(const std::string& resolved)
    {
        if (!engine)
            return false;
        drop_voices();
        if (mastering)
        {
            mastering->DestroyVoice();
            mastering = nullptr;
        }
        const auto wide = to_utf16(resolved);
        auto result = FAILED(engine->CreateMasteringVoice(&mastering, XAUDIO2_DEFAULT_CHANNELS,
                                                          XAUDIO2_DEFAULT_SAMPLERATE, 0,
                                                          resolved.empty() ? nullptr : wide.c_str(), nullptr,
                                                          AudioCategory_GameEffects))
                          ? engine->CreateMasteringVoice(&mastering, XAUDIO2_DEFAULT_CHANNELS,
                                                         XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr,
                                                         AudioCategory_GameEffects)
                          : S_OK;
        if (FAILED(result) || !mastering)
        {
            mastering = nullptr;
            m_attached.clear();
            m_attached_resolved = false;
            return false;
        }
        m_attached = resolved;
        m_attached_resolved = true;
        return true;
    }

    bool ensure_output()
    {
        if (!engine && FAILED(XAudio2Create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR)))
        {
            engine = nullptr;
            return false;
        }
        if (mastering && m_attached_resolved && m_attached == resolve(selected()))
            return true;
        return attach_mastering(resolve(selected()));
    }

    void follow_output()
    {
        if (!engine)
            return;
        if (mastering && m_attached_resolved && m_attached == resolve(selected()))
            return;
        static_cast<void>(attach_mastering(resolve(selected())));
    }

    void enqueue(int style, float volume)
    {
        {
            std::scoped_lock lock(mutex);
            if (stopping)
                return;
            if (requests.size() >= 32)
                requests.pop_front();
            requests.push_back({request::kind::play, style, volume, {}});
        }
        wake.notify_one();
    }

    void select(const std::string& id)
    {
        {
            std::scoped_lock lock(mutex);
            if (stopping || m_selected == id)
                return;
            m_selected = id;
            if (requests.size() >= 32)
                requests.pop_front();
            requests.push_back({request::kind::device, 0, 0.0f, id});
        }
        wake.notify_one();
    }

    void run()
    {
        scan();
        for (;;)
        {
            request next{};
            bool have{};
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::seconds(1), [&] { return stopping || !requests.empty(); });
                if (stopping && requests.empty())
                    break;
                if (!requests.empty())
                {
                    next = std::move(requests.front());
                    requests.pop_front();
                    have = true;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= m_next_scan)
            {
                m_next_scan = now + std::chrono::seconds(3);
                scan();
            }
            if (!have)
            {
                follow_output();
                continue;
            }
            if (next.type == request::kind::device)
            {
                follow_output();
                continue;
            }
            play_now(next.style, next.volume);
        }
    }

    void collect_finished()
    {
        std::erase_if(voices,
                      [](voice& value)
                      {
                          XAUDIO2_VOICE_STATE state{};
                          value.source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                          if (state.BuffersQueued)
                              return false;
                          value.source->DestroyVoice();
                          return true;
                      });
    }

    void play_now(int style, float volume)
    {
        const auto index = static_cast<std::size_t>(std::clamp(style, 0, static_cast<int>(sounds.size()) - 1));
        const auto wave = parse_wav(sounds[index].bytes, sounds[index].size);
        if (!wave || !ensure_output())
            return;
        collect_finished();
        if (voices.size() >= 16)
        {
            auto& oldest = voices.front();
            oldest.source->Stop(0);
            oldest.source->DestroyVoice();
            voices.erase(voices.begin());
        }

        IXAudio2SourceVoice* source{};
        if (FAILED(engine->CreateSourceVoice(&source, &wave->format)))
            return;
        source->SetVolume(std::clamp(volume, 0.0f, 1.0f));
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = wave->sample_bytes;
        buffer.pAudioData = wave->samples;
        if (FAILED(source->SubmitSourceBuffer(&buffer)) || FAILED(source->Start(0)))
        {
            source->DestroyVoice();
            return;
        }
        voices.push_back({source});
    }
};

hitsound_player::hitsound_player()
    : m_impl(std::make_unique<implementation>())
{
}
hitsound_player::~hitsound_player() = default;

void hitsound_player::play(int style, float volume)
{
    if (!m_impl || volume <= 0.0f)
        return;
    m_impl->enqueue(style, std::clamp(volume, 0.0f, 1.0f));
}

std::vector<sound_device> hitsound_player::devices() const
{
    if (!m_impl)
        return {};
    std::scoped_lock lock(m_impl->mutex);
    return m_impl->m_devices;
}

void hitsound_player::set_device(const std::string& id)
{
    if (m_impl)
        m_impl->select(id);
}

std::string hitsound_player::device() const
{
    if (!m_impl)
        return {};
    std::scoped_lock lock(m_impl->mutex);
    return m_impl->m_selected;
}

} // namespace features::visuals
