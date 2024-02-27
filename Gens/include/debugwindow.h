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

    uint32 elang;
    std::string condition;

#ifdef DEBUG_68K
    bool is_vdp;

    Breakpoint(bp_type _type, uint32 _start, uint32 _end, bool _enabled, bool _is_vdp, uint32 _elang, const std::string & _condition) :
      type(_type), start(_start), end(_end), enabled(_enabled), is_vdp(_is_vdp), elang(_elang), condition(_condition) {};
#else
    Breakpoint(bp_type _type, uint32 _start, uint32 _end, bool _enabled, uint32 _elang, const std::string& _condition) :
      type(_type), start(_start), end(_end), enabled(_enabled), elang(_elang), condition(_condition) {};
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
    std::map<uint32_t, uint32_t> changed;
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

extern void send_pause_event(int pc, std::map<uint32_t, uint32_t> changed);
extern bool evaluate_condition(uint32 elang, const char* condition);

#endif
