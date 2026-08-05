module;
#include <string>
module remap_source;

void remap_source::create(const std::string& master_source, const std::string& source_name, const std::string& description)
{
    // PulseAudio module-remap-source creates a virtual microphone (source)
    // linked to a master source (or a null sink's monitor)
    std::string args = "master=" + master_source +
                       " source_name=" + source_name +
                       " source_properties=device.description=\"" + description + "\"";

    set_module_id(get_pulse_client().load_module("module-remap-source", args));
}