#include <Windows.h>
#include <algorithm>
#include <ida.hpp>
#include <idd.hpp>
#include <dbg.hpp>
#include <diskio.hpp>
#include <auto.hpp>
#include <funcs.hpp>

#include "g_main.h"
#include "g_ddraw.h"
#include "g_dsound.h"
#include "star_68k.h"
#include "m68k_debugwindow.h"
#include "vdp_io.h"
#include "ram_search.h"
#include "resource.h"

#include "ida_debmod.h"

#include "ida_registers.h"
#include "ida_debug.h"
#include "ida_plugin.h"

#include <vector>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow);

codemap_t g_codemap;
eventlist_t g_events;

extern int Gens_Running;
qthread_t gens_thread = NULL;

uint16 allow0_breaks = 0;
uint32 allow1_breaks = 0;
uint32 break_regs[16 + 24] = { 0 };

#define BREAKPOINTS_BASE 0x00D00000

#define CHECK_FOR_START(x) {if (!Gens_Running) return x;}

static const char *const SRReg[] =
{
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
};

static const char *const ALLOW_FLAGS_DA[] =
{
    "_A07",
    "_A06",
    "_A05",
    "_A04",
    "_A03",
    "_A02",
    "_A01",
    "_A00",

    "_D07",
    "_D06",
    "_D05",
    "_D04",
    "_D03",
    "_D02",
    "_D01",
    "_D00",
};

static const char *const ALLOW_FLAGS_V[] =
{
    "_V23",
    "_V22",
    "_V21",
    "_V20",
    "_V19",
    "_V18",
    "_V17",
    "_V16",
    "_V15",
    "_V14",
    "_V13",
    "_V12",
    "_V11",
    "_V10",
    "_V09",
    "_V08",
    "_V07",
    "_V06",
    "_V05",
    "_V04",
    "_V03",
    "_V02",
    "_V01",
    "_V00",
};

