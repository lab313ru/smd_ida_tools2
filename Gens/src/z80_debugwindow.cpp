#include "gen-cpp/DbgClient.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

#include "z80_debugwindow.h"
#include "mem_z80.h"
#include "resource.h"

extern ::std::shared_ptr<DbgClientClient> client;

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
  Z80DW.TracePC(pc);
}

void __fastcall z80TraceRead(uint32 start, uint32 size)
{
  Z80DW.TraceRead(start, start + size - 1);
}

void __fastcall z80TraceWrite(uint32 start, uint32 size)
{
  Z80DW.TraceWrite(start, start + size - 1);
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

    try {
      if (client) {
        client->pause_event(pc, changed);
        changed.clear();
      }
    }
    catch (...) {

    }

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

  if (((opc & 0xC6) == 0xC4) || ((opc & 0xC7) == 0xC0)) // CALL, RST
  {
    callstack.push_back(last_pc);
  }

  if ((opc & 0xC6) == 0xC0) // ret
  {
    if (!callstack.empty())
      callstack.pop_back();
  }
}

void Z80DebugWindow::TraceRead(uint32 start, uint32 stop)
{
  handled_ida_event = false;
  if (BreakRead(last_pc, start, stop))
  {
    Breakpoint(last_pc);
  }
}

void Z80DebugWindow::TraceWrite(uint32 start, uint32 stop)
{
  handled_ida_event = false;
  if (BreakWrite(last_pc, start, stop))
  {
    Breakpoint(last_pc);
  }
}

void Z80DebugWindow::DoStepOver()
{
  unsigned char opc = Z80_ReadB_Ram(last_pc);
  if ((opc & 0xC6) == 0xC4) // CALL
  {
    StepOver = last_pc + 3;
  }
  else if ((opc & 0xC7) == 0xC0) // RST
  {
    StepOver = last_pc + 1;
  }
  else
  {
    StepInto = true;
    StepOver = -1;
  }
}