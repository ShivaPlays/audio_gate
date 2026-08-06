#pragma once

#include <QMenu>
#include <QActionGroup>

class QAction;
import pulse_client;

class tray_popup : public QMenu
{
public:
    explicit tray_popup(void* app);

public:
    void add_device(const pulse_client::device_info& device);
    void update_device(const pulse_client::device_info& device);
    void remove_device(const pulse_client::device_info& device);

    void set_current_output_device(std::string device_name);

    void set_mpd_available(bool value) noexcept { if (m_mpc_action) m_mpc_action->setEnabled(value); }

protected:

private:
    void setup_ui();
    void rebuild_device_menu();

    QActionGroup m_device_group{this};

    std::vector<pulse_client::device_info> m_devices;
    std::string m_output_device_name;

    QMenu* m_device_menu{ nullptr };
    QAction* m_mpc_action{nullptr};

    void* m_application{ nullptr };

    bool m_dirty{ true };
    bool m_mpd_available{ false };
};