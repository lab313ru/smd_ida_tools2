#include "z80_debugwindow.h"
#include "mem_z80.h"
#include "resource.h"

extern bool handled_ida_event;

Z80DebugWindow Z80DW;

Z80DebugWindow::Z80DebugWindow()
{
  last_pc = 0;
}

Z80DebugWindow::~Z80DebugWindow()
{
}

extern "C" {
  void __fastcall z80TracePC(unsigned int pc);
  void __fastcall z80TraceRead(uint32 start, uint32 size);
  void __fastcall z80TraceWrite(uint32 start, uint32 size);
}

void __fastcall z80TracePC(unsigned int pc)
{
#ifdef DEBUG_Z80
  Z80DW.TracePC(pc);
#endif

}

void __fastcall z80TraceRead(uint32 start, uint32 size)
{
#ifdef DEBUG_Z80
  Z80DW.TraceRead(start, start + size - 1);
#endif
}

void __fastcall z80TraceWrite(uint32 start, uint32 size)
{
#ifdef DEBUG_Z80
  Z80DW.TraceWrite(start, start + size - 1);
#endif
}

void Z80DebugWindow::TracePC(int pc)
{
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

  unsigned char opc = Z80_ReadB_Ram(last_pc);

  switch (opc) {
  case 0xCD:
  case 0xDC:
  case 0xFC:
  case 0xD4:
  case 0xC4:
  case 0xF4:
  case 0xEC:
  case 0xE4:
  case 0xCC:
  case 0xC7:
  case 0xCF:
  case 0xD7:
  case 0xDF:
  case 0xE7:
  case 0xEF:
  case 0xF7:
  case 0xFF: // CALL, RST
    callstack.push_back(last_pc);
    break;
  case 0xC9:
  case 0xD8:
  case 0xF8:
  case 0xD0:
  case 0xC0:
  case 0xF0:
  case 0xE8:
  case 0xE0:
  case 0xC8:
    if (!callstack.empty()) {
      callstack.pop_back();
    }
    break;
  case 0xED:
    opc = Z80_ReadB_Ram(last_pc+1);

    if (opc == 0x4D || opc == 0x45) {
      if (!callstack.empty()) {
        callstack.pop_back();
      }
    }
    break;
  }
}

void Z80DebugWindow::TraceRead(uint32 start, uint32 stop)
{
#ifdef DEBUG_Z80
  handled_ida_event = false;
  if (BreakRead(last_pc, start, stop))
  {
    Breakpoint(last_pc);
  }
#endif
}

void Z80DebugWindow::TraceWrite(uint32 start, uint32 stop)
{
#ifdef DEBUG_Z80
  handled_ida_event = false;
  if (BreakWrite(last_pc, start, stop))
  {
    Breakpoint(last_pc);
  }
#endif
}

void Z80DebugWindow::DoStepOver()
{
  unsigned char opc = Z80_ReadB_Ram(last_pc);

  switch (opc) {
  case 0xCD:
  case 0xDC:
  case 0xFC:
  case 0xD4:
  case 0xC4:
  case 0xF4:
  case 0xEC:
  case 0xE4:
  case 0xCC: // CALL
    StepOver = last_pc + 3;
    break;
  case 0xC7:
  case 0xCF:
  case 0xD7:
  case 0xDF:
  case 0xE7:
  case 0xEF:
  case 0xF7:
  case 0xFF: // RST
    StepOver = last_pc + 1;
    break;
  case 0xED:
    opc = Z80_ReadB_Ram(last_pc+1);

    if (opc == 0xB0) { // ldir
      StepOver = last_pc + 2;
      break;
    }
  default:
    StepInto = true;
    StepOver = -1;
  }
}