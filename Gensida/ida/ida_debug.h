#pragma once

#include <ida.hpp>
#include <idp.hpp>

#include "ida_plugin.h"

DECLARE_LISTENER(idd_listener_t, plugin_ctx_t, ctx);

// Stop callbacks before the plugin context and its IDA listeners disappear.
void shutdown_debugger();

