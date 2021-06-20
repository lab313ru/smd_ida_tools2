#include "m68k_debugwindow.h"
#include "mem_m68k.h"
#include "star_68k.h"
#include "resource.h"

extern bool handled_ida_event;

M68kDebugWindow M68kDW;

M68kDebugWindow::M68kDebugWindow()
{
  last_pc = 0;
}

M68kDebugWindow::~M68kDebugWindow()
{
}

void M68kDebugWindow::TracePC(int pc)
{
#ifdef DEBUG_68K
    handled_ida_event = false;

    if (last_pc != 0 && pc != 0 && pc < MAX_ROM_SIZE) {
      changed[pc] = last_pc;
    }

    last_pc = pc;

    bool br = false;
    if (StepInto || StepOver == pc)
    {
        br = true;

        send_pause_event(pc, changed);
        changed.clear();

        handled_ida_event = true;
    }

    if (!br)
    {
        br = BreakPC(last_pc);
    }

    if (br)
    {
        StepInto = false;
        StepOver = -1;
        Breakpoint(last_pc);
    }

    unsigned short opc = M68K_RW(last_pc);

    if ((opc & 0xFFC0) == 0x4E80 || //jsr
        (opc & 0xFF00) == 0x6100) //bsr
    {
        callstack.push_back(last_pc);
    }

    if ((opc & 0xFFFF) == 0x4E75 || //rts
        (opc & 0xFFFF) == 0x4E73) //rte
    {
        if (!callstack.empty())
            callstack.pop_back();
    }
#endif
}

void M68kDebugWindow::TraceRead(uint32 start, uint32 stop, bool is_vdp)
{
#ifdef DEBUG_68K
    handled_ida_event = false;
    if (BreakRead(last_pc, start, stop, is_vdp))
    {
        Breakpoint(last_pc);
    }
#endif
}

void M68kDebugWindow::TraceWrite(uint32 start, uint32 stop, bool is_vdp)
{
#ifdef DEBUG_68K
    handled_ida_event = false;
    if (BreakWrite(last_pc, start, stop, is_vdp))
    {
        Breakpoint(last_pc);
    }
#endif
}

void M68kDebugWindow::DoStepOver()
{
#ifdef DEBUG_68K
    unsigned short opc = M68K_RW(last_pc);
    if ((opc & 0xFFC0) == 0x4E80) // jsr
    {
        ushort ext;
        int bd, od, reg, mod, off = 1;

        mod = (opc & 0x38) >> 3;
        reg = (opc & 0x07) >> 0;

        switch (mod) {
        case 5: off++; break;
        case 7:
        {
            if (reg >= 0 && reg <= 2)
            {
                switch (reg)
                {
                case 0: off++; break;
                case 1: off += 2; break;
                case 2: off++; break;
                }
                break;
            }
        }
        case 6:
        {
            ext = M68K_RW(last_pc + 2);
            off++;

            if (ext & 0x80) {
                /* either base disp, or memory indirect */
                bd = (ext & 0x30) >> 4;
                od = (ext & 0x03) >> 0;

                switch (bd)
                {
                case 2: off++; break;
                default: off += 2; break;
                }

                switch (od)
                {
                case 2: off++; break;
                case 3: off += 2; break;
                }
            }
        } break;
        }

        StepOver = last_pc + off * 2;
    }
    else if ((opc & 0xFF00) == 0x6100) // bsr
    {
        int off = 1;

        switch (opc & 0xFF)
        {
        case 0x00: off++; break;
        case 0xFF: off += 2; break;
        }

        StepOver = last_pc + off * 2;
    }
    else
    {
        StepInto = true;
        StepOver = -1;
    }
#endif
}