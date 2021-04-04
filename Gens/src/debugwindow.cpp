#include "gen-cpp/DbgClient.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

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

extern ::std::shared_ptr<DbgClientClient> client;

bool handled_ida_event;

void Handle_Gens_Messages();
LRESULT CALLBACK EditBreakProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern int Gens_Running;
extern "C" int Clear_Sound_Buffer(void);

DebugWindow::DebugWindow()
{
    DebugStop = false;
    HWnd = NULL;
    DLGPROC DebugProc = NULL;

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
        try {
          if (client) {
            client->pause_event(pc, changed);
            changed.clear();
          }
        }
        catch (TException& ex) {

        }
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

void DebugWindow::SetWhyBreak(LPCSTR lpString)
{
    std::cout << lpString << std::endl;
}

bool DebugWindow::BreakPC(int pc)
{
    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;
        if (!(i->enabled)) continue;

        if (pc <= (int)(i->end) && pc >= (int)(i->start))
        {
            return !(i->is_forbid);
        }
    }
    return false;
}

bool DebugWindow::BreakRead(int pc, uint32 start, uint32 stop, bool is_vdp)
{
    bool brk = false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_READ) continue;
        if (!(i->enabled) || (i->is_vdp != is_vdp)) continue;

        if (start <= i->end && stop >= i->start)
        {
            brk = !(i->is_forbid);
            break;
        }
    }

    if (!brk) return false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;

        if (i->enabled && i->is_forbid)
        {
            if (pc <= (int)(i->end) && pc >= (int)(i->start))
                return false;
        }
    }

    return true;
}

bool DebugWindow::BreakWrite(int pc, uint32 start, uint32 stop, bool is_vdp)
{
    bool brk = false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_WRITE) continue;
        if (!(i->enabled) || (i->is_vdp != is_vdp)) continue;

        if (start <= i->end && stop >= i->start)
        {
            brk = !(i->is_forbid);
            break;
        }
    }

    if (!brk) return false;

    for (auto i = Breakpoints.cbegin(); i != Breakpoints.cend(); ++i)
    {
        if (i->type != bp_type::BP_PC) continue;

        if (i->enabled && i->is_forbid)
        {
            if (pc <= (int)(i->end) && pc >= (int)(i->start))
                return false;
        }
    }

    return true;
}