register_info_t registers[] =
{
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
    { "A7", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },

    { "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_dword, NULL, 0 },

    { "SR", NULL, RC_GENERAL, dt_word, SRReg, 0xFFFF },

    { "DMA_LEN", REGISTER_READONLY, RC_GENERAL, dt_word, NULL, 0 },
    { "DMA_SRC", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
    { "VDP_DST", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },

    // Register Breakpoints
    { "D00", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D01", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D02", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D03", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D04", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D05", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D06", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D07", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },

    { "A00", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A01", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A02", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A03", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A04", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A05", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A06", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A07", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },

    { "V00", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V01", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V02", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V03", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V04", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V05", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V06", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V07", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V08", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V09", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V10", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V11", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V12", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V13", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V14", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V15", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V16", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V17", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V18", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V19", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V20", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V21", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V22", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V23", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "ALLOW0", NULL, RC_BREAK, dt_word, ALLOW_FLAGS_DA, 0xFFFF },
    { "ALLOW1", NULL, RC_BREAK, dt_tbyte, ALLOW_FLAGS_V, 0xFFFFFF },

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
};

static const char *register_classes[] =
{
    "General Registers",
    "VDP Registers",
    "Register Breakpoints",
    NULL
};

static void prepare_codemap()
{
    g_codemap.resize(MAX_ROM_SIZE);
    for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
    {
        g_codemap[i] = std::pair<uint32, bool>(BADADDR, false);
    }
}

static void apply_codemap()
{
    if (g_codemap.empty()) return;

    msg("Applying codemap...\n");
    for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
    {
        if (g_codemap[i].second && g_codemap[i].first)
        {
            auto_make_code((ea_t)i);
            plan_ea((ea_t)i);
        }
        show_addr((ea_t)i);
    }
    plan_range(0, MAX_ROM_SIZE);

    for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
    {
        if (g_codemap[i].second && g_codemap[i].first && !get_func((ea_t)i))
        {
            if (add_func(i, BADADDR))
                add_cref(g_codemap[i].first, i, fl_CN);
            plan_ea((ea_t)i);
        }
        show_addr((ea_t)i);
    }
    plan_range(0, MAX_ROM_SIZE);
    msg("Codemap applied.\n");
}

inline static void toggle_pause()
{
    HWND hwndGens = FindWindowEx(NULL, NULL, "Gens", NULL);

    if (hwndGens != NULL)
        SendMessage(hwndGens, WM_COMMAND, ID_EMULATION_PAUSED, 0);
}

static void pause_execution()
{
    M68kDW.DebugStop = true;

    if (Paused) return;
    toggle_pause();
}

static void continue_execution()
{
    M68kDW.DebugStop = false;

    if (!Paused) return;
    toggle_pause();
}

static void finish_execution()
{
    if (gens_thread != NULL)
    {
        qthread_join(gens_thread);
        qthread_free(gens_thread);
        qthread_kill(gens_thread);
        gens_thread = NULL;
    }
}

// Initialize debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi init_debugger(const char* hostname, int portnum, const char* password, qstring *errbuf)
{
    prepare_codemap();
    set_processor_type(ph.psnames[0], SETPROC_LOADER); // reset proc to "M68000"
    return DRC_OK;
}

// Terminate debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi term_debugger(void)
{
    finish_execution();
    apply_codemap();
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

HINSTANCE GetHInstance()
{
    MEMORY_BASIC_INFORMATION mbi;
    SetLastError(ERROR_SUCCESS);
    VirtualQuery(GetHInstance, &mbi, sizeof(mbi));

    return (HINSTANCE)mbi.AllocationBase;
}

char cmdline[2048];
static int idaapi gens_process(void *ud)
{
    int rc;

    rc = WinMain(GetHInstance(), (HINSTANCE)NULL, cmdline, SW_NORMAL);

    debug_event_t ev;
    ev.set_eid(PROCESS_EXITED);
    ev.pid = 1;
    ev.handled = true;
    ev.set_exit_code(PROCESS_EXITED, rc);

    g_events.enqueue(ev, IN_BACK);

    return rc;
}

static uint32 get_entry_point(const char *rom_path)
{
    FILE *fp = fopenRB(rom_path);
    if (fp == NULL) return 0;

    uint32 addr = 0;
    eseek(fp, 4);
    eread(fp, &addr, sizeof(addr));
    eclose(fp);

    return swap32(addr);
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
    qsnprintf(cmdline, sizeof(cmdline), "-rom \"%s\"", path);

    uint32 start = get_entry_point(path);

    M68kDW.Breakpoints.clear();
    Breakpoint b(bp_type::BP_PC, start & 0xFFFFFF, start & 0xFFFFFF, true, false, false);
    M68kDW.Breakpoints.push_back(b);

    allow0_breaks = allow1_breaks = 0;
    g_events.clear();

    gens_thread = qthread_create(gens_process, NULL);

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
    CHECK_FOR_START(DRC_OK);
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
    CHECK_FOR_START(DRC_OK);
    allow0_breaks = allow1_breaks = 0;

    HWND hwndGens = FindWindowEx(NULL, NULL, "Gens", NULL);
    if (hwndGens != NULL)
    {
        SendMessage(hwndGens, WM_CLOSE, 0, 0);
    }

    return DRC_OK;
}

// Get a pending debug event and suspend the process
// This function will be called regularly by IDA.
// This function is called from debthread
static gdecode_t idaapi get_debug_event(debug_event_t *event, int timeout_ms)
{
    while (true)
    {
        // are there any pending events?
        if (g_events.retrieve(event))
        {
            switch (event->eid())
            {
            case PROCESS_SUSPENDED:
                apply_codemap();
                break;
            }
            return g_events.empty() ? GDE_ONE_EVENT : GDE_MANY_EVENTS;
        }
        if (g_events.empty())
            break;
    }
    return GDE_NO_EVENT;
}

// Continue after handling the event
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi continue_after_event(const debug_event_t *event)
{
    switch (event->eid())
    {
    case STEP:
    case PROCESS_SUSPENDED:
        continue_execution();
        break;
    case PROCESS_EXITED:
        continue_execution();
        finish_execution();
        apply_codemap();
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
        M68kDW.StepInto = 1;
        M68kDW.DebugStop = false;
        break;
    case RESMOD_OVER:    ///< step over call
        M68kDW.DoStepOver();
        M68kDW.DebugStop = false;
        break;
    }

    return DRC_OK;
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
        values[R_D0].ival = main68k_context.dreg[R_D0 - R_D0];
        values[R_D1].ival = main68k_context.dreg[R_D1 - R_D0];
        values[R_D2].ival = main68k_context.dreg[R_D2 - R_D0];
        values[R_D3].ival = main68k_context.dreg[R_D3 - R_D0];
        values[R_D4].ival = main68k_context.dreg[R_D4 - R_D0];
        values[R_D5].ival = main68k_context.dreg[R_D5 - R_D0];
        values[R_D6].ival = main68k_context.dreg[R_D6 - R_D0];
        values[R_D7].ival = main68k_context.dreg[R_D7 - R_D0];

        values[R_A0].ival = main68k_context.areg[R_A0 - R_A0];
        values[R_A1].ival = main68k_context.areg[R_A1 - R_A0];
        values[R_A2].ival = main68k_context.areg[R_A2 - R_A0];
        values[R_A3].ival = main68k_context.areg[R_A3 - R_A0];
        values[R_A4].ival = main68k_context.areg[R_A4 - R_A0];
        values[R_A5].ival = main68k_context.areg[R_A5 - R_A0];
        values[R_A6].ival = main68k_context.areg[R_A6 - R_A0];
        values[R_A7].ival = main68k_context.areg[R_A7 - R_A0];

        values[R_PC].ival = M68kDW.last_pc & 0xFFFFFF;
        values[R_SR].ival = main68k_context.sr;

        values[R_VDP_DMA_LEN].ival = (BYTE)(VDP_Reg.regs[R_DR19 - R_DR00]) | ((BYTE)(VDP_Reg.regs[R_DR20 - R_DR00]) << 8);

        values[R_VDP_DMA_SRC].ival = (BYTE)(VDP_Reg.regs[R_DR21 - R_DR00]) | ((BYTE)(VDP_Reg.regs[R_DR22 - R_DR00]) << 8);
        UINT16 dma_high = VDP_Reg.regs[R_DR23 - R_DR00];
        if (!(dma_high & 0x80))
            values[R_VDP_DMA_SRC].ival |= ((BYTE)(VDP_Reg.regs[R_DR23 - R_DR00] & mask(0, 7)) << 16);
        else
            values[R_VDP_DMA_SRC].ival |= ((BYTE)(VDP_Reg.regs[R_DR23 - R_DR00] & mask(0, 6)) << 16);
        values[R_VDP_DMA_SRC].ival <<= 1;

        values[R_VDP_DMA_SRC].ival &= 0xFFFFFF;

        values[R_VDP_WRITE_ADDR].ival = BREAKPOINTS_BASE;
        switch (Ctrl.Access)
        {
        case 0x09: // VRAM
        case 0x0A: // CRAM
        case 0x0B: // VSRAM
            values[R_VDP_WRITE_ADDR].ival = (BREAKPOINTS_BASE + 0x10000 * (Ctrl.Access - 0x09)) + (Ctrl.Address & 0xFFFF);
            break;
        }
    }

    if (clsmask & RC_VDP)
    {
        for (int i = 0; i < 24; ++i)
        {
            values[R_DR00 + i].ival = VDP_Reg.regs[i];
        }
    }

    if (clsmask & RC_BREAK)
    {
        values[R_B_ALLOW0].ival = allow0_breaks;
        values[R_B_ALLOW1].ival = allow1_breaks;
        for (int i = 0; i < (16 + 24); ++i)
        {
            values[R_B00 + i].ival = break_regs[i];
        }
    }

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
    if (regidx >= R_D0 && regidx <= R_D7)
    {
        main68k_context.dreg[regidx - R_D0] = (uint32)value->ival;
    }
    else if (regidx >= R_A0 && regidx <= R_A7)
    {
        main68k_context.areg[regidx - R_A0] = (uint32)value->ival;
    }
    else if (regidx == R_PC)
    {
        main68k_context.pc = (uint32)value->ival & 0xFFFFFF;
    }
    else if (regidx == R_SR)
    {
        main68k_context.sr = (uint16)value->ival;
    }
    else if (regidx >= R_DR00 && regidx <= R_DR23)
    {
        VDP_Reg.regs[regidx - R_DR00] = (uint32)value->ival;
    }
    else if (regidx == R_B_ALLOW0)
    {
        allow0_breaks = (uint16)value->ival;
    }
    else if (regidx == R_B_ALLOW1)
    {
        allow1_breaks = (uint32)value->ival;
    }
    else if (regidx >= R_B00 && regidx <= R_B39)
    {
        break_regs[regidx - R_B00] = (uint32)value->ival;
    }

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

    return DRC_OK;
}

extern bool IsHardwareAddressValid(unsigned int address);

// Read process memory
// Returns number of read bytes
// 0 means read error
// -1 means that the process does not exist anymore
// This function is called from debthread
static ssize_t idaapi read_memory(ea_t ea, void *buffer, size_t size, qstring *errbuf)
{
    CHECK_FOR_START(0);
    for (size_t i = 0; i < size; ++i)
    {
        if ((ea + i >= 0xA00000 && ea + i < 0xA10000) && IsHardwareAddressValid((uint32)(ea + i)))
        {
            // Z80
            unsigned char value = (unsigned char)(ReadValueAtHardwareAddress((uint32)((ea + i) ^ 1), 1) & 0xFF);
            ((UINT8*)buffer)[i] = value;
        }
        else if (IsHardwareAddressValid((uint32)(ea + i)))
        {
            unsigned char value = (unsigned char)(ReadValueAtHardwareAddress((uint32)(ea + i), 1) & 0xFF);
            ((UINT8*)buffer)[i] = value;
        }
        else if (ea + i >= (BREAKPOINTS_BASE + 0x00000) && ea + i < (BREAKPOINTS_BASE + 0x10000))
        {
            // VRAM
            ea_t addr = ea + i - (BREAKPOINTS_BASE + 0x00000);
            ((UINT8*)buffer)[i] = VRam[(addr ^ 1) & 0xFFFF];
        }
        else if (ea + i >= (BREAKPOINTS_BASE + 0x10000) && ea + i < (BREAKPOINTS_BASE + 0x20000))
        {
            // CRAM
            ea_t addr = ea + i - (BREAKPOINTS_BASE + 0x10000);
            ((UINT8*)buffer)[i] = ((UINT8*)CRam)[(addr ^ 1) & 0x1FF];
        }
        else if (ea + i >= (BREAKPOINTS_BASE + 0x20000) && ea + i < (BREAKPOINTS_BASE + 0x30000))
        {
            // VSRAM
            ea_t addr = ea + i - (BREAKPOINTS_BASE + 0x20000);
            ((UINT8*)buffer)[i] = ((UINT8*)VSRam)[(addr ^ 1) & 0xFF];
        }
        // else leave the value nil
    }

    return size;
}
// Write process memory
// Returns number of written bytes, -1-fatal error
// This function is called from debthread
static ssize_t idaapi write_memory(ea_t ea, const void *buffer, size_t size, qstring *errbuf)
{
    return 0;
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
    CHECK_FOR_START(DRC_FAILED);

    for (int i = 0; i < nadd; ++i)
    {
        ea_t start = bpts[i].ea;
        ea_t end = bpts[i].ea + bpts[i].size - 1;
        bp_type type1;
        int type2 = 0;
        bool is_vdp = false;

        switch (bpts[i].type)
        {
        case BPT_EXEC:
            type1 = bp_type::BP_PC;
            break;
        case BPT_READ:
            type1 = bp_type::BP_READ;
            break;
        case BPT_WRITE:
            type1 = bp_type::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = bp_type::BP_READ;
            type2 = (int)bp_type::BP_WRITE;
            break;
        }

        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }

        Breakpoint b(type1, start & 0xFFFFFF, end & 0xFFFFFF, true, is_vdp, false);
        M68kDW.Breakpoints.push_back(b);

        if (type2 != 0)
        {
            Breakpoint b((bp_type)type2, start & 0xFFFFFF, end & 0xFFFFFF, true, is_vdp, false);
            M68kDW.Breakpoints.push_back(b);
        }

        bpts[i].code = BPT_OK;
    }

    for (int i = 0; i < ndel; ++i)
    {
        ea_t start = bpts[nadd + i].ea;
        ea_t end = bpts[nadd + i].ea + bpts[nadd + i].size - 1;
        bp_type type1;
        int type2 = 0;
        bool is_vdp = false;

        switch (bpts[nadd + i].type)
        {
        case BPT_EXEC:
            type1 = bp_type::BP_PC;
            break;
        case BPT_READ:
            type1 = bp_type::BP_READ;
            break;
        case BPT_WRITE:
            type1 = bp_type::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = bp_type::BP_READ;
            type2 = (int)bp_type::BP_WRITE;
            break;
        }

        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }

        start &= 0xFFFFFF;
        end &= 0xFFFFFF;

        for (auto j = M68kDW.Breakpoints.begin(); j != M68kDW.Breakpoints.end(); )
        {
            if (j->type != type1 ||
                !(start <= j->end && end >= j->start && is_vdp == j->is_vdp))
            {
                ++j;
            }
            else
            {
                j = M68kDW.Breakpoints.erase(j);
            }
        }

        if (type2 != 0)
        {
            for (auto j = M68kDW.Breakpoints.begin(); j != M68kDW.Breakpoints.end(); )
            {
                if (j->type != (bp_type)type2 ||
                    !(start <= j->end && end >= j->start && is_vdp == j->is_vdp))
                {
                    ++j;
                }
                else
                {
                    j = M68kDW.Breakpoints.erase(j);
                }
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
        bp_type type1;
        int type2 = 0;
        bool is_vdp = false;

        switch (lowcnds[i].type)
        {
        case BPT_EXEC:
            type1 = bp_type::BP_PC;
            break;
        case BPT_READ:
            type1 = bp_type::BP_READ;
            break;
        case BPT_WRITE:
            type1 = bp_type::BP_WRITE;
            break;
        case BPT_RDWR:
            type1 = bp_type::BP_READ;
            type2 = (int)bp_type::BP_WRITE;
            break;
        }

        if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000)
        {
            start -= BREAKPOINTS_BASE;
            end -= BREAKPOINTS_BASE;
            is_vdp = true;
        }

        start &= 0xFFFFFF;
        end &= 0xFFFFFF;

        for (auto j = M68kDW.Breakpoints.begin(); j != M68kDW.Breakpoints.end(); ++j)
        {
            if (j->type != type1) continue;

            if (start <= j->end && end >= j->start &&
                j->is_vdp == is_vdp)
            {
                j->is_forbid = (lowcnds[i].cndbody.empty() ? false : ((lowcnds[i].cndbody[0] == '1') ? true : false));
            }
        }

        if (type2 != 0)
        {
            for (auto j = M68kDW.Breakpoints.begin(); j != M68kDW.Breakpoints.end(); ++j)
            {
                if (j->type != (bp_type)type2) continue;

                if (start <= j->end && end >= j->start &&
                    j->is_vdp == is_vdp)
                {
                    j->is_forbid = (lowcnds[i].cndbody.empty() ? false : ((lowcnds[i].cndbody[0] == '1') ? true : false));
                }
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
    CHECK_FOR_START(0);

    size_t n = M68kDW.callstack.size();
    trace->resize(n);
    for (size_t i = 0; i < n; i++)
    {
        call_stack_info_t &ci = (*trace)[i];
        ci.callea = M68kDW.callstack[i];
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
    out_pattrs->addrsize = 4;
    out_pattrs->is_be = true;
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
    CHECK_FOR_START(DRC_OK);
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
    "GXIDA",
    0x8000 + 1,
    "m68k",
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