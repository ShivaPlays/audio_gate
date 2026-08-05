module;
#include <pulse/pulseaudio.h>
#include <iostream>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <stdexcept>
module pulse_client;

bool pulse_client::ensure_connected()
{
    // 1. FAST PATH: Single atomic acquire read (sub-nanosecond, no mutex lock).
    if (m_connected.load(std::memory_order_acquire)) return true;

    // 2. SLOW PATH: Lock mutex so only ONE thread executes the connection sequence.
    std::scoped_lock lock(m_init_mutex);

    // Double-check flag in case another thread connected while we waited for the lock.
    if (m_connected.load(std::memory_order_relaxed)) return true;

    bool success = connect_internal();

    // Store connection status with release semantics so all threads see initialized pointers
    m_connected.store(success, std::memory_order_release);
    return success;
}

void pulse_client::disconnect()
{
    std::scoped_lock lock(m_init_mutex);

    m_connected.store(false, std::memory_order_release);

    // If we have an active thread loop, manipulate the context INSIDE the lock first
    if (m_mainloop)
    {
        pa_threaded_mainloop_lock(m_mainloop);

        if (m_context)
        {
            // Unregister state callback so background thread doesn't fire stale signals
            pa_context_set_state_callback(m_context, nullptr, nullptr);
            pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
            pa_context_disconnect(m_context);
            pa_context_unref(m_context);
            m_context = nullptr;
        }

        pa_threaded_mainloop_unlock(m_mainloop);

        // NOW it is safe to stop the background thread and free the loop
        pa_threaded_mainloop_stop(m_mainloop);
        pa_threaded_mainloop_free(m_mainloop);
        m_mainloop = nullptr;
        m_api = nullptr;
    }
    else if (m_context)
    {
        // Fallback: If mainloop failed to start, just unref context directly
        pa_context_unref(m_context);
        m_context = nullptr;
    }

    std::scoped_lock cache_lock(m_cache_mutex);
    m_device_cache.clear();
}

