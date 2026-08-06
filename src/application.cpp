module;
#include <QApplication>
#include <QSystemTrayIcon>
#include <QIcon>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <thread>
#include <memory>
#include <chrono>

#include "tray_popup.h"
module application;

application::application() noexcept = default;

application::~application()
{
    if (m_mpd_available) QProcess::startDetached("systemctl", {"--user", "stop", "mpd"});

    // 1. Restore default sink FIRST while virtual devices are still alive
    if (!m_default_sink_name.empty())
    {
        // 1. Forcefully evict streams (YouTube) off System_Audio_Bridge back to hardware headphones
        // This effect sadly is permanent so this solution is not good.
        //m_pulse_client.evict_streams_from_sink("System_Audio_Bridge", m_default_sink_name);

        m_pulse_client.set_default_sink(m_default_sink_name);
        //Wait for the pulse_client to react. Streams will not stop but continue playing (ugly but the only possibility for now)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 2. Unload loopbacks and virtual sinks EXPLICITLY in reverse order of creation
    m_zoom_loop.destroy();
    m_sys_loop.destroy();

    m_zoom_mic_source.destroy();

    m_zoom_mic_backend.destroy();
    m_zoom_bridge.destroy();
    m_sys_bridge.destroy();

    // 3. Disconnect pulse client LAST
    m_pulse_client.disconnect();
}

void application::set_output_device(const std::string& device_name)
{
    m_sys_loop.set_target_sink(device_name);
    m_zoom_loop.set_target_sink(device_name);
}

int application::exec(int argc, char* argv[])
{
    if (!init_pulse()) return 1;

    QApplication app{argc, argv};
    QApplication::setQuitOnLastWindowClosed(false);

    m_mpd_available = !QStandardPaths::findExecutable("mpd").isEmpty();
    if (m_mpd_available)
    {
        QProcess::startDetached("sh", {"-c", "systemctl --user start mpd && mpc update"});
    }

    QSystemTrayIcon tray_icon;
    auto app_icon = QIcon(":/icons/tray_icon.png");
    tray_icon.setIcon(app_icon);
    //tray_icon.setIcon(QIcon::fromTheme("application-x-executable"));
    tray_icon.setToolTip("Audio Router Control");

    QApplication::setWindowIcon(app_icon);

    // 1. Declare tray_popup (derived from QMenu) on the stack
    m_tray_popup = std::make_unique<tray_popup>(this);
    auto result = m_pulse_client.get_devices()
        | std::views::filter([](const pulse_client::device_info& device) {
            constexpr auto reserved_device_names = std::array<std::string_view, 4>
            {
                "System_Audio_Bridge",
                "Zoom_Output_Bridge",
                "Zoom_Virtual_Mic_Backend",
                "Zoom_Virtual_Mic",
            };

            return device.type == pulse_client::device_type::sink && !std::ranges::contains(reserved_device_names, device.name);
        });

    for (auto& device : result) m_tray_popup->add_device(device);
    m_tray_popup->set_current_output_device(m_default_sink_name);
    m_tray_popup->set_mpd_available(m_mpd_available);

    // 2. Set as context menu: Plasma handles positioning and Wayland input grabs natively
    tray_icon.setContextMenu(m_tray_popup.get());

    tray_icon.show();

    return QApplication::exec();
}

bool application::init_pulse()
{
    m_pulse_client.set_device_event_callback([this](pulse_client::device_event_type device_event, const pulse_client::device_info& device_info)
    {
        if (m_tray_popup == nullptr || device_info.type == pulse_client::device_type::source) return;

        constexpr auto reserved_device_names = std::array<std::string_view, 4>
       {
           "System_Audio_Bridge",
           "Zoom_Output_Bridge",
           "Zoom_Virtual_Mic_Backend",
           "Zoom_Virtual_Mic",
       };

        if (std::ranges::contains(reserved_device_names, device_info.name)) return;

        switch (device_event)
        {
        case pulse_client::device_event_type::added:
            m_tray_popup->add_device(device_info);
            break;

        case pulse_client::device_event_type::removed:
            m_tray_popup->remove_device(device_info);
            break;

        case pulse_client::device_event_type::changed:
            m_tray_popup->update_device(device_info);
            break;
        }
    });

    if (m_pulse_client.device_exists("System_Audio_Bridge")) return false;

    m_default_sink_name = m_pulse_client.get_default_sink();

    m_sys_bridge.create("System_Audio_Bridge", "System_Audio_Bridge");
    m_zoom_bridge.create("Zoom_Output_Bridge", "Zoom_Output_Bridge");
    m_zoom_mic_backend.create("Zoom_Virtual_Mic_Backend", "Zoom_Virtual_Mic_Backend");

    m_zoom_mic_source.create("Zoom_Virtual_Mic_Backend.monitor", "Zoom_Virtual_Mic", "Zoom_Virtual_Mic");

    m_sys_loop.create("System_Audio_Bridge.monitor", m_default_sink_name);
    m_zoom_loop.create("Zoom_Output_Bridge.monitor", m_default_sink_name);

    m_pulse_client.set_default_sink("System_Audio_Bridge");

    return true;
}

