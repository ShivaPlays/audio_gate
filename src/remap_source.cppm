module;
#include <string>
export module remap_source;

import virtual_audio_device;
import pulse_client;

export class remap_source
    : public virtual_audio_device
{
public:
    remap_source(pulse_client& client) noexcept
        : virtual_audio_device{ client }
    {}
public:
    void create(const std::string& master_source, const std::string& source_name, const std::string& description);

protected:

private:
};