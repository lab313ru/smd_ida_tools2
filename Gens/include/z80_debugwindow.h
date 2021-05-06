#ifndef Z80_DEBUG_WINDOW_H
#define Z80_DEBUG_WINDOW_H

#include "debugwindow.h"

struct Z80DebugWindow :DebugWindow
{
    Z80DebugWindow();

    uint32 last_pc;

    void DoStepOver();
    void TracePC(int pc);
    void TraceRead(uint32 start, uint32 stop);
    void TraceWrite(uint32 start, uint32 stop);
    ~Z80DebugWindow();
};

extern Z80DebugWindow Z80DW;

#endif
