#include "gen-cpp/DbgClient.h"
#include "gen-cpp/DbgServer.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/server/TNonblockingServer.h>
#include <thrift/transport/TNonblockingServerSocket.h>
#include <thrift/concurrency/ThreadFactory.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;

#include <Windows.h>
#include <algorithm>
#include <ida.hpp>
#include <idd.hpp>
#include <dbg.hpp>
#include <diskio.hpp>
#include <auto.hpp>
#include <funcs.hpp>

#include "ida_debmod.h"

#include "ida_registers.h"
#include "ida_debug.h"
#include "ida_plugin.h"

#include <vector>
#include <iostream>

static ::std::shared_ptr<DbgServerClient> client;
static ::std::shared_ptr<TNonblockingServer> srv;
static ::std::shared_ptr<TTransport> cli_transport;

typedef qvector<std::pair<uint32, bool>> codemap_t;

static ::std::mutex list_mutex;
static eventlist_t events;

#ifdef DEBUG_68K
#define BREAKPOINTS_BASE 0x00D00000
#endif

static const char *const SRReg[] =
{
#ifdef DEBUG_68K
    "C",
    "V",
    "Z",
    "N",
    "X",
    NULL,
    NULL,
    NULL,
    "I",
    "I",
    "I",
    NULL,
    NULL,
    "S",
    NULL,
    "T"
#else
  "C",
  "N",
  "P",
  NULL,
  "H",
  NULL,
  "Z",
  "S",
#endif
};

