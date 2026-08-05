module; // 1. Global Module Fragment (Standard headers & C libraries go here)

#include <string>
#include <optional>
#include <cstdint>
#include <functional>
#include <string_view>
#include <pulse/pulseaudio.h>

export module virtual_audio_device; // 2. Module Declaration
import pulse_client;

// 3. Exported Class Interface
export class virtual_audio_device
{
public:
    explicit virtual_audio_device(pulse_client& client) noexcept
        : m_pulse_client{ client }
    {}

    virtual ~virtual_audio_device() { destroy(); }

    // Non-copyable due to managing PulseAudio module IDs
    virtual_audio_device(const virtual_audio_device&) = delete;
    virtual_audio_device& operator=(const virtual_audio_device&) = delete;

    // Moveable
    virtual_audio_device(virtual_audio_device&&) noexcept = default;
    virtual_audio_device& operator=(virtual_audio_device&&) noexcept = default;

public:
    void destroy() noexcept;

    pulse_client& get_pulse_client() noexcept { return m_pulse_client; }

    [[nodiscard]] const std::string& get_name() const noexcept { return m_name; }
    [[nodiscard]] std::optional<uint32_t> get_module_id() const noexcept { return m_module_id; }
    [[nodiscard]] bool is_created() const noexcept { return m_module_id.has_value(); }

protected:
    void set_name(std::string name) { m_name = std::move(name); }
    void set_module_id(uint32_t id) noexcept { m_module_id = id; }
    void clear_module_id() noexcept { m_module_id.reset(); }

private:
    void fetch_actual_sink_name();

    std::string m_name;

    std::reference_wrapper<pulse_client> m_pulse_client;
    std::optional<uint32_t> m_module_id{ std::nullopt };
};