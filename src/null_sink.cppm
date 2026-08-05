module;
#include <string>
export module null_sink;

import virtual_audio_device;
import pulse_client;

export class null_sink
    : public virtual_audio_device
{
public:
    null_sink(pulse_client& client) noexcept
        : virtual_audio_device{ client }
    {}
public:
    void create(const std::string& name, const std::string& description);

protected:

private:
};