register_info_t registers[] =
{
#ifdef DEBUG_68K
    { "D0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

    { "A0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

    { "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_dword, NULL, 0 },
    { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },
    { "SR", NULL, RC_GENERAL, dt_word, SRReg, 0xFFFF },

    { "DMA_LEN", REGISTER_READONLY, RC_GENERAL, dt_word, NULL, 0 },
    { "DMA_SRC", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
    { "VDP_DST", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },

    // VDP Registers
    { "Set1", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set2", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "PlaneA", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Window", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "PlaneB", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Sprite", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg6", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "BgClr", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg8", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg9", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "HInt", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set3", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set4", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "HScrl", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg14", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WrInc", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "ScrSz", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WinX", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WinY", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "LenLo", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "LenHi", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcLo", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcMid", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcHi", NULL, RC_VDP, dt_byte, NULL, 0 },
#else
    { "A", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "AF", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "AF'", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "B", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "C", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "BC", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "BC'", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "DE", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "DE'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "HL", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "HL'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

    { "IXH", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IXL", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IX", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "IYH", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IYL", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IY", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "I", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "R", 0, RC_GENERAL, dt_word, NULL, 0 },

    { "PC", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

    { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_word, NULL, 0 },
    { "IP", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_word, NULL, 0 },
    
    { "BANK", 0, RC_GENERAL, dt_dword, NULL, 0 },
#endif
};

static const char *register_classes[] =
{
    "General Registers",
#ifdef DEBUG_68K
    "VDP Registers",
#endif
    NULL
};

struct apply_codemap_req : public exec_request_t {
private:
  const std::map<int32_t, int32_t>& _changed;
public:
  apply_codemap_req(const std::map<int32_t, int32_t>& changed) : _changed(changed) {};

  int idaapi execute(void) override {
    for (auto i = _changed.cbegin(); i != _changed.cend(); ++i) {
      auto_make_code((ea_t)i->first);
      plan_ea((ea_t)i->first);
      show_addr((ea_t)i->first);
    }

    return 0;
  }
};

static void apply_codemap(const std::map<int32_t, int32_t>& changed)
{
  if (changed.empty()) return;

  apply_codemap_req req(changed);
  execute_sync(req, MFF_FAST);
}

static void pause_execution()
{
    try {
      if (client) {
        client->pause();
      }
    } catch (...) {

    }
}

static void continue_execution()
{
    try {
      if (client) {
        client->resume();
      }
    }
    catch (...) {

    }
}

static void finish_execution()
{
    try {
      if (client) {
        client->exit_emulation();
      }
    }
    catch (...) {
      ::std::lock_guard<::std::mutex> lock(list_mutex);

      debug_event_t ev;
      ev.pid = 1;
      ev.handled = true;
      ev.set_exit_code(PROCESS_EXITED, 0);

      events.enqueue(ev, IN_BACK);
    }
}

void stop_server() {
  try {
    srv->stop();
  }
  catch (...) {
    
  }
}

class DbgClientHandler : virtual public DbgClientIf {
public:
  DbgClientHandler() {
    // Your initialization goes here
  }

  void start_event() {
    ::std::lock_guard<::std::mutex> lock(list_mutex);
    
    debug_event_t ev;
    ev.pid = 1;
    ev.tid = 1;
    ev.ea = BADADDR;
    ev.handled = true;

    ev.set_modinfo(PROCESS_STARTED).name.sprnt("GPGX");
    ev.set_modinfo(PROCESS_STARTED).base = 0;
    ev.set_modinfo(PROCESS_STARTED).size = 0;
    ev.set_modinfo(PROCESS_STARTED).rebase_to = BADADDR;

    events.enqueue(ev, IN_BACK);
  }

  void pause_event(const int32_t address, const std::map<int32_t, int32_t>& changed) {
    ::std::lock_guard<::std::mutex> lock(list_mutex);

    debug_event_t ev;
    ev.pid = 1;
    ev.tid = 1;
    ev.ea = address;
    ev.handled = true;
    ev.set_eid(PROCESS_SUSPENDED);
    events.enqueue(ev, IN_BACK);

    apply_codemap(changed);
  }

  void stop_event(const std::map<int32_t, int32_t>& changed) {
    ::std::lock_guard<::std::mutex> lock(list_mutex);

    debug_event_t ev;
    ev.pid = 1;
    ev.handled = true;
    ev.set_exit_code(PROCESS_EXITED, 0);

    apply_codemap(changed);

    events.enqueue(ev, IN_BACK);
  }

};

static void init_ida_server() {
  try {
    ::std::shared_ptr<DbgClientHandler> handler(new DbgClientHandler());
    ::std::shared_ptr<TProcessor> processor(new DbgClientProcessor(handler));
    ::std::shared_ptr<TNonblockingServerTransport> serverTransport(new TNonblockingServerSocket(9091));
    ::std::shared_ptr<TFramedTransportFactory> transportFactory(new TFramedTransportFactory());
    ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    srv = ::std::shared_ptr<TNonblockingServer>(new TNonblockingServer(processor, protocolFactory, serverTransport));
    ::std::shared_ptr<ThreadFactory> tf(new ThreadFactory());
    ::std::shared_ptr<Thread> thread = tf->newThread(srv);
    thread->start();
  }
  catch (...) {

  }
}

static void init_emu_client() {
  ::std::shared_ptr<TTransport> socket(new TSocket("127.0.0.1", 9090));
  cli_transport = ::std::shared_ptr<TTransport>(new TFramedTransport(socket));
  ::std::shared_ptr<TBinaryProtocol> protocol(new TBinaryProtocol(cli_transport));
  client = ::std::shared_ptr<DbgServerClient>(new DbgServerClient(protocol));

  show_wait_box("Waiting for GENS emulation...");

  while (true) {
    if (user_cancelled()) {
      break;
    }

    try {
      cli_transport->open();
      break;
    }
    catch (...) {
      
    }
  }

  hide_wait_box();
}

// Initialize debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi init_debugger(const char* hostname, int portnum, const char* password, qstring *errbuf)
{
#ifdef DEBUG_68K
    set_processor_type(ph.psnames[0], SETPROC_LOADER); // reset proc to "M68000"
#endif
    return DRC_OK;
}

// Terminate debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi term_debugger(void)
{
    finish_execution();
    return DRC_OK;
}

// Return information about the n-th "compatible" running process.
// If n is 0, the processes list is reinitialized.
// 1-ok, 0-failed, -1-network error
// This function is called from the main thread
static drc_t s_get_processes(procinfo_vec_t* procs, qstring* errbuf) {
  process_info_t info;
  info.name.sprnt("gpgx");
  info.pid = 1;
  procs->add(info);

  return DRC_OK;
}

// Start an executable to debug
// 1 - ok, 0 - failed, -2 - file not found (ask for process options)
// 1|CRC32_MISMATCH - ok, but the input file crc does not match
// -1 - network error
// This function is called from debthread
static drc_t idaapi s_start_process(const char* path,
  const char* args,
  const char* startdir,
  uint32 dbg_proc_flags,
  const char* input_path,
  uint32 input_file_crc32,
  qstring* errbuf = NULL)
{
    ::std::lock_guard<::std::mutex> lock(list_mutex);
    events.clear();

    init_ida_server();
    init_emu_client();

    try {
      if (client) {
        client->start_emulation();
      }
    }
    catch (...) {
      return DRC_FAILED;
    }

    return DRC_OK;
}

// rebase database if the debugged program has been rebased by the system
// This function is called from the main thread
static void idaapi rebase_if_required_to(ea_t new_base)
{
}

// Prepare to pause the process
// This function will prepare to pause the process
// Normally the next get_debug_event() will pause the process
// If the process is sleeping then the pause will not occur
// until the process wakes up. The interface should take care of
// this situation.
// If this function is absent, then it won't be possible to pause the program
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi prepare_to_pause_process(qstring *errbuf)
{
    pause_execution();
    return DRC_OK;
}

// Stop the process.
// May be called while the process is running or suspended.
// Must terminate the process in any case.
// The kernel will repeatedly call get_debug_event() and until PROCESS_EXIT.
// In this mode, all other events will be automatically handled and process will be resumed.
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi emul_exit_process(qstring *errbuf)
{
    finish_execution();

    return DRC_OK;
}

// Get a pending debug event and suspend the process
// This function will be called regularly by IDA.
// This function is called from debthread
static gdecode_t idaapi get_debug_event(debug_event_t *event, int timeout_ms)
{
    while (true)
    {
        ::std::lock_guard<::std::mutex> lock(list_mutex);

        // are there any pending events?
        if (events.retrieve(event))
        {
            return events.empty() ? GDE_ONE_EVENT : GDE_MANY_EVENTS;
        }
        if (events.empty())
            break;
    }
    return GDE_NO_EVENT;
}

// Continue after handling the event
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi continue_after_event(const debug_event_t *event)
{
    dbg_notification_t req = get_running_notification();
    switch (event->eid())
    {
    case STEP:
    case PROCESS_SUSPENDED:
        if (req == dbg_null || req == dbg_run_to) {
          continue_execution();
        }
        break;
    case PROCESS_EXITED:
        stop_server();
        break;
    }

    return DRC_OK;
}

// The following function will be called by the kernel each time
// when it has stopped the debugger process for some reason,
// refreshed the database and the screen.
// The debugger module may add information to the database if it wants.
// The reason for introducing this function is that when an event line
// LOAD_DLL happens, the database does not reflect the memory state yet
// and therefore we can't add information about the dll into the database
// in the get_debug_event() function.
// Only when the kernel has adjusted the database we can do it.
// Example: for imported PE DLLs we will add the exported function
// names to the database.
// This function pointer may be absent, i.e. NULL.
// This function is called from the main thread
static void idaapi stopped_at_debug_event(bool dlls_added)
{
}

// The following functions manipulate threads.
// 1-ok, 0-failed, -1-network error
// These functions are called from debthread
static drc_t idaapi s_set_resume_mode(thid_t tid, resume_mode_t resmod) // Run one instruction in the thread
{
    switch (resmod)
    {
    case RESMOD_INTO:    ///< step into call (the most typical single stepping)
        try {
          if (client) {
            client->step_into();
            return DRC_OK;
          }
        }
        catch (...) {
          return DRC_FAILED;
        }

        break;
    case RESMOD_OVER:    ///< step over call
        try {
          if (client) {
            client->step_over();
            return DRC_OK;
          }
        }
        catch (...) {
          return DRC_FAILED;
        }
        break;
    }

    return DRC_FAILED;
}

// Read thread registers
//	tid	- thread id
//	clsmask- bitmask of register classes to read
//	regval - pointer to vector of regvals for all registers
//			 regval is assumed to have debugger_t::registers_size elements
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi read_registers(thid_t tid, int clsmask, regval_t *values, qstring *errbuf)
{
    if (clsmask & RC_GENERAL)
    {
        GpRegisters regs;

        try {
          if (client) {
            client->get_gp_regs(regs);

#ifdef DEBUG_68K
            values[R_D0].ival = regs.D0;
            values[R_D1].ival = regs.D1;
            values[R_D2].ival = regs.D2;
            values[R_D3].ival = regs.D3;
            values[R_D4].ival = regs.D4;
            values[R_D5].ival = regs.D5;
            values[R_D6].ival = regs.D6;
            values[R_D7].ival = regs.D7;

            values[R_A0].ival = regs.A0;
            values[R_A1].ival = regs.A1;
            values[R_A2].ival = regs.A2;
            values[R_A3].ival = regs.A3;
            values[R_A4].ival = regs.A4;
            values[R_A5].ival = regs.A5;
            values[R_A6].ival = regs.A6;
            values[R_A7].ival = regs.A7;

            values[R_PC].ival = regs.PC;
            values[R_SP].ival = regs.SP;
            values[R_SR].ival = regs.SR;
#else
            /*
            { "A", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "AF", 0, RC_GENERAL, dt_word, SRReg, 0xFF },
    { "AF'", 0, RC_GENERAL, dt_word, NULL, 0 },
    { "B", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "C", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "BC", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "BC'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "DE", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "DE'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "HL", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "HL'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

    { "IXH", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IXL", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IX", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "IYH", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IYL", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "IY", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
    { "I", 0, RC_GENERAL, dt_byte, NULL, 0 },
    { "R", 0, RC_GENERAL, dt_word, NULL, 0 },

    { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_word, NULL, 0 },
    { "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_word, NULL, 0 },

    { "M68K_BANK", REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
            */
            values[R_A].ival = (regs.AF >> 8) & 0xFF;
            values[R_AF].ival = regs.AF;
            values[R_AF2].ival = regs.AF2;
            values[R_B].ival = (regs.BC >> 8) & 0xFF;
            values[R_C].ival = (regs.BC >> 0) & 0xFF;
            values[R_BC].ival = regs.BC;
            values[R_BC2].ival = regs.BC2;
            values[R_DE].ival = regs.DE;
            values[R_DE2].ival = regs.DE2;
            values[R_HL].ival = regs.HL;
            values[R_HL2].ival = regs.HL2;

            values[R_IXH].ival = (regs.IX >> 8) & 0xFF;
            values[R_IXL].ival = (regs.IX >> 0) & 0xFF;
            values[R_IX].ival = regs.IX;
            values[R_IYH].ival = (regs.IY >> 8) & 0xFF;
            values[R_IYL].ival = (regs.IY >> 0) & 0xFF;
            values[R_IY].ival = regs.IY;
            values[R_I].ival = regs.I;
            values[R_R].ival = regs.R;
            values[R_PC].ival = regs.PC;

            values[R_SP].ival = regs.SP;
            values[R_IP].ival = regs.IP;

            values[R_BANK].ival = regs.BANK;
#endif
          }
        }
        catch (...) {
          return DRC_FAILED;
        }

#ifdef DEBUG_68K
        DmaInfo dma;

        try {
          if (client) {
            client->get_dma_info(dma);

            values[R_VDP_DMA_LEN].ival = dma.Len;
            values[R_VDP_DMA_SRC].ival = dma.Src;
            values[R_VDP_WRITE_ADDR].ival = dma.Dst;
          }
        }
        catch (...) {
          return DRC_FAILED;
        }
#endif
    }

#ifdef DEBUG_68K
    if (clsmask & RC_VDP)
    {
        VdpRegisters regs;

        try {
          if (client) {
            client->get_vdp_regs(regs);

            values[R_V00].ival = regs.V00;
            values[R_V01].ival = regs.V01;
            values[R_V02].ival = regs.V02;
            values[R_V03].ival = regs.V03;
            values[R_V04].ival = regs.V04;
            values[R_V05].ival = regs.V05;
            values[R_V06].ival = regs.V06;
            values[R_V07].ival = regs.V07;
            values[R_V08].ival = regs.V08;
            values[R_V09].ival = regs.V09;
            values[R_V10].ival = regs.V0A;
            values[R_V11].ival = regs.V0B;
            values[R_V12].ival = regs.V0C;
            values[R_V13].ival = regs.V0D;
            values[R_V14].ival = regs.V0E;
            values[R_V15].ival = regs.V0F;
            values[R_V16].ival = regs.V10;
            values[R_V17].ival = regs.V11;
            values[R_V18].ival = regs.V12;
            values[R_V19].ival = regs.V13;
            values[R_V20].ival = regs.V14;
            values[R_V21].ival = regs.V15;
            values[R_V22].ival = regs.V16;
            values[R_V23].ival = regs.V17;
          }
        }
        catch (...) {
          return DRC_FAILED;
        }
    }
#endif

    return DRC_OK;
}

// Write one thread register
//	tid	- thread id
//	regidx - register index
//	regval - new value of the register
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi write_register(thid_t tid, int regidx, const regval_t *value, qstring *errbuf)
{
#ifdef DEBUG_68K
    if (regidx >= R_D0 && regidx <= R_SR) {
#endif
      GpRegister reg;
      reg.index = (GpRegsEnum::type)regidx;
      reg.value = value->ival & 0xFFFFFFFF;

      try {
        if (client) {
          client->set_gp_reg(reg);
        }
      }
      catch (...) {
        return DRC_FAILED;
      }
#ifdef DEBUG_68K
    }
    else if (regidx >= R_V00 && regidx <= R_V23) {
      VdpRegister reg;
      reg.index = (VdpRegsEnum::type)(regidx - R_V00);
      reg.value = value->ival & 0xFF;

      try {
        if (client) {
          client->set_vdp_reg(reg);
        }
      }
      catch (...) {
        return DRC_FAILED;
      }
    }
#endif

    return DRC_OK;
}

//
// The following functions manipulate bytes in the memory.
//
// Get information on the memory areas
// The debugger module fills 'areas'. The returned vector MUST be sorted.
// Returns:
//   -3: use idb segmentation
//   -2: no changes
//   -1: the process does not exist anymore
//	0: failed
//	1: new memory layout is returned
// This function is called from debthread
static drc_t idaapi get_memory_info(meminfo_vec_t &areas, qstring *errbuf)
{
    areas.clear();

    memory_info_t info;

    // Don't remove this loop
    for (int i = 0; i < get_segm_qty(); ++i)
    {
      segment_t* segm = getnseg(i);

      info.start_ea = segm->start_ea;
      info.end_ea = segm->end_ea;

      qstring buf;
      get_segm_name(&buf, segm);
      info.name = buf;

      get_segm_class(&buf, segm);
      info.sclass = buf;

      info.sbase = 0;
      info.perm = SEGPERM_READ | SEGPERM_WRITE;
      info.bitness = 1;
      areas.push_back(info);
    }
    // Don't remove this loop

#ifdef DEBUG_68K
    info.name = "DBG_VDP_VRAM";
    info.start_ea = BREAKPOINTS_BASE;
    info.end_ea = info.start_ea + 0x10000;
    info.bitness = 1;
    areas.push_back(info);

    info.name = "DBG_VDP_CRAM";
    info.start_ea = info.end_ea;
    info.end_ea = info.start_ea + 0x10000;
    info.bitness = 1;
    areas.push_back(info);

    info.name = "DBG_VDP_VSRAM";
    info.start_ea = info.end_ea;
    info.end_ea = info.start_ea + 0x10000;
    info.bitness = 1;
    areas.push_back(info);
#endif

    return DRC_OK;
}

// Read process memory
// Returns number of read bytes
// 0 means read error
// -1 means that the process does not exist anymore
// This function is called from debthread
static ssize_t idaapi read_memory(ea_t ea, void *buffer, size_t size, qstring *errbuf)
{
    std::string mem;

    try {
      if (client) {
        client->read_memory(mem, (int32_t)ea, (int32_t)size);

        memcpy(&((unsigned char*)buffer)[0], mem.c_str(), size);
      }
    }
    catch (...) {
      return 0;
    }

    return size;
}

// Write process memory
// Returns number of written bytes, -1-fatal error
// This function is called from debthread
static ssize_t idaapi write_memory(ea_t ea, const void *buffer, size_t size, qstring *errbuf)
{
    std::string mem((const char*)buffer);

    try {
      if (client) {
        client->write_memory((int32_t)ea, mem);
      }
    }
    catch (...) {
      return 0;
    }

    return size;
}

// Is it possible to set breakpoint?
// Returns: BPT_...
// This function is called from debthread or from the main thread if debthread
// is not running yet.
// It is called to verify hardware breakpoints.
static int idaapi is_ok_bpt(bpttype_t type, ea_t ea, int len)
{
    switch (type)
    {
        //case BPT_SOFT:
    case BPT_EXEC:
    case BPT_READ: // there is no such constant in sdk61
    case BPT_WRITE:
    case BPT_RDWR:
        return BPT_OK;
    }

    return BPT_BAD_TYPE;
}

// Add/del breakpoints.
// bpts array contains nadd bpts to add, followed by ndel bpts to del.
// returns number of successfully modified bpts, -1-network error
// This function is called from debthread
static drc_t idaapi update_bpts(int* nbpts, update_bpt_info_t *bpts, int nadd, int ndel, qstring *errbuf)
{
    for (int i = 0; i < nadd; ++i)
    {
        ea_t start = bpts[i].ea;
        ea_t end = bpts[i].ea + bpts[i].size - 1;

        BpType::type type1;
        int type2 = 0;
#ifdef DEBUG_68K
        bool is_vdp = false;
#endif

        switch (bpts[i].type)
        {
        case BPT_EXEC:
            type1 = BpType::BP_PC;
            break;
        case BPT_READ:
            type1 = BpType::BP_READ;
            break;
        case BPT_WRITE:
            type1 = BpType::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = BpType::BP_READ;
            type2 = (int)BpType::BP_WRITE;
            break;
        }

#ifdef DEBUG_68K
        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }
#endif

        DbgBreakpoint bp;
        bp.bstart = start & 0xFFFFFF;
        bp.bend = end & 0xFFFFFF;
        bp.type = type1;

        bp.enabled = true;
#ifdef DEBUG_68K
        bp.is_vdp = is_vdp;
#endif
        bp.is_forbid = false;

        try {
          if (client) {
            client->add_breakpoint(bp);
          }
        }
        catch (...) {
          return DRC_FAILED;
        }

        if (type2 != 0)
        {
            bp.type = (BpType::type)type2;

            try {
              if (client) {
                client->add_breakpoint(bp);
              }
            }
            catch (...) {
              return DRC_FAILED;
            }
        }

        bpts[i].code = BPT_OK;
    }

    for (int i = 0; i < ndel; ++i)
    {
        ea_t start = bpts[nadd + i].ea;
        ea_t end = bpts[nadd + i].ea + bpts[nadd + i].size - 1;
        BpType::type type1;
        int type2 = 0;
#ifdef DEBUG_68K
        bool is_vdp = false;
#endif

        switch (bpts[nadd + i].type)
        {
        case BPT_EXEC:
            type1 = BpType::BP_PC;
            break;
        case BPT_READ:
            type1 = BpType::BP_READ;
            break;
        case BPT_WRITE:
            type1 = BpType::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = BpType::BP_READ;
            type2 = (int)BpType::BP_WRITE;
            break;
        }

#ifdef DEBUG_68K
        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }
#endif

        DbgBreakpoint bp;
        bp.bstart = start & 0xFFFFFF;
        bp.bend = end & 0xFFFFFF;
        bp.type = type1;

        bp.enabled = true;
#ifdef DEBUG_68K
        bp.is_vdp = is_vdp;
#endif
        bp.is_forbid = false;

        try {
          if (client) {
            client->del_breakpoint(bp);
          }
        }
        catch (...) {
          return DRC_FAILED;
        }

        if (type2 != 0)
        {
            bp.type = (BpType::type)type2;

            try {
              if (client) {
                client->del_breakpoint(bp);
              }
            }
            catch (...) {
              return DRC_FAILED;
            }
        }

        bpts[nadd + i].code = BPT_OK;
    }

    *nbpts = (ndel + nadd);
    return DRC_OK;
}

// Update low-level (server side) breakpoint conditions
// Returns nlowcnds. -1-network error
// This function is called from debthread
static drc_t idaapi update_lowcnds(int* nupdated, const lowcnd_t *lowcnds, int nlowcnds, qstring* errbuf)
{
    for (int i = 0; i < nlowcnds; ++i)
    {
        ea_t start = lowcnds[i].ea;
        ea_t end = lowcnds[i].ea + lowcnds[i].size - 1;
        BpType::type type1;
        int type2 = 0;
#ifdef DEBUG_68K
        bool is_vdp = false;
#endif

        switch (lowcnds[i].type)
        {
        case BPT_EXEC:
            type1 = BpType::BP_PC;
            break;
        case BPT_READ:
            type1 = BpType::BP_READ;
            break;
        case BPT_WRITE:
            type1 = BpType::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = BpType::BP_READ;
            type2 = (int)BpType::BP_WRITE;
            break;
        }

#ifdef DEBUG_68K
        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }
#endif

        DbgBreakpoint bp;
        bp.bstart = start & 0xFFFFFF;
        bp.bend = end & 0xFFFFFF;
        bp.type = type1;

        bp.enabled = true;
#ifdef DEBUG_68K
        bp.is_vdp = is_vdp;
#endif
        bp.is_forbid = (lowcnds[i].cndbody.empty() ? false : ((lowcnds[i].cndbody[0] == '1') ? true : false));

        try {
          if (client) {
            client->update_breakpoint(bp);
          }
        }
        catch (...) {
          return DRC_FAILED;
        }

        if (type2 != 0)
        {
            bp.type = (BpType::type)type2;

            try {
              if (client) {
                client->update_breakpoint(bp);
              }
            }
            catch (...) {
              return DRC_FAILED;
            }
        }
    }

    *nupdated = nlowcnds;
    return DRC_OK;
}

// Calculate the call stack trace
// This function is called when the process is suspended and should fill
// the 'trace' object with the information about the current call stack.
// Returns: true-ok, false-failed.
// If this function is missing or returns false, IDA will use the standard
// mechanism (based on the frame pointer chain) to calculate the stack trace
// This function is called from the main thread
static bool idaapi update_call_stack(thid_t tid, call_stack_t *trace)
{
    std::vector<int32_t> cs;

    try {
      if (client) {
        client->get_callstack(cs);
      }
    }
    catch (...) {
      return false;
    }

    trace->resize(cs.size());

    for (size_t i = 0; i < cs.size(); i++)
    {
        call_stack_info_t &ci = (*trace)[i];
        ci.callea = cs[i];
        ci.funcea = BADADDR;
        ci.fp = BADADDR;
        ci.funcok = true;
    }

    return true;
}

static ssize_t idaapi idd_notify(void*, int msgid, va_list va) {
  drc_t retcode = DRC_NONE;
  qstring* errbuf;

  switch (msgid)
  {
  case debugger_t::ev_init_debugger:
  {
    const char* hostname = va_arg(va, const char*);

    int portnum = va_arg(va, int);
    const char* password = va_arg(va, const char*);
    errbuf = va_arg(va, qstring*);
    QASSERT(1522, errbuf != NULL);
    retcode = init_debugger(hostname, portnum, password, errbuf);
  }
  break;

  case debugger_t::ev_term_debugger:
    retcode = term_debugger();
    break;

  case debugger_t::ev_get_processes:
  {
    procinfo_vec_t* procs = va_arg(va, procinfo_vec_t*);
    errbuf = va_arg(va, qstring*);
    retcode = s_get_processes(procs, errbuf);
  }
  break;

  case debugger_t::ev_start_process:
  {
    const char* path = va_arg(va, const char*);
    const char* args = va_arg(va, const char*);
    const char* startdir = va_arg(va, const char*);
    uint32 dbg_proc_flags = va_arg(va, uint32);
    const char* input_path = va_arg(va, const char*);
    uint32 input_file_crc32 = va_arg(va, uint32);
    errbuf = va_arg(va, qstring*);
    retcode = s_start_process(path,
      args,
      startdir,
      dbg_proc_flags,
      input_path,
      input_file_crc32,
      errbuf);
  }
  break;

  //case debugger_t::ev_attach_process:
  //{
  //    pid_t pid = va_argi(va, pid_t);
  //    int event_id = va_arg(va, int);
  //    uint32 dbg_proc_flags = va_arg(va, uint32);
  //    errbuf = va_arg(va, qstring*);
  //    retcode = s_attach_process(pid, event_id, dbg_proc_flags, errbuf);
  //}
  //break;

  //case debugger_t::ev_detach_process:
  //    retcode = g_dbgmod.dbg_detach_process();
  //    break;

  case debugger_t::ev_get_debapp_attrs:
  {
    debapp_attrs_t* out_pattrs = va_arg(va, debapp_attrs_t*);
#ifdef DEBUG_68K
    out_pattrs->addrsize = 4;
    out_pattrs->is_be = true;
#else
    out_pattrs->addrsize = 2;
    out_pattrs->is_be = false;
#endif
    out_pattrs->platform = "sega_md";
    out_pattrs->cbsize = sizeof(debapp_attrs_t);
    retcode = DRC_OK;
  }
  break;

  //case debugger_t::ev_rebase_if_required_to:
  //{
  //    ea_t new_base = va_arg(va, ea_t);
  //    retcode = DRC_OK;
  //}
  //break;

  case debugger_t::ev_request_pause:
    errbuf = va_arg(va, qstring*);
    retcode = prepare_to_pause_process(errbuf);
    break;

  case debugger_t::ev_exit_process:
    errbuf = va_arg(va, qstring*);
    retcode = emul_exit_process(errbuf);
    break;

  case debugger_t::ev_get_debug_event:
  {
    gdecode_t* code = va_arg(va, gdecode_t*);
    debug_event_t* event = va_arg(va, debug_event_t*);
    int timeout_ms = va_arg(va, int);
    *code = get_debug_event(event, timeout_ms);
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_resume:
  {
    debug_event_t* event = va_arg(va, debug_event_t*);
    retcode = continue_after_event(event);
  }
  break;

  //case debugger_t::ev_set_exception_info:
  //{
  //    exception_info_t* info = va_arg(va, exception_info_t*);
  //    int qty = va_arg(va, int);
  //    g_dbgmod.dbg_set_exception_info(info, qty);
  //    retcode = DRC_OK;
  //}
  //break;

  //case debugger_t::ev_suspended:
  //{
  //    bool dlls_added = va_argi(va, bool);
  //    thread_name_vec_t* thr_names = va_arg(va, thread_name_vec_t*);
  //    retcode = DRC_OK;
  //}
  //break;

  case debugger_t::ev_thread_suspend:
  {
    thid_t tid = va_argi(va, thid_t);
    pause_execution();
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_thread_continue:
  {
    thid_t tid = va_argi(va, thid_t);
    continue_execution();
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_set_resume_mode:
  {
    thid_t tid = va_argi(va, thid_t);
    resume_mode_t resmod = va_argi(va, resume_mode_t);
    retcode = s_set_resume_mode(tid, resmod);
  }
  break;

  case debugger_t::ev_read_registers:
  {
    thid_t tid = va_argi(va, thid_t);
    int clsmask = va_arg(va, int);
    regval_t* values = va_arg(va, regval_t*);
    errbuf = va_arg(va, qstring*);
    retcode = read_registers(tid, clsmask, values, errbuf);
  }
  break;

  case debugger_t::ev_write_register:
  {
    thid_t tid = va_argi(va, thid_t);
    int regidx = va_arg(va, int);
    const regval_t* value = va_arg(va, const regval_t*);
    errbuf = va_arg(va, qstring*);
    retcode = write_register(tid, regidx, value, errbuf);
  }
  break;

  case debugger_t::ev_get_memory_info:
  {
    meminfo_vec_t* ranges = va_arg(va, meminfo_vec_t*);
    errbuf = va_arg(va, qstring*);
    retcode = get_memory_info(*ranges, errbuf);
  }
  break;

  case debugger_t::ev_read_memory:
  {
    size_t* nbytes = va_arg(va, size_t*);
    ea_t ea = va_arg(va, ea_t);
    void* buffer = va_arg(va, void*);
    size_t size = va_arg(va, size_t);
    errbuf = va_arg(va, qstring*);
    ssize_t code = read_memory(ea, buffer, size, errbuf);
    *nbytes = code >= 0 ? code : 0;
    retcode = code >= 0 ? DRC_OK : DRC_NOPROC;
  }
  break;

  case debugger_t::ev_write_memory:
  {
    size_t* nbytes = va_arg(va, size_t*);
    ea_t ea = va_arg(va, ea_t);
    const void* buffer = va_arg(va, void*);
    size_t size = va_arg(va, size_t);
    errbuf = va_arg(va, qstring*);
    ssize_t code = write_memory(ea, buffer, size, errbuf);
    *nbytes = code >= 0 ? code : 0;
    retcode = code >= 0 ? DRC_OK : DRC_NOPROC;
  }
  break;

  case debugger_t::ev_check_bpt:
  {
    int* bptvc = va_arg(va, int*);
    bpttype_t type = va_argi(va, bpttype_t);
    ea_t ea = va_arg(va, ea_t);
    int len = va_arg(va, int);
    *bptvc = is_ok_bpt(type, ea, len);
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_update_bpts:
  {
    int* nbpts = va_arg(va, int*);
    update_bpt_info_t* bpts = va_arg(va, update_bpt_info_t*);
    int nadd = va_arg(va, int);
    int ndel = va_arg(va, int);
    errbuf = va_arg(va, qstring*);
    retcode = update_bpts(nbpts, bpts, nadd, ndel, errbuf);
  }
  break;

  case debugger_t::ev_update_lowcnds:
  {
      int* nupdated = va_arg(va, int*);
      const lowcnd_t* lowcnds = va_arg(va, const lowcnd_t*);
      int nlowcnds = va_arg(va, int);
      errbuf = va_arg(va, qstring*);
      retcode = update_lowcnds(nupdated, lowcnds, nlowcnds, errbuf);
  }
  break;

#ifdef HAVE_UPDATE_CALL_STACK
  case debugger_t::ev_update_call_stack:
  {
    thid_t tid = va_argi(va, thid_t);
    call_stack_t* trace = va_arg(va, call_stack_t*);
    retcode = g_dbgmod.dbg_update_call_stack(tid, trace);
  }
  break;
#endif

  //case debugger_t::ev_eval_lowcnd:
  //{
  //    thid_t tid = va_argi(va, thid_t);
  //    ea_t ea = va_arg(va, ea_t);
  //    errbuf = va_arg(va, qstring*);
  //    retcode = g_dbgmod.dbg_eval_lowcnd(tid, ea, errbuf);
  //}
  //break;

  //case debugger_t::ev_bin_search:
  //{
  //    ea_t* ea = va_arg(va, ea_t*);
  //    ea_t start_ea = va_arg(va, ea_t);
  //    ea_t end_ea = va_arg(va, ea_t);
  //    const compiled_binpat_vec_t* ptns = va_arg(va, const compiled_binpat_vec_t*);
  //    int srch_flags = va_arg(va, int);
  //    errbuf = va_arg(va, qstring*);
  //    if (ptns != NULL)
  //        retcode = g_dbgmod.dbg_bin_search(ea, start_ea, end_ea, *ptns, srch_flags, errbuf);
  //}
  //break;
  default:
    retcode = DRC_NONE;
  }

  return retcode;
}

//--------------------------------------------------------------------------
//
//	  DEBUGGER DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------

debugger_t debugger =
{
    IDD_INTERFACE_VERSION,
    NAME,
#ifdef DEBUG_68K
    0x8000 + 1,
    "m68k",
#else
    0x8000 + 2,
    "z80",
#endif
    DBG_FLAG_NOHOST | DBG_FLAG_CAN_CONT_BPT | DBG_FLAG_FAKE_ATTACH | DBG_FLAG_SAFE | DBG_FLAG_NOPASSWORD | DBG_FLAG_NOSTARTDIR | DBG_FLAG_NOPARAMETERS | DBG_FLAG_ANYSIZE_HWBPT | DBG_FLAG_DEBTHREAD | DBG_FLAG_PREFER_SWBPTS,
    DBG_HAS_GET_PROCESSES | DBG_HAS_REQUEST_PAUSE | DBG_HAS_SET_RESUME_MODE | DBG_HAS_CHECK_BPT | DBG_HAS_THREAD_SUSPEND | DBG_HAS_THREAD_CONTINUE | DBG_FLAG_LOWCNDS | DBG_FLAG_CONNSTRING,

    register_classes,
    RC_GENERAL,
    registers,
    qnumber(registers),

    0x1000,

    NULL,
    0,
    0,

    DBG_RESMOD_STEP_INTO | DBG_RESMOD_STEP_OVER,

    NULL, // set_dbg_options
    idd_notify
};