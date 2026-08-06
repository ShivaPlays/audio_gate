module;
#include <string>
module remap_source;

void remap_source::create(const std::string& master_source, const std::string& source_name, const std::string& description)
{
    // 1. dont_remember=true stops WirePlumber from persisting stream state/volume for this remapped source
    // 2. node.name inside source_properties ensures PipeWire registers a static node identity
    std::string args = "master=" + master_source +
                       " source_name=" + source_name +
                       " dont_remember=true" +
                       " source_properties=\"device.description=\\\"" + description + "\\\" node.name=\\\"" + source_name + "\\\"\"";

    set_module_id(get_pulse_client().load_module("module-remap-source", args));
}