module;
#include <string>
module null_sink;

void null_sink::create(const std::string& name, const std::string& description)
{
    // PulseAudio module-null-sink takes:
    // sink_name: internal identifier
    // sink_properties: metadata like device.description for UI display
    std::string args = "sink_name=" + name +
                       " sink_properties=device.description=\"" + description + "\"";

    set_module_id(get_pulse_client().load_module("module-null-sink", args));
}