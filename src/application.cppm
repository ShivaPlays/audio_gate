module;
#include <string>
#include <memory>

#include <tray_popup_fwd.h>
export module application;

import pulse_client;
import loopback;
import null_sink;
import remap_source;

export class application
{
public:
    application() noexcept;
    ~application();

    application(const application& other) = delete;
    application(application&& other) = delete;

    application& operator=(const application& other) = delete;
    application& operator=(application&& other) = delete;

    void set_output_device(const std::string& device_name);
public:
    int exec(int argc, char* argv[]);

protected:

private:
    void init_pulse();

    std::unique_ptr<tray_popup> m_tray_popup;

    pulse_client m_pulse_client;

    null_sink m_sys_bridge{ m_pulse_client };
    null_sink m_zoom_bridge{ m_pulse_client };
    null_sink m_zoom_mic_backend{ m_pulse_client };

    remap_source m_zoom_mic_source{ m_pulse_client };

    loopback m_sys_loop{ m_pulse_client };
    loopback m_zoom_loop{ m_pulse_client };

    std::string m_default_sink_name{};
};