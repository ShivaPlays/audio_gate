//
// Created by skaldi on 01.08.26.
//

module;
#include <format>
#include <string>
#include <iostream>
#include <pulse/pulseaudio.h>
module virtual_audio_device;

void virtual_audio_device::destroy() noexcept
{
    if (m_module_id.has_value()) {
        // Tell pulse_client to unload m_module_id.value()
        m_pulse_client.get().unload_module(m_module_id.value());
        m_module_id.reset();
    }
    m_name.clear();
}