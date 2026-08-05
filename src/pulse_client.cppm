module; // 1. Global Module Fragment (Standard headers & C libraries go here)
#include <mutex>
#include <atomic>
#include <string>
#include <functional>

#include <pulse/pulseaudio.h>
export module pulse_client; // 2. Module Declaration

export class pulse_client
{
public:
    enum class device_event_type
    {
        added,
        removed,
        changed
    };

    enum class device_type
    {
        sink,   //playback device
        source  //recording / mic / monitor device
    };

    struct device_info
    {
        uint32_t index{};
        std::string name{};
        std::string description{};
        device_type type{};
    };

    using device_event_callback = std::function<void(device_event_type event, const device_info& info)>;

    pulse_client() noexcept = default;

    pulse_client(const pulse_client& other) = delete;
    pulse_client& operator=(const pulse_client& other) = delete;

public:
    bool ensure_connected();
    void disconnect();
    bool reconnect();

    [[nodiscard]] std::vector<device_info> get_devices() const;

    uint32_t load_module(const std::string& name, const std::string& args);
    void unload_module(uint32_t module_index);
    void set_default_sink(const std::string& sink_name);
    std::string get_default_sink();
    void evict_streams_from_sink(const std::string& source_sink_name, const std::string& target_sink_name);

    // Register a callback for device hotplug / creation / removal events
    void set_device_event_callback(device_event_callback cb) { m_device_callback = std::move(cb); }

    bool move_sink_input_to_sink(uint32_t sink_input_idx, const std::string& new_sink_name);
    uint32_t get_sink_input_for_module(uint32_t module_index);

protected:

private:
    struct query_data
    {
        pulse_client* client;
        device_event_type event_type;
        device_type dev_type;
    };

    static void context_state_callback(pa_context* c, void* userdata);
    static void subscribe_callback(pa_context* c, pa_subscription_event_type_t t, uint32_t idx, void* userdata);
    static void sink_info_callback(pa_context* c, const pa_sink_info* i, int eol, void* userdata);
    static void source_info_callback(pa_context* c, const pa_source_info* i, int eol, void* userdata);

    bool connect_internal();
    void fetch_initial_devices();
    void process_device_update(device_event_type event, const device_info& info);

    std::mutex m_init_mutex;
    mutable std::mutex m_cache_mutex;

    device_event_callback m_device_callback{nullptr};

    std::vector<device_info> m_device_cache;

    pa_threaded_mainloop* m_mainloop{ nullptr };
    pa_mainloop_api *m_api{ nullptr };
    pa_context* m_context{ nullptr };

    std::atomic<bool> m_connected{false};
};