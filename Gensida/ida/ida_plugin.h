#pragma once

#include <ida.hpp>
#include <idp.hpp>

struct plugin_ctx_t;

#ifdef DEBUG_68K

#define NAME "GensIDA"

DECLARE_LISTENER(ui_listener_t, plugin_ctx_t, ctx);
DECLARE_LISTENER(dis_listener_t, plugin_ctx_t, ctx);
DECLARE_LISTENER(asm_listener_t, plugin_ctx_t, ctx);
DECLARE_LISTENER(dbg_listener_t, plugin_ctx_t, ctx);
DECLARE_LISTENER(view_listener_t, plugin_ctx_t, ctx);
#else

#define NAME "Zida80"

#endif

#define VERSION "2.00"