bool pulse_client::connect_internal()
{
    // 1. Instantiate the threaded mainloop (spawns background thread for PulseAudio)
    m_mainloop = pa_threaded_mainloop_new();
    if (!m_mainloop)
    {
        std::cerr << "[PulseClient] Failed to create PulseAudio threaded mainloop.\n";
        return false;
    }

    // 2. Extract API abstraction pointer
    m_api = pa_threaded_mainloop_get_api(m_mainloop);
    m_context = pa_context_new(m_api, "AudioStudioController");
    if (!m_context)
    {
        std::cerr << "[PulseClient] Failed to create PulseAudio context.\n";
        disconnect();
        return false;
    }

    // 3. Register state change callback and event subscription callback
    pa_context_set_state_callback(m_context, context_state_callback, this);
    pa_context_set_subscribe_callback(m_context, subscribe_callback, this);

    // 4. Start the background thread processing loop
    if (pa_threaded_mainloop_start(m_mainloop) < 0)
    {
        std::cerr << "[PulseClient] Failed to start PulseAudio mainloop thread.\n";
        disconnect();
        return false;
    }

    // 5. Lock thread loop and initiate context connection
    pa_threaded_mainloop_lock(m_mainloop);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
    {
        std::cerr << "[PulseClient] Failed to initiate context connection: "
                  << pa_strerror(pa_context_errno(m_context)) << "\n";
        pa_threaded_mainloop_unlock(m_mainloop);

        disconnect();
        return false;
    }

    // 6. Block calling thread until PulseAudio notifies us it is READY or FAILED
    while (true)
    {
        pa_context_state_t state = pa_context_get_state(m_context);

        if (state == PA_CONTEXT_READY)
        {
            // Include PA_SUBSCRIPTION_MASK_SERVER to catch default sink change events
            auto mask = static_cast<pa_subscription_mask_t>(
                PA_SUBSCRIPTION_MASK_SINK |
                PA_SUBSCRIPTION_MASK_SOURCE |
                PA_SUBSCRIPTION_MASK_SERVER
            );

            pa_operation* op = pa_context_subscribe(m_context, mask, nullptr, nullptr);
            if (op)
            {
                pa_operation_unref(op);
            }

            fetch_initial_devices();

            break; // Connection succeeded!
        }

        if (!PA_CONTEXT_IS_GOOD(state))
        {
            std::cerr << "[PulseClient] Connection failed with state: " << state << "\n";
            pa_threaded_mainloop_unlock(m_mainloop);

            disconnect();
            return false;
        }

        // Wait on conditional variable. Releases lock and sleeps until signaled by callback!
        pa_threaded_mainloop_wait(m_mainloop);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
    return true;
}

void pulse_client::fetch_initial_devices()
{
    {
        std::scoped_lock lock(m_cache_mutex);
        m_device_cache.reserve(32);
    }

    struct fetch_context
    {
        pulse_client* client{};
        bool sinks_done{ false };
        bool sources_done{ false };
    };

    fetch_context fctx{ this, false, false };

    // 1. Fetch Sinks
    pa_operation* sink_op = pa_context_get_sink_info_list(
        m_context,
        [](pa_context*, const pa_sink_info* i, int eol, void* userdata) {
            auto* ctx = static_cast<fetch_context*>(userdata);
            if (eol != 0)
            {
                ctx->sinks_done = true;
                pa_threaded_mainloop_signal(ctx->client->m_mainloop, 0);
                return;
            }
            if (i)
            {
                std::scoped_lock lock(ctx->client->m_cache_mutex);

                // Avoid overwriting/duplicating if a subscribe event already captured it
                auto& cache = ctx->client->m_device_cache;
                auto it = std::find_if(cache.begin(), cache.end(), [idx = i->index](const device_info& dev) {
                    return dev.index == idx && dev.type == device_type::sink;
                });

                if (it == cache.end())
                {
                    cache.push_back({
                        i->index,
                        i->name ? i->name : "",
                        i->description ? i->description : "",
                        device_type::sink
                    });
                }
            }
        },
        &fctx
    );

    // 2. Fetch Sources
    pa_operation* source_op = pa_context_get_source_info_list(
        m_context,
        [](pa_context*, const pa_source_info* i, int eol, void* userdata) {
            auto* ctx = static_cast<fetch_context*>(userdata);
            if (eol != 0)
            {
                ctx->sources_done = true;
                pa_threaded_mainloop_signal(ctx->client->m_mainloop, 0);
                return;
            }
            if (i)
            {
                std::scoped_lock lock(ctx->client->m_cache_mutex);

                auto& cache = ctx->client->m_device_cache;
                auto it = std::find_if(cache.begin(), cache.end(), [idx = i->index](const device_info& dev) {
                    return dev.index == idx && dev.type == device_type::source;
                });

                if (it == cache.end())
                {
                    cache.push_back({
                        i->index,
                        i->name ? i->name : "",
                        i->description ? i->description : "",
                        device_type::source
                    });
                }
            }
        },
        &fctx
    );

    if (sink_op && source_op)
    {
        while (!fctx.sinks_done || !fctx.sources_done)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
    }

    if (sink_op) pa_operation_unref(sink_op);
    if (source_op) pa_operation_unref(source_op);
}

void pulse_client::process_device_update(device_event_type event_type, const device_info& info)
{
    device_info dispatched_info = info;

    {
        std::scoped_lock lock(m_cache_mutex);

        auto it = std::find_if(m_device_cache.begin(), m_device_cache.end(),
            [index = info.index, type = info.type](const device_info& item) {
                return item.index == index && item.type == type;
            }
        );

        if (event_type == device_event_type::added || event_type == device_event_type::changed)
        {
            if (it != m_device_cache.end())
            {
                *it = info;
            }
            else
            {
                m_device_cache.push_back(info);
            }
        }
        else if (event_type == device_event_type::removed)
        {
            if (it != m_device_cache.end())
            {
                dispatched_info = *it; // Retain name and description for removal notification
                m_device_cache.erase(it);
            }
        }
    }

    if (m_device_callback)
    {
        m_device_callback(event_type, dispatched_info);
    }
}

std::vector<pulse_client::device_info> pulse_client::get_devices() const
{
    std::scoped_lock lock(m_cache_mutex);
    return m_device_cache;
}

bool pulse_client::device_exists(const std::string& device_name)
{
    if (device_name.empty() || !ensure_connected() || !m_context || !m_mainloop)
    {
        return false;
    }

    struct search_state
    {
        std::string target_name;
        pa_threaded_mainloop* mainloop{nullptr};
        bool found{false};
        bool sink_done{false};
        bool source_done{false};
    } state{ device_name, m_mainloop, false, false, false };

    pa_threaded_mainloop_lock(m_mainloop);

    if (pa_context_get_state(m_context) != PA_CONTEXT_READY)
    {
        pa_threaded_mainloop_unlock(m_mainloop);
        return false;
    }

    // Query both Sinks (playback) and Sources (recording/monitors) directly from PulseAudio server
    pa_operation* op_sink = pa_context_get_sink_info_list(
        m_context,
        [](pa_context*, const pa_sink_info* info, int eol, void* userdata) {
            auto* s = static_cast<search_state*>(userdata);
            if (eol > 0)
            {
                s->sink_done = true;
            }
            else if (info && info->name && s->target_name == info->name)
            {
                s->found = true;
                s->sink_done = true;
            }
            pa_threaded_mainloop_signal(s->mainloop, 0);
        },
        &state
    );

    pa_operation* op_source = pa_context_get_source_info_list(
        m_context,
        [](pa_context*, const pa_source_info* info, int eol, void* userdata) {
            auto* s = static_cast<search_state*>(userdata);
            if (eol > 0) {
                s->source_done = true;
            }
            else if (info && info->name && s->target_name == info->name)
            {
                s->found = true;
                s->source_done = true;
            }
            pa_threaded_mainloop_signal(s->mainloop, 0);
        },
        &state
    );

    if (!op_sink || !op_source)
    {
        if (op_sink) pa_operation_unref(op_sink);
        if (op_source) pa_operation_unref(op_source);
        pa_threaded_mainloop_unlock(m_mainloop);
        return false;
    }

    // Wait until found or both operations complete
    while (!state.found && (!state.sink_done || !state.source_done))
    {
        pa_threaded_mainloop_wait(m_mainloop);
    }

    pa_operation_unref(op_sink);
    pa_operation_unref(op_source);
    pa_threaded_mainloop_unlock(m_mainloop);

    return state.found;
}

bool pulse_client::reconnect()
{
    disconnect();
    return ensure_connected();
}

uint32_t pulse_client::load_module(const std::string& name, const std::string& args)
{
    if (!ensure_connected()) throw std::runtime_error{ "[PulseClient] Cannot load module: client not connected." };

    pa_threaded_mainloop_lock(m_mainloop);

    uint32_t module_index = PA_INVALID_INDEX;
    bool completed = false;

    struct load_context
    {
        uint32_t* index;
        bool* done;
        pa_threaded_mainloop* mainloop;
    };
    load_context ctx{&module_index, &completed, m_mainloop};

    pa_operation* op = pa_context_load_module(
        m_context, name.c_str(), args.c_str(),
        [](pa_context*, uint32_t idx, void* userdata) {
            auto* c = static_cast<load_context*>(userdata);
            *c->index = idx;
            *c->done = true;
            pa_threaded_mainloop_signal(c->mainloop, 0);
        },
        &ctx
    );

    if (!op)
    {
        pa_threaded_mainloop_unlock(m_mainloop);

        throw std::runtime_error{ "[PulseClient] Failed to load module operation." };
    }

    while (!completed)
    {
        pa_threaded_mainloop_wait(m_mainloop);
    }

    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(m_mainloop);

    if (module_index == PA_INVALID_INDEX) throw std::runtime_error{ "[PulseClient] Failed to load module." };

    return module_index;
}

void pulse_client::unload_module(uint32_t module_index)
{
    if (!ensure_connected()) return;

    pa_threaded_mainloop_lock(m_mainloop);
    bool completed = false;

    struct unload_context
    {
        bool* done;
        pa_threaded_mainloop* mainloop;
    };
    unload_context ctx{&completed, m_mainloop};

    pa_operation* op = pa_context_unload_module(
        m_context, module_index,
        [](pa_context*, int success, void* userdata) {
            auto* c = static_cast<unload_context*>(userdata);
            *c->done = true;
            pa_threaded_mainloop_signal(c->mainloop, 0);
        },
        &ctx
    );

    if (op)
    {
        while (!completed)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
}

void pulse_client::set_default_sink(const std::string& sink_name)
{
    if (!ensure_connected() || sink_name.empty()) return;

    pa_threaded_mainloop_lock(m_mainloop);

    bool completed = false;
    struct default_sink_context
    {
        bool* done;
        pa_threaded_mainloop* mainloop;
    };
    default_sink_context ctx{ &completed, m_mainloop };

    pa_operation* op = pa_context_set_default_sink(
        m_context,
        sink_name.c_str(),
        [](pa_context*, int success, void* userdata) {
            auto* c = static_cast<default_sink_context*>(userdata);
            *c->done = true;
            pa_threaded_mainloop_signal(c->mainloop, 0);
        },
        &ctx
    );

    if (op)
    {
        while (!completed)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
}

std::string pulse_client::get_default_sink()
{
    constexpr std::string_view DEFAULT_NAME = "@DEFAULT_SINK@";

    if (!ensure_connected()) return std::string{ DEFAULT_NAME };

    struct server_info_context
    {
        pulse_client* client;
        std::string default_sink_name;
        bool done{ false };
    };

    server_info_context sctx{ this, "", false };

    pa_operation* op = pa_context_get_server_info(
        m_context,
        [](pa_context*, const pa_server_info* i, void* userdata) {
            auto* ctx = static_cast<server_info_context*>(userdata);
            if (i && i->default_sink_name)
            {
                ctx->default_sink_name = i->default_sink_name;
            }
            ctx->done = true;
            pa_threaded_mainloop_signal(ctx->client->m_mainloop, 0);
        },
        &sctx
    );

    if (op)
    {
        while (!sctx.done)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(op);
    }

    // Fallback if empty, just like your bash script (@DEFAULT_SINK@)
    return sctx.default_sink_name.empty() ? std::string{ DEFAULT_NAME } : sctx.default_sink_name;
}

void pulse_client::evict_streams_from_sink(const std::string& source_sink_name, const std::string& target_sink_name)
{
    if (!ensure_connected() || source_sink_name.empty() || target_sink_name.empty()) return;

    pa_threaded_mainloop_lock(m_mainloop);

    // 1. Resolve numerical index of source sink (e.g., "System_Audio_Bridge")
    uint32_t source_sink_idx = PA_INVALID_INDEX;
    bool sink_query_done = false;

    struct sink_ctx
    {
        uint32_t* idx;
        bool* done;
        pa_threaded_mainloop* mainloop;
    } s_ctx{ &source_sink_idx, &sink_query_done, m_mainloop };

    pa_operation* sink_op = pa_context_get_sink_info_by_name(
        m_context,
        source_sink_name.c_str(),
        [](pa_context*, const pa_sink_info* i, int eol, void* userdata) noexcept {
            auto* ctx = static_cast<sink_ctx*>(userdata);
            if (eol != 0) {
                *ctx->done = true;
                pa_threaded_mainloop_signal(ctx->mainloop, 0);
                return;
            }
            if (i) *ctx->idx = i->index;
        },
        &s_ctx
    );

    if (sink_op)
    {
        while (!sink_query_done) pa_threaded_mainloop_wait(m_mainloop);
        pa_operation_unref(sink_op);
    }

    if (source_sink_idx == PA_INVALID_INDEX)
    {
        pa_threaded_mainloop_unlock(m_mainloop);
        return;
    }

    // 2. Direct eviction state wrapper
    struct evict_ctx
    {
        uint32_t target_source_idx;
        const char* target_sink;
        bool done{ false };
        pa_threaded_mainloop* mainloop;
    } e_ctx{ source_sink_idx, target_sink_name.c_str(), false, m_mainloop };

    pa_operation* op = pa_context_get_sink_input_info_list(
        m_context,
        [](pa_context* c, const pa_sink_input_info* i, int eol, void* userdata) noexcept {
            auto* ctx = static_cast<evict_ctx*>(userdata);
            if (eol != 0) {
                ctx->done = true;
                pa_threaded_mainloop_signal(ctx->mainloop, 0);
                return;
            }

            // If the stream is attached to source_sink, move it immediately!
            if (i && i->sink == ctx->target_source_idx)
            {
                pa_operation* move_op = pa_context_move_sink_input_by_name(
                    c,
                    i->index,
                    ctx->target_sink,
                    nullptr,
                    nullptr
                );
                if (move_op) pa_operation_unref(move_op);
            }
        },
        &e_ctx
    );

    if (op)
    {
        while (!e_ctx.done) pa_threaded_mainloop_wait(m_mainloop);
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
}

bool pulse_client::move_sink_input_to_sink(uint32_t sink_input_idx, const std::string& new_sink_name)
{
    if (!ensure_connected()) return false;

    pa_threaded_mainloop_lock(m_mainloop);
    bool completed = false;
    bool success_flag = false;

    struct move_context
    {
        bool* done;
        bool* success;
        pa_threaded_mainloop* mainloop;
    };
    move_context ctx{&completed, &success_flag, m_mainloop};

    pa_operation* op = pa_context_move_sink_input_by_name(
        m_context,
        sink_input_idx,
        new_sink_name.c_str(),
        [](pa_context*, int success, void* userdata) {
            auto* c = static_cast<move_context*>(userdata);
            *c->success = (success != 0);
            *c->done = true;
            pa_threaded_mainloop_signal(c->mainloop, 0);
        },
        &ctx
    );

    if (op)
    {
        while (!completed)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
    return success_flag;
}

uint32_t pulse_client::get_sink_input_for_module(uint32_t module_index)
{
    if (!ensure_connected() || module_index == PA_INVALID_INDEX) return PA_INVALID_INDEX;

    pa_threaded_mainloop_lock(m_mainloop);

    struct search_context
    {
        uint32_t target_module{};
        uint32_t found_sink_input_idx{ PA_INVALID_INDEX };
        bool done{ false };
        pa_threaded_mainloop* mainloop{};
    };

    search_context sctx{ module_index, PA_INVALID_INDEX, false, m_mainloop };

    pa_operation* op = pa_context_get_sink_input_info_list(
        m_context,
        [](pa_context*, const pa_sink_input_info* i, int eol, void* userdata) {
            auto* ctx = static_cast<search_context*>(userdata);

            // Correct EOL handling: non-zero means list enumeration ended or failed
            if (eol != 0)
            {
                ctx->done = true;
                pa_threaded_mainloop_signal(ctx->mainloop, 0);
                return;
            }

            if (i && i->owner_module == ctx->target_module)
            {
                ctx->found_sink_input_idx = i->index;
            }
        },
        &sctx
    );

    if (op)
    {
        while (!sctx.done)
        {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(m_mainloop);
    return sctx.found_sink_input_idx;
}

void pulse_client::context_state_callback(pa_context* c, void* userdata)
{
    auto* self = static_cast<pulse_client*>(userdata);
    if (!self || !self->m_mainloop) return;

    // PulseAudio thread automatically holds mainloop lock when invoking callbacks.
    // Signaling wakes up threads blocked on pa_threaded_mainloop_wait().
    pa_threaded_mainloop_signal(self->m_mainloop, 0);
}

void pulse_client::subscribe_callback(pa_context* c, pa_subscription_event_type_t t, uint32_t idx, void* userdata)
{
    auto* self = static_cast<pulse_client*>(userdata);
    if (!self) return;

    // Extract Event Action (New, Remove, Change)
    auto facility = static_cast<pa_subscription_event_type_t>(t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
    auto type     = static_cast<pa_subscription_event_type_t>(t & PA_SUBSCRIPTION_EVENT_TYPE_MASK);

    device_type dev_type;
    switch (facility)
    {
    case PA_SUBSCRIPTION_EVENT_SINK:   dev_type = device_type::sink; break;
    case PA_SUBSCRIPTION_EVENT_SOURCE: dev_type = device_type::source; break;
    default: return;
    }

    // Handle REMOVED events directly
    if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
    {
        device_info info;
        info.index = idx;
        info.type = dev_type;
        self->process_device_update(device_event_type::removed, info);
        return;
    }

    // For ADDED or CHANGED, query PulseAudio for the device's name & description
    device_event_type event_type = (type == PA_SUBSCRIPTION_EVENT_NEW) ? device_event_type::added : device_event_type::changed;

    // Allocate a small context wrapper for the callback
    auto* qdata = new query_data{ self, event_type, dev_type };

    if (dev_type == device_type::sink)
    {
        pa_operation* op = pa_context_get_sink_info_by_index(c, idx, sink_info_callback, qdata);
        if (op) pa_operation_unref(op);
    }
    else
    {
        pa_operation* op = pa_context_get_source_info_by_index(c, idx, source_info_callback, qdata);
        if (op) pa_operation_unref(op);
    }
}

// Static callback for Sink (Playback) information
void pulse_client::sink_info_callback(pa_context* c, const pa_sink_info* i, int eol, void* userdata)
{
    auto* qdata = static_cast<query_data*>(userdata);

    if (eol == 0 && i)
    {
        device_info info;
        info.index = i->index;
        info.name = i->name ? i->name : "";
        info.description = i->description ? i->description : "";
        info.type = device_type::sink;

        qdata->client->process_device_update(qdata->event_type, info);
    }

    // Free memory when query finishes
    if (eol != 0) delete qdata;
}

// Static callback for Source (Recording / Monitor) information
void pulse_client::source_info_callback(pa_context* c, const pa_source_info* i, int eol, void* userdata)
{
    auto* qdata = static_cast<query_data*>(userdata);

    if (eol == 0 && i)
    {
        device_info info;
        info.index = i->index;
        info.name = i->name ? i->name : "";
        info.description = i->description ? i->description : "";
        info.type = device_type::source;

        qdata->client->process_device_update(qdata->event_type, info);
    }

    // Free memory when query finishes
    if (eol != 0) delete qdata;
}