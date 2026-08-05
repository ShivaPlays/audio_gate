module;
#include <string>
export module loopback;

import virtual_audio_device;
import pulse_client;

export class loopback
    : public virtual_audio_device
{
public:
    loopback(pulse_client& client) noexcept
        : virtual_audio_device{ client }
    {}

public:
    void create(const std::string& source, const std::string& sink);
    bool set_target_sink(const std::string& new_sink_name);

protected:

private:
};