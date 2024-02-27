#include "resource.h"
#include "gens.h"
#include "save.h"
#include "g_main.h"
#include "ramwatch.h"
#include "debugwindow.h"
#include "g_ddraw.h"
#include "star_68k.h"
#include "vdp_io.h"
#include <vector>
#include <iostream>

bool handled_ida_event;

void Handle_Gens_Messages();
extern int Gens_Running;
extern "C" int Clear_Sound_Buffer(void);

DebugWindow::DebugWindow()
{
    DebugStop = false;
    HWnd = NULL;

    StepInto = false;
    StepOver = -1;
}

DebugWindow::~DebugWindow()
{
}

void DebugWindow::TracePC(int pc) {}
void DebugWindow::TraceRead(uint32 start, uint32 stop) {}
void DebugWindow::TraceWrite(uint32 start, uint32 stop) {}
void DebugWindow::DoStepOver() {}

void DebugWindow::Breakpoint(int pc)
{
    if (!handled_ida_event)
    {
      send_pause_event(pc, changed);
      changed.clear();
    }

    Show_Genesis_Screen(HWnd);
    Update_RAM_Watch();
    Clear_Sound_Buffer();

    if (!DebugStop)
    {
        DebugStop = true;
        MSG msg = { 0 };
        for (; Gens_Running && DebugStop;)
        {
            Handle_Gens_Messages();
        }
        //DebugDummyHWnd=(HWND)0;
    }
}

bool DebugWindow::BreakPC(int pc)
{
    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;
        if (!(i->enabled)) continue;

        if (pc <= (int)(i->end) && pc >= (int)(i->start))
        {
            return evaluate_condition(i->elang, i->condition.c_str());
        }
    }
    return false;
}

#ifdef DEBUG_68K
bool DebugWindow::BreakRead(int pc, uint32 start, uint32 stop, bool is_vdp)
#else
bool DebugWindow::BreakRead(int pc, uint32 start, uint32 stop)
#endif
{
    bool brk = false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_READ) continue;
        if (!i->enabled) continue;
#ifdef DEBUG_68K
        if (i->is_vdp != is_vdp) continue;
#endif

        if (start <= i->end && stop >= i->start)
        {
            brk = evaluate_condition(i->elang, i->condition.c_str());
            break;
        }
    }

    if (!brk) return false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;

        if (i->enabled) {
            bool ev = evaluate_condition(i->elang, i->condition.c_str());

            if (!ev) {
                if (pc <= (int)(i->end) && pc >= (int)(i->start))
                    return false;
            }
        }
    }

    return true;
}

#ifdef DEBUG_68K
bool DebugWindow::BreakWrite(int pc, uint32 start, uint32 stop, bool is_vdp)
#else
bool DebugWindow::BreakWrite(int pc, uint32 start, uint32 stop)
#endif
{
    bool brk = false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_WRITE) continue;
        if (!i->enabled) continue;
#ifdef DEBUG_68K
        if (i->is_vdp != is_vdp) continue;
#endif

        if (start <= i->end && stop >= i->start)
        {
            brk = evaluate_condition(i->elang, i->condition.c_str());
            break;
        }
    }

    if (!brk) return false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;

        if (i->enabled) {
            bool ev = evaluate_condition(i->elang, i->condition.c_str());

            if (!ev) {
                if (pc <= (int)(i->end) && pc >= (int)(i->start))
                    return false;
            }
        }
    }

    return true;
}