#include "tray_popup.h"

#include <QAction>
#include <QCoreApplication>
#include <QThread>
#include <QStandardPaths>
#include <QProcess>

import pulse_client;
import application;

tray_popup::tray_popup(void* app)
    : QMenu{ nullptr }
    , m_application{ app }
{
    setup_ui();
}

void tray_popup::add_device(const pulse_client::device_info& device)
{
    auto* app_instance = QCoreApplication::instance();
    if (!app_instance) return; // Prevent segfault during app shutdown

    if (QThread::currentThread() != app_instance->thread())
    {
        QMetaObject::invokeMethod(this, [this, device]() {
            add_device(device);
        }, Qt::QueuedConnection);
        return;
    }

    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [&](const pulse_client::device_info& d) {
            return d.name == device.name;
        });

    if (it == m_devices.end())
    {
        m_devices.push_back(device);
    }
    else
    {
        *it = device;
    }

    m_dirty = true;
}

void tray_popup::update_device(const pulse_client::device_info& device)
{
    auto* app_instance = QCoreApplication::instance();
    if (!app_instance) return; // Prevent segfault during app shutdown

    if (QThread::currentThread() != app_instance->thread())
    {
        QMetaObject::invokeMethod(this, [this, device]() {
            update_device(device);
        }, Qt::QueuedConnection);
        return;
    }

    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [&](const pulse_client::device_info& d) {
            return d.name == device.name;
        });

    if (it != m_devices.end())
    {
        *it = device; // Updates description and any other fields stored in device_info
    }
    else
    {
        // Fallback: If PipeWire updates a device before emitting an add event
        m_devices.push_back(device);
    }

    m_dirty = true;
}

void tray_popup::remove_device(const pulse_client::device_info& device)
{
    auto* app_instance = QCoreApplication::instance();
    if (!app_instance) return; // Prevent segfault during app shutdown

    if (QThread::currentThread() != app_instance->thread())
    {
        QMetaObject::invokeMethod(this, [this, device]() {
            remove_device(device);
        }, Qt::QueuedConnection);
        return;
    }

    std::erase_if(m_devices, [&](const pulse_client::device_info& d) {
        return d.name == device.name;
    });

    m_dirty = true;
}

void tray_popup::set_current_output_device(std::string device_name)
{
    m_output_device_name = std::move(device_name);
}

void tray_popup::setup_ui()
{
    QAction *title_action = addAction("Audio Router Control");
    title_action->setEnabled(false);

    addSeparator();

    // --- Submenu 1: Audio Output Selection ---
    m_device_menu = addMenu("Output Device");
    m_device_group.setExclusive(true);

    connect(m_device_menu, &QMenu::aboutToShow, this, &tray_popup::rebuild_device_menu);

    // --- Quick Toggle: Mute Action ---
    m_mpc_action = addAction("Update MPC");
    connect(m_mpc_action, &QAction::triggered, []()
    {
        QProcess::startDetached("mpc", QStringList() << "update");
    });

    // Check if the 'mpc' binary is installed in system PATH
    QString mpc_path = QStandardPaths::findExecutable("mpc");
    bool mpc_installed = !mpc_path.isEmpty();

    m_mpc_action->setEnabled(mpc_installed);
    if (!mpc_installed) m_mpc_action->setToolTip("mpc is not installed on this system");

    addSeparator();

    // --- Quit Action ---
    QAction *quit_action = addAction("Quit");
    connect(quit_action, &QAction::triggered, []() { QCoreApplication::quit(); });
}

void tray_popup::rebuild_device_menu()
{
    if (!m_dirty) return;

    // Clear old QAction items from the menu and group
    m_device_menu->clear();

    for (const auto& dev : m_devices)
    {
        auto action = m_device_menu->addAction(QString::fromStdString(dev.description));
        action->setCheckable(true);
        action->setData(QString::fromStdString(dev.name));

        // Mark checked if it matches the current active output device
        if (dev.name == m_output_device_name)
        {
            action->setChecked(true);
        }

        // Connect user interaction to application logic
        connect(action, &QAction::triggered, [this, dev]() {
            auto app = static_cast<application*>(m_application);
            app->set_output_device(dev.name);
            m_output_device_name = dev.name;
        });

        m_device_group.addAction(action);
    }

    m_dirty = false;
}