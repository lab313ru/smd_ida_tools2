#ifndef DEBUG_WINDOW_H
#define DEBUG_WINDOW_H

#include <vector>
#include <string>
#include <map>

typedef unsigned int uint32;
typedef unsigned short ushort;

enum class bp_type
{
    BP_PC = 1,
    BP_READ,
    BP_WRITE,
};

struct Breakpoint
{
    bp_type type;

    uint32 start;
    uint32 end;

    bool enabled;
    bool is_forbid;

#ifdef DEBUG_68K
    bool is_vdp;

    Breakpoint(bp_type _type, uint32 _start, uint32 _end, bool _enabled, bool _is_vdp, bool _is_forbid) :
      type(_type), start(_start), end(_end), enabled(_enabled), is_vdp(_is_vdp), is_forbid(_is_forbid) {};
#else
    Breakpoint(bp_type _type, uint32 _start, uint32 _end, bool _enabled, bool _is_forbid) :
      type(_type), start(_start), end(_end), enabled(_enabled), is_forbid(_is_forbid) {};
#endif
};

typedef std::vector<Breakpoint> bp_list;

#ifdef DEBUG_68K
#define MAX_ROM_SIZE 0x800000
#else
#define MAX_ROM_SIZE 0x10000 // including possible z80 code in 0x8000 - 0xFFFF
#endif

struct DebugWindow
{
    DebugWindow();
    std::vector<uint32> callstack;
    std::map<int32_t, int32_t> changed;
    bp_list Breakpoints;

    bool DebugStop;

    bool StepInto;
    uint32 StepOver;

    void Breakpoint(int pc);

    bool BreakPC(int pc);
#ifdef DEBUG_68K
    bool BreakRead(int pc, uint32 start, uint32 stop, bool is_vdp);
    bool BreakWrite(int pc, uint32 start, uint32 stop, bool is_vdp);
#else
    bool BreakRead(int pc, uint32 start, uint32 stop);
    bool BreakWrite(int pc, uint32 start, uint32 stop);
#endif

    virtual void DoStepOver();
    virtual void TracePC(int pc);
    virtual void TraceRead(uint32 start, uint32 stop);
    virtual void TraceWrite(uint32 start, uint32 stop);
    virtual ~DebugWindow();
};


#endif
