module;
#include <string>
module null_sink;

void null_sink::create(const std::string& name, const std::string& description)
{
    // 1. dont_remember=true prevents WirePlumber state persistence
    // 2. node.name inside sink_properties guarantees a static PipeWire node identity
    std::string args = "sink_name=" + name +
                       " dont_remember=true" +
                       " sink_properties=\"device.description=\\\"" + description + "\\\" node.name=\\\"" + name + "\\\"\"";

    set_module_id(get_pulse_client().load_module("module-null-sink", args));
}