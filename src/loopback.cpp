module;
#include <string>
#include <cstdint>
#include <pulse/pulseaudio.h>
module loopback;

void loopback::create(const std::string& source, const std::string &sink)
{
    // PulseAudio module-loopback routes audio from a source (mic/monitor) to a sink (speakers/virtual sink)
    std::string args = "source=" + source + " sink=" + sink;

    set_module_id(get_pulse_client().load_module("module-loopback", args));
}

bool loopback::set_target_sink(const std::string& new_sink_name)
{
    // 1. Get the Sink Input stream ID corresponding to this loopback module
    if (!get_module_id().has_value()) return false;

    uint32_t sink_input_idx = get_pulse_client().get_sink_input_for_module(get_module_id().value());

    if (sink_input_idx == PA_INVALID_INDEX) return false;

    // 2. Move the stream to the new hardware playback device seamlessly
    return get_pulse_client().move_sink_input_to_sink(sink_input_idx, new_sink_name);
}