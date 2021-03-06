#include "luascript.h"
#include "gens.h"
#include "save.h"
#include "g_main.h"
#include "g_ddraw.h"
#include "misc.h"
#include "guidraw.h"
#include "movie.h"
#include "vdp_io.h"
#include "drawutil.h"
#include "unzip.h"
#include "cpu_68k.h"
#include "joypads.h"
#include "ym2612.h"
#include "resource.h"
#include <assert.h>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include "m68k_debugwindow.h"

// the emulator must provide these so that we can implement
// the various functions the user can call from their lua script
// (this interface with the emulator needs cleanup, I know)
extern int(*Update_Frame)();
extern int(*Update_Frame_Fast)();
extern unsigned int ReadValueAtHardwareAddress(unsigned int address, unsigned int size);
extern bool ReadCellAtVDPAddress(unsigned short address, unsigned char *cell);
extern bool ReadVDPPaletteLine(unsigned short line, unsigned short *pal);
extern bool WriteCellToVDPAddress(unsigned short address, unsigned char *cell);
extern bool WriteVDPPaletteLine(unsigned short line, unsigned short *pal);
extern bool WriteValueAtHardwareAddress(unsigned int address, unsigned int value, unsigned int size, bool hookless = false);
extern bool WriteValueAtHardwareRAMAddress(unsigned int address, unsigned int value, unsigned int size, bool hookless = false);
extern bool WriteValueAtHardwareROMAddress(unsigned int address, unsigned int value, unsigned int size);
extern bool IsHardwareAddressValid(unsigned int address);
extern bool IsHardwareRAMAddressValid(unsigned int address);
extern bool IsHardwareROMAddressValid(unsigned int address);
extern "C" int disableRamSearchUpdate;
extern "C" int Clear_Sound_Buffer(void);
extern long long GetCurrentInputCondensed();
extern long long PeekInputCondensed();
extern void SetNextInputCondensed(long long input, long long mask);
extern int Set_Current_State(int Num, bool showOccupiedMessage, bool showEmptyMessage);
extern int Update_Emulation_One(HWND hWnd);
extern void Update_Emulation_One_Before(HWND hWnd);
extern void Update_Emulation_After_Fast(HWND hWnd);
extern void Update_Emulation_One_Before_Minimal();
extern int Update_Frame_Adjusted();
extern int Update_Frame_Hook();
extern int Update_Frame_Fast_Hook();
extern void Update_Emulation_After_Controlled(HWND hWnd, bool flip);
extern void Prevent_Next_Frame_Skipping();
extern void UpdateLagCount();
extern bool BackgroundInput;
extern bool g_disableStatestateWarnings;
extern bool g_onlyCallSavestateCallbacks;
extern bool Step_Gens_MainLoop(bool allowSleep, bool allowEmulate);
extern bool frameadvSkipLagForceDisable;
extern "C" void Put_Info_NonImmediate(char *Message, int Duration);
extern int Show_Genesis_Screen();
extern void GensReplayMovie();
extern bool SkipNextRerecordIncrement;

extern "C" {
#include "lua/src/lua.h"
#include "lua/src/lauxlib.h"
#include "lua/src/lualib.h"
#include "lua/src/lstate.h"
};

enum SpeedMode
{
    SPEEDMODE_NORMAL,
    SPEEDMODE_NOTHROTTLE,
    SPEEDMODE_TURBO,
    SPEEDMODE_MAXIMUM,
};

struct LuaContextInfo {
    lua_State* L; // the Lua state
    bool started; // script has been started and hasn't yet been terminated, although it may not be currently running
    bool running; // script is currently running code (either the main call to the script or the callbacks it registered)
    bool returned; // main call to the script has returned (but it may still be active if it registered callbacks)
    bool crashed; // true if script has errored out
    bool restart; // if true, tells the script-running code to restart the script when the script stops
    bool restartLater; // set to true when a still-running script is stopped so that RestartAllLuaScripts can know which scripts to restart
    unsigned int worryCount; // counts up as the script executes, gets reset when the application is able to process messages, triggers a warning prompt if it gets too high
    bool stopWorrying; // set to true if the user says to let the script run forever despite appearing to be frozen
    bool panic; // if set to true, tells the script to terminate as soon as it can do so safely (used because directly calling lua_close() or luaL_error() is unsafe in some contexts)
    bool ranExit; // used to prevent a registered exit callback from ever getting called more than once
    bool guiFuncsNeedDeferring; // true whenever GUI drawing would be cleared by the next emulation update before it would be visible, and thus needs to be deferred until after the next emulation update
    int numDeferredGUIFuncs; // number of deferred function calls accumulated, used to impose an arbitrary limit to avoid running out of memory
    bool ranFrameAdvance; // false if gens.frameadvance() hasn't been called yet
    int transparencyModifier; // values less than 255 will scale down the opacity of whatever the GUI renders, values greater than 255 will increase the opacity of anything transparent the GUI renders
    SpeedMode speedMode; // determines how gens.frameadvance() acts
    char panicMessage[72]; // a message to print if the script terminates due to panic being set
    std::string lastFilename; // path to where the script last ran from so that restart can work (note: storing the script in memory instead would not be useful because we always want the most up-to-date script from file)
    std::string nextFilename; // path to where the script should run from next, mainly used in case the restart flag is true
    unsigned int dataSaveKey; // crc32 of the save data key, used to decide which script should get which data... by default (if no key is specified) it's calculated from the script filename
    unsigned int dataLoadKey; // same as dataSaveKey but set through registerload instead of registersave if the two differ
    bool dataSaveLoadKeySet; // false if the data save keys are unset or set to their default value
    bool rerecordCountingDisabled; // true if this script has disabled rerecord counting for the savestates it loads
    std::vector<std::string> persistVars; // names of the global variables to persist, kept here so their associated values can be output when the script exits
    LuaSaveData newDefaultData; // data about the default state of persisted global variables, which we save on script exit so we can detect when the default value has changed to make it easier to reset persisted variables
    unsigned int numMemHooks; // number of registered memory functions (1 per hooked byte)
    // callbacks into the lua window... these don't need to exist per context the way I'm using them, but whatever
    void(*print)(int uid, const char* str);
    void(*onstart)(int uid);
    void(*onstop)(int uid, bool statusOK);
};
std::map<int, LuaContextInfo*> luaContextInfo;
std::map<lua_State*, int> luaStateToUIDMap;
int g_numScriptsStarted = 0;
bool g_anyScriptsHighSpeed = false;
bool g_stopAllScriptsEnabled = true;

#define USE_INFO_STACK
#ifdef USE_INFO_STACK
std::vector<LuaContextInfo*> infoStack;
#define GetCurrentInfo() *infoStack.front() // should be faster but relies on infoStack correctly being updated to always have the current info in the first element
#else
std::map<lua_State*, LuaContextInfo*> luaStateToContextMap;
#define GetCurrentInfo() *luaStateToContextMap[L] // should always work but might be slower
#endif

//#define ASK_USER_ON_FREEZE // dialog on freeze is disabled now because it seems to be unnecessary, but this can be re-defined to enable it

static std::map<lua_CFunction, const char*> s_cFuncInfoMap;

// using this macro you can define a callable-from-Lua function
// while associating with it some information about its arguments.
// that information will show up if the user tries to print the function
// or otherwise convert it to a string.
// (for example, "writebyte=function(addr,value)" instead of "writebyte=function:0A403490")
// note that the user can always use addressof(func) if they want to retrieve the address.
#define DEFINE_LUA_FUNCTION(name, argstring) \
	static int name(lua_State* L); \
	static const char* name##_args = s_cFuncInfoMap[name] = argstring; \
	static int name(lua_State* L)

#ifdef _MSC_VER
#define snprintf _snprintf
#define vscprintf _vscprintf
#else
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define __forceinline __attribute__((always_inline))
#endif

static const char* luaCallIDStrings[] =
{
    "CALL_BEFOREEMULATION",
    "CALL_AFTEREMULATION",
    "CALL_AFTEREMULATIONGUI",
    "CALL_BEFOREEXIT",
    "CALL_BEFORESAVE",
    "CALL_AFTERLOAD",
    "CALL_ONSTART",

    "CALL_HOTKEY_1",
    "CALL_HOTKEY_2",
    "CALL_HOTKEY_3",
    "CALL_HOTKEY_4",
    "CALL_HOTKEY_5",
    "CALL_HOTKEY_6",
    "CALL_HOTKEY_7",
    "CALL_HOTKEY_8",
    "CALL_HOTKEY_9",
    "CALL_HOTKEY_10",
    "CALL_HOTKEY_11",
    "CALL_HOTKEY_12",
    "CALL_HOTKEY_13",
    "CALL_HOTKEY_14",
    "CALL_HOTKEY_15",
    "CALL_HOTKEY_16",
};
static const int _makeSureWeHaveTheRightNumberOfStrings[sizeof(luaCallIDStrings) / sizeof(*luaCallIDStrings) == LUACALL_COUNT ? 1 : 0];

static const char* luaMemHookTypeStrings[] =
{
    "MEMHOOK_WRITE",
    "MEMHOOK_READ",
    "MEMHOOK_EXEC",

    "MEMHOOK_WRITE_SUB",
    "MEMHOOK_READ_SUB",
    "MEMHOOK_EXEC_SUB",

    "MEMHOOK_WRITE_Z80",
    "MEMHOOK_READ_Z80",
    "MEMHOOK_EXEC_Z80"
};
static const int _makeSureWeHaveTheRightNumberOfStrings2[sizeof(luaMemHookTypeStrings) / sizeof(*luaMemHookTypeStrings) == LUAMEMHOOK_COUNT ? 1 : 0];

void StopScriptIfFinished(int uid, bool justReturned = false);
void SetSaveKey(LuaContextInfo& info, const char* key);
void SetLoadKey(LuaContextInfo& info, const char* key);
void RefreshScriptStartedStatus();
void RefreshScriptSpeedStatus();

static char* rawToCString(lua_State* L, int idx = 0);
static const char* toCString(lua_State* L, int idx = 0);

static void CalculateMemHookRegions(LuaMemHookType hookType);

static int memory_registerHook(lua_State* L, LuaMemHookType hookType, int defaultSize)
{
    // get first argument: address
    unsigned int addr = luaL_checkinteger(L, 1);
    if ((addr & ~0xFFFFFF) == ~0xFFFFFF)
        addr &= 0xFFFFFF;

    // get optional second argument: size
    int size = defaultSize;
    int funcIdx = 2;
    if (lua_isnumber(L, 2))
    {
        size = luaL_checkinteger(L, 2);
        if (size < 0)
        {
            size = -size;
            addr -= size;
        }
        funcIdx++;
    }

    // check last argument: callback function
    bool clearing = lua_isnil(L, funcIdx);
    if (!clearing)
        luaL_checktype(L, funcIdx, LUA_TFUNCTION);
    lua_settop(L, funcIdx);

    // get the address-to-callback table for this hook type of the current script
    lua_getfield(L, LUA_REGISTRYINDEX, luaMemHookTypeStrings[hookType]);

    // count how many callback functions we'll be displacing
    int numFuncsAfter = clearing ? 0 : size;
    int numFuncsBefore = 0;
    for (unsigned int i = addr; i != addr + size; i++)
    {
        lua_rawgeti(L, -1, i);
        if (lua_isfunction(L, -1))
            numFuncsBefore++;
        lua_pop(L, 1);
    }

    // put the callback function in the address slots
    for (unsigned int i = addr; i != addr + size; i++)
    {
        lua_pushvalue(L, -2);
        lua_rawseti(L, -2, i);
    }

    // adjust the count of active hooks
    LuaContextInfo& info = GetCurrentInfo();
    info.numMemHooks += numFuncsAfter - numFuncsBefore;

    // re-cache regions of hooked memory across all scripts
    CalculateMemHookRegions(hookType);

    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 0;
}

LuaMemHookType MatchHookTypeToCPU(lua_State* L, LuaMemHookType hookType)
{
    int cpuID = 0;

    int cpunameIndex = 0;
    if (lua_type(L, 2) == LUA_TSTRING)
        cpunameIndex = 2;
    else if (lua_type(L, 3) == LUA_TSTRING)
        cpunameIndex = 3;

    if (cpunameIndex)
    {
        const char* cpuName = lua_tostring(L, cpunameIndex);
        if (!stricmp(cpuName, "sub") || !stricmp(cpuName, "s68k"))
            cpuID = 1;
        if (!stricmp(cpuName, "z80"))
            cpuID = 2;
        lua_remove(L, cpunameIndex);
    }

    switch (cpuID)
    {
    case 0: // m68k:
        return hookType;

    case 1: // s68k:
        switch (hookType)
        {
        case LUAMEMHOOK_WRITE: return LUAMEMHOOK_WRITE_SUB;
        case LUAMEMHOOK_READ: return LUAMEMHOOK_READ_SUB;
        case LUAMEMHOOK_EXEC: return LUAMEMHOOK_EXEC_SUB;
        }

    case 2: // z80:
        switch (hookType)
        {
        case LUAMEMHOOK_WRITE: return LUAMEMHOOK_WRITE_Z80;
        case LUAMEMHOOK_READ: return LUAMEMHOOK_READ_Z80;
        case LUAMEMHOOK_EXEC: return LUAMEMHOOK_EXEC_Z80;
        }
    }
    return hookType;
}

DEFINE_LUA_FUNCTION(memory_registerwrite, "address,[size=1,][cpuname=\"main\",]func")
{
    return memory_registerHook(L, MatchHookTypeToCPU(L, LUAMEMHOOK_WRITE), 1);
}
DEFINE_LUA_FUNCTION(memory_registerread, "address,[size=1,][cpuname=\"main\",]func")
{
    return memory_registerHook(L, MatchHookTypeToCPU(L, LUAMEMHOOK_READ), 1);
}
DEFINE_LUA_FUNCTION(memory_registerexec, "address,[size=2,][cpuname=\"main\",]func")
{
    return memory_registerHook(L, MatchHookTypeToCPU(L, LUAMEMHOOK_EXEC), 2);
}

DEFINE_LUA_FUNCTION(vdp_readcell, "address[,count]")
{
    unsigned short address = luaL_checkinteger(L, 1);
    int count = 1;
    if (lua_isnumber(L, 2))
    {
        count = luaL_checkinteger(L, 2);
    }

    if (count < 0)
    {
        address += (count * 32);
        count = -count;
    }

    int i = 0;
    bool blah = true;

    // push the array
    lua_newtable(L);
    while ((i < count) && blah)
    {
        blah = ReadCellAtVDPAddress(address, (unsigned char *)lua_newuserdata(L, 32));
        if (blah)
        {
            lua_rawseti(L, -2, ++i);
            address += 32;
        }
        else
        {
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, i);
    lua_insert(L, -2);

    return 2;
}
DEFINE_LUA_FUNCTION(vdp_writecell, "address,celldata")
{
    unsigned short address = luaL_checkinteger(L, 1);
    int count = 0;
    int type = lua_type(L, 2);
    if (type == LUA_TUSERDATA)
    {
        if (WriteCellToVDPAddress(address, (unsigned char *)lua_touserdata(L, 2)))
            count++;
        lua_pushinteger(L, count);
        return 1;
    }

    if (type != LUA_TTABLE)
        return 0;

    int i = 1;
    bool blah = true;
    do {
        lua_rawgeti(L, 2, i++);
        if (lua_type(L, -1) == LUA_TUSERDATA)
        {
            blah = WriteCellToVDPAddress(address, (unsigned char *)lua_touserdata(L, -1));
            count++;
            address += 32;
        }
        else
            blah = false;
        lua_pop(L, 1);
    } while (blah);

    lua_pushinteger(L, count);
    return 1;
}
DEFINE_LUA_FUNCTION(vdp_readpalette, "[line][,numlines]")
{
    short line = 0;
    int count = 4;
    if (lua_isnumber(L, 1))
        line = luaL_checkinteger(L, 1) - 1;
    if (lua_isnumber(L, 2))
        count = luaL_checkinteger(L, 2);

    if (count < 0)
    {
        line += count;
        count = -count;
    }
    count = min(count, (4 - line));

    int i = 0;
    bool blah = true;

    // push the array
    lua_newtable(L);
    while ((i < count) && blah)
    {
        blah = ReadVDPPaletteLine(i + line, (unsigned short *)lua_newuserdata(L, 32));
        if (blah)
            lua_rawseti(L, -2, ++i);
        else
            lua_pop(L, 1);
    }
    lua_pushinteger(L, i);
    lua_insert(L, -2);

    return 2;
}
DEFINE_LUA_FUNCTION(vdp_writepalette, "[line,]paldata")
{
    short line = 0;
    int ind = 1;
    if (lua_isnumber(L, 1))
    {
        line = luaL_checkinteger(L, 1) - 1;
        ind++;
    }
    int count = 0;

    int type = lua_type(L, ind);
    if (type == LUA_TUSERDATA)
    {
        if (WriteVDPPaletteLine(line, (unsigned short *)lua_touserdata(L, ind)))
            count++;
        lua_pushinteger(L, count);
        return 1;
    }

    if (type != LUA_TTABLE)
        return 0;

    int i = 1;
    bool blah = true;
    do {
        lua_rawgeti(L, ind, i++);
        if (lua_type(L, -1) == LUA_TUSERDATA)
        {
            blah = WriteVDPPaletteLine(line++, (unsigned short *)lua_touserdata(L, -1));
            count++;
        }
        else
            blah = false;
        lua_pop(L, 1);
    } while (blah);

    lua_pushinteger(L, count);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_registerbefore, "func")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFOREEMULATION]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFOREEMULATION]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_registerafter, "func")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTEREMULATION]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTEREMULATION]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_registerexit, "func")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFOREEXIT]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFOREEXIT]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_registerstart, "func")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_ONSTART]);
    lua_insert(L, 1);
    lua_pushvalue(L, -1); // copy the function so we can also call it
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_ONSTART]);
    if (!lua_isnil(L, -1) && (Game))
        lua_call(L, 0, 0); // call the function now since the game has already started and this start function hasn't been called yet
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(gui_register, "func")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTEREMULATIONGUI]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTEREMULATIONGUI]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(state_registersave, "func[,savekey]")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!lua_isnoneornil(L, 2))
        SetSaveKey(GetCurrentInfo(), rawToCString(L, 2));
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFORESAVE]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFORESAVE]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}
DEFINE_LUA_FUNCTION(state_registerload, "func[,loadkey]")
{
    if (!lua_isnil(L, 1))
        luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!lua_isnoneornil(L, 2))
        SetLoadKey(GetCurrentInfo(), rawToCString(L, 2));
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTERLOAD]);
    lua_insert(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_AFTERLOAD]);
    StopScriptIfFinished(luaStateToUIDMap[L]);
    return 1;
}

DEFINE_LUA_FUNCTION(input_registerhotkey, "keynum,func")
{
    int hotkeyNumber = luaL_checkinteger(L, 1);
    if (hotkeyNumber < 1 || hotkeyNumber > 16)
    {
        luaL_error(L, "input.registerhotkey(n,func) requires 1 <= n <= 16, but got n = %d.", hotkeyNumber);
        return 0;
    }
    else
    {
        const char* key = luaCallIDStrings[LUACALL_SCRIPT_HOTKEY_1 + hotkeyNumber - 1];
        lua_getfield(L, LUA_REGISTRYINDEX, key);
        lua_replace(L, 1);
        if (!lua_isnil(L, 2))
            luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_settop(L, 2);
        lua_setfield(L, LUA_REGISTRYINDEX, key);
        StopScriptIfFinished(luaStateToUIDMap[L]);
        return 1;
    }
}

static int doPopup(lua_State* L, const char* deftype, const char* deficon)
{
    const char* str = toCString(L, 1);
    const char* type = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : deftype;
    const char* icon = lua_type(L, 3) == LUA_TSTRING ? lua_tostring(L, 3) : deficon;

    int itype = -1, iters = 0;
    while (itype == -1 && iters++ < 2)
    {
        if (!stricmp(type, "ok")) itype = 0;
        else if (!stricmp(type, "yesno")) itype = 1;
        else if (!stricmp(type, "yesnocancel")) itype = 2;
        else if (!stricmp(type, "okcancel")) itype = 3;
        else if (!stricmp(type, "abortretryignore")) itype = 4;
        else type = deftype;
    }
    assert(itype >= 0 && itype <= 4);
    if (!(itype >= 0 && itype <= 4)) itype = 0;

    int iicon = -1; iters = 0;
    while (iicon == -1 && iters++ < 2)
    {
        if (!stricmp(icon, "message") || !stricmp(icon, "notice")) iicon = 0;
        else if (!stricmp(icon, "question")) iicon = 1;
        else if (!stricmp(icon, "warning")) iicon = 2;
        else if (!stricmp(icon, "error")) iicon = 3;
        else icon = deficon;
    }
    assert(iicon >= 0 && iicon <= 3);
    if (!(iicon >= 0 && iicon <= 3)) iicon = 0;

    static const char * const titles[] = { "Notice", "Question", "Warning", "Error" };
    const char* answer = "ok";
#ifdef _WIN32
    static const int etypes[] = { MB_OK, MB_YESNO, MB_YESNOCANCEL, MB_OKCANCEL, MB_ABORTRETRYIGNORE };
    static const int eicons[] = { MB_ICONINFORMATION, MB_ICONQUESTION, MB_ICONWARNING, MB_ICONERROR };
    DialogsOpen++;
    int uid = luaStateToUIDMap[L];
    EnableWindow(HWnd, false);
    if (Full_Screen)
    {
        while (ShowCursor(false) >= 0);
        while (ShowCursor(true) < 0);
    }
    int ianswer = MessageBox((HWND)uid, str, titles[iicon], etypes[itype] | eicons[iicon]);
    EnableWindow(HWnd, true);
    DialogsOpen--;
    switch (ianswer)
    {
    case IDOK: answer = "ok"; break;
    case IDCANCEL: answer = "cancel"; break;
    case IDABORT: answer = "abort"; break;
    case IDRETRY: answer = "retry"; break;
    case IDIGNORE: answer = "ignore"; break;
    case IDYES: answer = "yes"; break;
    case IDNO: answer = "no"; break;
    }
#else
    // NYI (assume first answer for now)
    switch (itype)
    {
    case 0: case 3: answer = "ok"; break;
    case 1: case 2: answer = "yes"; break;
    case 4: answer = "abort"; break;
    }
#endif

    lua_pushstring(L, answer);
    return 1;
}

// string gui.popup(string message, string type = "ok", string icon = "message")
// string input.popup(string message, string type = "yesno", string icon = "question")
DEFINE_LUA_FUNCTION(gui_popup, "message[,type=\"ok\"[,icon=\"message\"]]")
{
    return doPopup(L, "ok", "message");
}
DEFINE_LUA_FUNCTION(input_popup, "message[,type=\"yesno\"[,icon=\"question\"]]")
{
    return doPopup(L, "yesno", "question");
}

const char *prompt_str = new char[1024];
const char *prompt_default = new char[1024];
int prompt_maxlength;
char *prompt_result = new char[1024];

LRESULT CALLBACK LuaPromptProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT r;
    RECT r2;
    int dx1, dy1, dx2, dy2;

    const char* msg = "";
    char* length = "";
    //int uid;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        if (Full_Screen)
        {
            while (ShowCursor(false) >= 0);
            while (ShowCursor(true) < 0);
        }

        GetWindowRect(HWnd, &r);
        dx1 = (r.right - r.left) / 2;
        dy1 = (r.bottom - r.top) / 2;

        GetWindowRect(hDlg, &r2);
        dx2 = (r2.right - r2.left) / 2;
        dy2 = (r2.bottom - r2.top) / 2;

        SetWindowPos(hDlg, NULL, r.left + dx1 - dx2, r.top + dy1 + dy2, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

        SendDlgItemMessage(hDlg, IDC_PROMPT_TEXT, WM_SETTEXT, 0, (LPARAM)prompt_str);

        SendDlgItemMessage(hDlg, IDC_PROMPT_EDIT, EM_LIMITTEXT, (WPARAM)prompt_maxlength, 0);
        SendDlgItemMessage(hDlg, IDC_PROMPT_EDIT, WM_SETTEXT, 0, (LPARAM)prompt_default);

        sprintf(Str_Tmp, "Maximum length is %i characters.", prompt_maxlength);

        SendDlgItemMessage(hDlg, IDC_PROMPT_TEXT2, WM_SETTEXT, 0, (LPARAM)Str_Tmp);

        return true;
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            if (Full_Screen)
            {
                while (ShowCursor(true) < 0);
                while (ShowCursor(false) >= 0);
            }

            prompt_result = new char[prompt_maxlength];
            GetDlgItemText(hDlg, IDC_PROMPT_EDIT, prompt_result, prompt_maxlength + 1);

            EndDialog(hDlg, true);
            return true;
            break;
        }
        case ID_CANCEL:
        case IDCANCEL:
            if (Full_Screen)
            {
                while (ShowCursor(true) < 0);
                while (ShowCursor(false) >= 0);
            }

            prompt_result = "";

            EndDialog(hDlg, false);
            return true;
            break;
        }
        break;

    case WM_CLOSE:
        if (Full_Screen)
        {
            while (ShowCursor(true) < 0);
            while (ShowCursor(false) >= 0);
        }
        prompt_result = "";
        EndDialog(hDlg, false);

        return true;
        break;
    }

    return false;
}

static int doPrompt(lua_State* L, const char *defstr, const char *defvalue, const int deflength)
{
    prompt_str = (const char *)(lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : defstr);
    prompt_default = (const char *)(lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : defvalue);
    prompt_maxlength = lua_type(L, 3) == LUA_TNUMBER ? lua_tointeger(L, 3) : deflength;

    if (Full_Screen)
    {
        while (ShowCursor(false) >= 0);
        while (ShowCursor(true) < 0);
    }

    EnableWindow(HWnd, false);

    if (DialogBox(ghInstance, MAKEINTRESOURCE(IDD_PROMPT), (HWND)luaStateToUIDMap[L], (DLGPROC)LuaPromptProc))
        lua_pushstring(L, prompt_result);
    else
        lua_pushnil(L);

    EnableWindow(HWnd, true);

    return 1;
}

// string input.prompt()
// string input.prompt(string message = "", string default = "", int maxlength = 100)
DEFINE_LUA_FUNCTION(input_prompt, "[message=\"\"[,default=\"\"[,maxlength=100]]]")
{
    return doPrompt(L, "", "", 100);
}

static const char* FilenameFromPath(const char* path)
{
    const char* slash1 = strrchr(path, '\\');
    const char* slash2 = strrchr(path, '/');
    if (slash1) slash1++;
    if (slash2) slash2++;
    const char* rv = path;
    rv = max(rv, slash1);
    rv = max(rv, slash2);
    if (!rv) rv = "";
    return rv;
}

static void toCStringConverter(lua_State* L, int i, char*& ptr, int& remaining);

// compare the contents of two items on the Lua stack to determine if they differ
// only works for relatively simple, saveable items (numbers, strings, bools, nil, and possibly-nested tables of those, up to a certain max length)
// not the best implementation, but good enough for what it's currently used for
static bool luaValueContentsDiffer(lua_State* L, int idx1, int idx2)
{
    static const int maxLen = 8192;
    static char str1[maxLen];
    static char str2[maxLen];
    str1[0] = 0;
    str2[0] = 0;
    char* ptr1 = str1;
    char* ptr2 = str2;
    int remaining1 = maxLen;
    int remaining2 = maxLen;
    toCStringConverter(L, idx1, ptr1, remaining1);
    toCStringConverter(L, idx2, ptr2, remaining2);
    return (remaining1 != remaining2) || (strcmp(str1, str2) != 0);
}

// fills output with the path
// also returns a pointer to the first character in the filename (non-directory) part of the path
static char* ConstructScriptSaveDataPath(char* output, int bufferSize, LuaContextInfo& info)
{
    Get_State_File_Name(output);
    char* slash1 = strrchr(output, '\\');
    char* slash2 = strrchr(output, '/');
    if (slash1) slash1[1] = '\0';
    if (slash2) slash2[1] = '\0';
    char* rv = output + strlen(output);
    strncat(output, "u.", bufferSize - (strlen(output) + 1));
    if (!info.dataSaveLoadKeySet)
        strncat(output, FilenameFromPath(info.lastFilename.c_str()), bufferSize - (strlen(output) + 1));
    else
    {
        size_t output_len = strlen(output);
        snprintf(output + output_len, bufferSize - (output_len + 1), "%X", info.dataSaveKey);
    }
    strncat(output, ".luasav", bufferSize - (strlen(output) + 1));
    return rv;
}

// gens.persistglobalvariables({
//   variable1 = defaultvalue1,
//   variable2 = defaultvalue2,
//   etc
// })
// takes a table with variable names as the keys and default values as the values,
// and defines each of those variables names as a global variable,
// setting them equal to the values they had the last time the script exited,
// or (if that isn't available) setting them equal to the provided default values.
// as a special case, if you want the default value for a variable to be nil,
// then put the variable name alone in quotes as an entry in the table without saying "= nil".
// this special case is because tables in lua don't store nil valued entries.
// also, if you change the default value that will reset the variable to the new default.
DEFINE_LUA_FUNCTION(gens_persistglobalvariables, "variabletable")
{
    int uid = luaStateToUIDMap[L];
    LuaContextInfo& info = GetCurrentInfo();

    // construct a path we can load the persistent variables from
    char path[1024] = { 0 };
    char* pathTypeChrPtr = ConstructScriptSaveDataPath(path, 1024, info);

    // load the previously-saved final variable values from file
    LuaSaveData exitData;
    {
        *pathTypeChrPtr = 'e';
        FILE* persistFile = fopen(path, "rb");
        if (persistFile)
        {
            exitData.ImportRecords(persistFile);
            fclose(persistFile);
        }
    }

    // load the previously-saved default variable values from file
    LuaSaveData defaultData;
    {
        *pathTypeChrPtr = 'd';
        FILE* defaultsFile = fopen(path, "rb");
        if (defaultsFile)
        {
            defaultData.ImportRecords(defaultsFile);
            fclose(defaultsFile);
        }
    }

    // loop through the passed-in variables,
    // exposing a global variable to the script for each one
    // while also keeping a record of their names
    // so we can save them (to the persistFile) later when the script exits
    int numTables = lua_gettop(L);
    for (int i = 1; i <= numTables; i++)
    {
        luaL_checktype(L, i, LUA_TTABLE);

        lua_pushnil(L); // before first key
        int keyIndex = lua_gettop(L);
        int valueIndex = keyIndex + 1;
        while (lua_next(L, i))
        {
            int keyType = lua_type(L, keyIndex);
            int valueType = lua_type(L, valueIndex);
            if (keyType == LUA_TSTRING && valueType <= LUA_TTABLE && valueType != LUA_TLIGHTUSERDATA)
            {
                // variablename = defaultvalue,

                // duplicate the key first because lua_next() needs to eat that
                lua_pushvalue(L, keyIndex);
                lua_insert(L, keyIndex);
            }
            else if (keyType == LUA_TNUMBER && valueType == LUA_TSTRING)
            {
                // "variablename",
                // or [index] = "variablename",

                // defaultvalue is assumed to be nil
                lua_pushnil(L);
            }
            else
            {
                luaL_error(L, "'%s' = '%s' entries are not allowed in the table passed to gens.persistglobalvariables()", lua_typename(L, keyType), lua_typename(L, valueType));
            }

            int varNameIndex = valueIndex;
            int defaultIndex = valueIndex + 1;

            // keep track of the variable name for later
            const char* varName = lua_tostring(L, varNameIndex);
            info.persistVars.push_back(varName);
            unsigned int varNameCRC = crc32(0, (const unsigned char*)varName, strlen(varName));
            info.newDefaultData.SaveRecordPartial(uid, varNameCRC, defaultIndex);

            // load the previous default value for this variable if it exists.
            // if the new default is different than the old one,
            // assume the user wants to set the value to the new default value
            // instead of the previously-saved exit value.
            bool attemptPersist = true;
            defaultData.LoadRecord(uid, varNameCRC, 1);
            lua_pushnil(L);
            if (luaValueContentsDiffer(L, defaultIndex, defaultIndex + 1))
                attemptPersist = false;
            lua_settop(L, defaultIndex);

            if (attemptPersist)
            {
                // load the previous saved value for this variable if it exists
                exitData.LoadRecord(uid, varNameCRC, 1);
                if (lua_gettop(L) > defaultIndex)
                    lua_remove(L, defaultIndex); // replace value with loaded record
                lua_settop(L, defaultIndex);
            }

            // set the global variable
            lua_settable(L, LUA_GLOBALSINDEX);

            assert(lua_gettop(L) == keyIndex);
        }
    }

    return 0;
}

static const char* deferredGUIIDString = "lazygui";

// store the most recent C function call from Lua (and all its arguments)
// for later evaluation
void DeferFunctionCall(lua_State* L, const char* idstring)
{
    // there might be a cleaner way of doing this using lua_pushcclosure and lua_getref

    int num = lua_gettop(L);

    // get the C function pointer
    //lua_CFunction cf = lua_tocfunction(L, -(num+1));
    lua_CFunction cf = (L->ci->func)->value.gc->cl.c.f;
    assert(cf);
    lua_pushcfunction(L, cf);

    // make a list of the function and its arguments (and also pop those arguments from the stack)
    lua_createtable(L, num + 1, 0);
    lua_insert(L, 1);
    for (int n = num + 1; n > 0; n--)
        lua_rawseti(L, 1, n);

    // put the list into a global array
    lua_getfield(L, LUA_REGISTRYINDEX, idstring);
    lua_insert(L, 1);
    int curSize = lua_objlen(L, 1);
    lua_rawseti(L, 1, curSize + 1);

    // clean the stack
    lua_settop(L, 0);
}
void CallDeferredFunctions(lua_State* L, const char* idstring)
{
    lua_settop(L, 0);
    lua_getfield(L, LUA_REGISTRYINDEX, idstring);
    int numCalls = lua_objlen(L, 1);
    for (int i = 1; i <= numCalls; i++)
    {
        lua_rawgeti(L, 1, i);  // get the function+arguments list
        int listSize = lua_objlen(L, 2);

        // push the arguments and the function
        for (int j = 1; j <= listSize; j++)
            lua_rawgeti(L, 2, j);

        // get and pop the function
        lua_CFunction cf = lua_tocfunction(L, -1);
        lua_pop(L, 1);

        // shift first argument to slot 1 and call the function
        lua_remove(L, 2);
        lua_remove(L, 1);
        cf(L);

        // prepare for next iteration
        lua_settop(L, 0);
        lua_getfield(L, LUA_REGISTRYINDEX, idstring);
    }

    // clear the list of deferred functions
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, idstring);
    LuaContextInfo& info = GetCurrentInfo();
    info.numDeferredGUIFuncs = 0;

    // clean the stack
    lua_settop(L, 0);
}

#define MAX_DEFERRED_COUNT 16384

bool DeferGUIFuncIfNeeded(lua_State* L)
{
    LuaContextInfo& info = GetCurrentInfo();
    if (info.speedMode == SPEEDMODE_MAXIMUM)
    {
        // if the mode is "maximum" then discard all GUI function calls
        // and pretend it was because we deferred them
        return true;
    }
    if (info.guiFuncsNeedDeferring)
    {
        if (info.numDeferredGUIFuncs < MAX_DEFERRED_COUNT)
        {
            // defer whatever function called this one until later
            DeferFunctionCall(L, deferredGUIIDString);
            info.numDeferredGUIFuncs++;
        }
        else
        {
            // too many deferred functions on the same frame
            // silently discard the rest
        }
        return true;
    }

    // ok to run the function right now
    return false;
}

void worry(lua_State* L, int intensity)
{
    LuaContextInfo& info = GetCurrentInfo();
    info.worryCount += intensity;
}

static inline bool isalphaorunderscore(char c)
{
    return isalpha(c) || c == '_';
}

static std::vector<const void*> s_tableAddressStack; // prevents infinite recursion of a table within a table (when cycle is found, print something like table:parent)
static std::vector<const void*> s_metacallStack; // prevents infinite recursion if something's __tostring returns another table that contains that something (when cycle is found, print the inner result without using __tostring)

#define APPENDPRINT { int _n = snprintf(ptr, remaining,
#define END ); if(_n >= 0) { ptr += _n; remaining -= _n; } else { remaining = 0; } }
static void toCStringConverter(lua_State* L, int i, char*& ptr, int& remaining)
{
    if (remaining <= 0)
        return;

    const char* str = ptr; // for debugging

    // if there is a __tostring metamethod then call it
    int usedMeta = luaL_callmeta(L, i, "__tostring");
    if (usedMeta)
    {
        std::vector<const void*>::const_iterator foundCycleIter = std::find(s_metacallStack.begin(), s_metacallStack.end(), lua_topointer(L, i));
        if (foundCycleIter != s_metacallStack.end())
        {
            lua_pop(L, 1);
            usedMeta = false;
        }
        else
        {
            s_metacallStack.push_back(lua_topointer(L, i));
            i = lua_gettop(L);
        }
    }

    switch (lua_type(L, i))
    {
    case LUA_TNONE: break;
    case LUA_TNIL: APPENDPRINT "nil" END break;
    case LUA_TBOOLEAN: APPENDPRINT lua_toboolean(L, i) ? "true" : "false" END break;
    case LUA_TSTRING: APPENDPRINT "%s", lua_tostring(L, i) END break;
    case LUA_TNUMBER: APPENDPRINT "%.12Lg", lua_tonumber(L, i) END break;
    case LUA_TFUNCTION:
        if ((L->base + i - 1)->value.gc->cl.c.isC)
        {
            lua_CFunction func = lua_tocfunction(L, i);
            std::map<lua_CFunction, const char*>::iterator iter = s_cFuncInfoMap.find(func);
            if (iter == s_cFuncInfoMap.end())
                goto defcase;
            APPENDPRINT "function(%s)", iter->second END
        }
        else
        {
            APPENDPRINT "function(" END
                Proto* p = (L->base + i - 1)->value.gc->cl.l.p;
            int numParams = p->numparams + (p->is_vararg ? 1 : 0);
            for (int n = 0; n < p->numparams; n++)
            {
                APPENDPRINT "%s", getstr(p->locvars[n].varname) END
                    if (n != numParams - 1)
                        APPENDPRINT "," END
            }
            if (p->is_vararg)
                APPENDPRINT "..." END
                APPENDPRINT ")" END
        }
        break;
    defcase:default: APPENDPRINT "%s:%p", luaL_typename(L, i), lua_topointer(L, i) END break;
    case LUA_TTABLE:
    {
        // first make sure there's enough stack space
        if (!lua_checkstack(L, 4))
        {
            // note that even if lua_checkstack never returns false,
            // that doesn't mean we didn't need to call it,
            // because calling it retrieves stack space past LUA_MINSTACK
            goto defcase;
        }

        std::vector<const void*>::const_iterator foundCycleIter = std::find(s_tableAddressStack.begin(), s_tableAddressStack.end(), lua_topointer(L, i));
        if (foundCycleIter != s_tableAddressStack.end())
        {
            int parentNum = s_tableAddressStack.end() - foundCycleIter;
            if (parentNum > 1)
                APPENDPRINT "%s:parent^%d", luaL_typename(L, i), parentNum END
            else
                APPENDPRINT "%s:parent", luaL_typename(L, i) END
        }
        else
        {
            s_tableAddressStack.push_back(lua_topointer(L, i));
            struct Scope { ~Scope() { s_tableAddressStack.pop_back(); } } scope;

            APPENDPRINT "{" END

                lua_pushnil(L); // first key
            int keyIndex = lua_gettop(L);
            int valueIndex = keyIndex + 1;
            bool first = true;
            bool skipKey = true; // true if we're still in the "array part" of the table
            lua_Number arrayIndex = (lua_Number)0;
            while (lua_next(L, i))
            {
                if (first)
                    first = false;
                else
                    APPENDPRINT ", " END
                    if (skipKey)
                    {
                        arrayIndex += (lua_Number)1;
                        bool keyIsNumber = (lua_type(L, keyIndex) == LUA_TNUMBER);
                        skipKey = keyIsNumber && (lua_tonumber(L, keyIndex) == arrayIndex);
                    }
                if (!skipKey)
                {
                    bool keyIsString = (lua_type(L, keyIndex) == LUA_TSTRING);
                    bool invalidLuaIdentifier = (!keyIsString || !isalphaorunderscore(*lua_tostring(L, keyIndex)));
                    if (invalidLuaIdentifier)
                        if (keyIsString)
                            APPENDPRINT "['" END
                        else
                            APPENDPRINT "[" END

                            toCStringConverter(L, keyIndex, ptr, remaining); // key

                    if (invalidLuaIdentifier)
                        if (keyIsString)
                            APPENDPRINT "']=" END
                        else
                            APPENDPRINT "]=" END
                    else
                            APPENDPRINT "=" END
                }

                bool valueIsString = (lua_type(L, valueIndex) == LUA_TSTRING);
                if (valueIsString)
                    APPENDPRINT "'" END

                    toCStringConverter(L, valueIndex, ptr, remaining); // value

                if (valueIsString)
                    APPENDPRINT "'" END

                    lua_pop(L, 1);

                if (remaining <= 0)
                {
                    lua_settop(L, keyIndex - 1); // stack might not be clean yet if we're breaking early
                    break;
                }
            }
            APPENDPRINT "}" END
        }
    }	break;
    }

    if (usedMeta)
    {
        s_metacallStack.pop_back();
        lua_pop(L, 1);
    }
}

static const int s_tempStrMaxLen = 64 * 1024;
static char s_tempStr[s_tempStrMaxLen];

static char* rawToCString(lua_State* L, int idx)
{
    int a = idx > 0 ? idx : 1;
    int n = idx > 0 ? idx : lua_gettop(L);

    char* ptr = s_tempStr;
    *ptr = 0;

    int remaining = s_tempStrMaxLen;
    for (int i = a; i <= n; i++)
    {
        toCStringConverter(L, i, ptr, remaining);
        if (i != n)
            APPENDPRINT " " END
    }

    if (remaining < 3)
    {
        while (remaining < 6)
            remaining++, ptr--;
        APPENDPRINT "..." END
    }
    APPENDPRINT "\r\n" END
        // the trailing newline is so print() can avoid having to do wasteful things to print its newline
        // (string copying would be wasteful and calling info.print() twice can be extremely slow)
        // at the cost of functions that don't want the newline needing to trim off the last two characters
        // (which is a very fast operation and thus acceptable in this case)

        return s_tempStr;
}
#undef APPENDPRINT
#undef END

// replacement for luaB_tostring() that is able to show the contents of tables (and formats numbers better, and show function prototypes)
// can be called directly from lua via tostring(), assuming tostring hasn't been reassigned
DEFINE_LUA_FUNCTION(tostring, "...")
{
    char* str = rawToCString(L);
    str[strlen(str) - 2] = 0; // hack: trim off the \r\n (which is there to simplify the print function's task)
    lua_pushstring(L, str);
    return 1;
}

// like rawToCString, but will check if the global Lua function tostring()
// has been replaced with a custom function, and call that instead if so
static const char* toCString(lua_State* L, int idx)
{
    int a = idx > 0 ? idx : 1;
    int n = idx > 0 ? idx : lua_gettop(L);
    lua_getglobal(L, "tostring");
    lua_CFunction cf = lua_tocfunction(L, -1);
    if (cf == tostring) // optimization: if using our own C tostring function, we can bypass the call through Lua and all the string object allocation that would entail
    {
        lua_pop(L, 1);
        return rawToCString(L, idx);
    }
    else // if the user overrided the tostring function, we have to actually call it and store the temporarily allocated string it returns
    {
        lua_pushstring(L, "");
        for (int i = a; i <= n; i++) {
            lua_pushvalue(L, -2);  // function to be called
            lua_pushvalue(L, i);   // value to print
            lua_call(L, 1, 1);
            if (lua_tostring(L, -1) == NULL)
                luaL_error(L, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
            lua_pushstring(L, (i < n) ? " " : "\r\n");
            lua_concat(L, 3);
        }
        const char* str = lua_tostring(L, -1);
        strncpy(s_tempStr, str, s_tempStrMaxLen);
        s_tempStr[s_tempStrMaxLen - 1] = 0;
        lua_pop(L, 2);
        return s_tempStr;
    }
}

// replacement for luaB_print() that goes to the appropriate textbox instead of stdout
DEFINE_LUA_FUNCTION(print, "...")
{
    const char* str = toCString(L);

    int uid = luaStateToUIDMap[L];
    LuaContextInfo& info = GetCurrentInfo();

    if (info.print)
        info.print(uid, str);
    else
        puts(str);

    worry(L, 100);
    return 0;
}

DEFINE_LUA_FUNCTION(gens_message, "str")
{
    const char* str = toCString(L);
    Put_Info_NonImmediate((char*)str, 500);
    return 0;
}

// provides an easy way to copy a table from Lua
// (simple assignment only makes an alias, but sometimes an independent table is desired)
// currently this function only performs a shallow copy,
// but I think it should be changed to do a deep copy (possibly of configurable depth?)
// that maintains the internal table reference structure
DEFINE_LUA_FUNCTION(copytable, "origtable")
{
    int origIndex = 1; // we only care about the first argument
    int origType = lua_type(L, origIndex);
    if (origType == LUA_TNIL)
    {
        lua_pushnil(L);
        return 1;
    }
    if (origType != LUA_TTABLE)
    {
        luaL_typerror(L, 1, lua_typename(L, LUA_TTABLE));
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, lua_objlen(L, 1), 0);
    int copyIndex = lua_gettop(L);

    lua_pushnil(L); // first key
    int keyIndex = lua_gettop(L);
    int valueIndex = keyIndex + 1;

    while (lua_next(L, origIndex))
    {
        lua_pushvalue(L, keyIndex);
        lua_pushvalue(L, valueIndex);
        lua_rawset(L, copyIndex); // copytable[key] = value
        lua_pop(L, 1);
    }

    // copy the reference to the metatable as well, if any
    if (lua_getmetatable(L, origIndex))
        lua_setmetatable(L, copyIndex);

    return 1; // return the new table
}

// because print traditionally shows the address of tables,
// and the print function I provide instead shows the contents of tables,
// I also provide this function
// (otherwise there would be no way to see a table's address, AFAICT)
DEFINE_LUA_FUNCTION(addressof, "table_or_function")
{
    const void* ptr = lua_topointer(L, -1);
    lua_pushinteger(L, (lua_Integer)ptr);
    return 1;
}

// the following bit operations are ported from LuaBitOp 1.0.1,
// because it can handle the sign bit (bit 31) correctly.

/*
** Lua BitOp -- a bit operations library for Lua 5.1.
** http://bitop.luajit.org/
**
** Copyright (C) 2008-2009 Mike Pall. All rights reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
** [ MIT license: http://www.opensource.org/licenses/mit-license.php ]
*/

#ifdef _MSC_VER
/* MSVC is stuck in the last century and doesn't have C99's stdint.h. */
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

typedef int32_t SBits;
typedef uint32_t UBits;

typedef union {
    lua_Number n;
#ifdef LUA_NUMBER_DOUBLE
    uint64_t b;
#else
    UBits b;
#endif
} BitNum;

/* Convert argument to bit type. */
static UBits barg(lua_State *L, int idx)
{
    BitNum bn;
    UBits b;
    bn.n = lua_tonumber(L, idx);
#if defined(LUA_NUMBER_DOUBLE)
    bn.n += 6755399441055744.0;  /* 2^52+2^51 */
#ifdef SWAPPED_DOUBLE
    b = (UBits)(bn.b >> 32);
#else
    b = (UBits)(bn.b & (UBits)~0);
#endif
#elif defined(LUA_NUMBER_INT) || defined(LUA_NUMBER_LONG) || \
      defined(LUA_NUMBER_LONGLONG) || defined(LUA_NUMBER_LONG_LONG) || \
      defined(LUA_NUMBER_LLONG)
    if (sizeof(UBits) == sizeof(lua_Number))
        b = bn.b;
    else
        b = (UBits)(SBits)bn.n;
#elif defined(LUA_NUMBER_FLOAT)
#error "A 'float' lua_Number type is incompatible with this library"
#else
#error "Unknown number type, check LUA_NUMBER_* in luaconf.h"
#endif
    if (b == 0 && !lua_isnumber(L, idx))
        luaL_typerror(L, idx, "number");
    return b;
}

/* Return bit type. */
#define BRET(b)  lua_pushnumber(L, (lua_Number)(SBits)(b)); return 1;

DEFINE_LUA_FUNCTION(bit_tobit, "x") { BRET(barg(L, 1)) }
DEFINE_LUA_FUNCTION(bit_bnot, "x") { BRET(~barg(L, 1)) }

#define BIT_OP(func, opr) \
  DEFINE_LUA_FUNCTION(func, "x1 [,x2...]") { int i; UBits b = barg(L, 1); \
    for (i = lua_gettop(L); i > 1; i--) b opr barg(L, i); BRET(b) }
BIT_OP(bit_band, &=)
BIT_OP(bit_bor, |=)
BIT_OP(bit_bxor, ^=)

#define bshl(b, n)  (b << n)
#define bshr(b, n)  (b >> n)
#define bsar(b, n)  ((SBits)b >> n)
#define brol(b, n)  ((b << n) | (b >> (32-n)))
#define bror(b, n)  ((b << (32-n)) | (b >> n))
#define BIT_SH(func, fn) \
  DEFINE_LUA_FUNCTION(func, "x, n") { \
    UBits b = barg(L, 1); UBits n = barg(L, 2) & 31; BRET(fn(b, n)) }
BIT_SH(bit_lshift, bshl)
BIT_SH(bit_rshift, bshr)
BIT_SH(bit_arshift, bsar)
BIT_SH(bit_rol, brol)
BIT_SH(bit_ror, bror)

DEFINE_LUA_FUNCTION(bit_bswap, "x")
{
    UBits b = barg(L, 1);
    b = (b >> 24) | ((b >> 8) & 0xff00) | ((b & 0xff00) << 8) | (b << 24);
    BRET(b)
}

DEFINE_LUA_FUNCTION(bit_tohex, "x [,n]")
{
    UBits b = barg(L, 1);
    SBits n = lua_isnone(L, 2) ? 8 : (SBits)barg(L, 2);
    const char *hexdigits = "0123456789abcdef";
    char buf[8];
    int i;
    if (n < 0) { n = -n; hexdigits = "0123456789ABCDEF"; }
    if (n > 8) n = 8;
    for (i = (int)n; --i >= 0;) { buf[i] = hexdigits[b & 15]; b >>= 4; }
    lua_pushlstring(L, buf, (size_t)n);
    return 1;
}

static const struct luaL_Reg bit_funcs[] = {
    { "tobit", bit_tobit },
    { "bnot", bit_bnot },
    { "band", bit_band },
    { "bor", bit_bor },
    { "bxor", bit_bxor },
    { "lshift", bit_lshift },
    { "rshift", bit_rshift },
    { "arshift", bit_arshift },
    { "rol", bit_rol },
    { "ror", bit_ror },
    { "bswap", bit_bswap },
    { "tohex", bit_tohex },
    { NULL, NULL }
};

/* Signed right-shifts are implementation-defined per C89/C99.
** But the de facto standard are arithmetic right-shifts on two's
** complement CPUs. This behaviour is required here, so test for it.
*/
#define BAD_SAR		(bsar(-8, 2) != (SBits)-2)

bool luabitop_validate(lua_State *L) // originally named as luaopen_bit
{
    UBits b;
    lua_pushnumber(L, (lua_Number)1437217655L);
    b = barg(L, -1);
    if (b != (UBits)1437217655L || BAD_SAR) {  /* Perform a simple self-test. */
        const char *msg = "compiled with incompatible luaconf.h";
#ifdef LUA_NUMBER_DOUBLE
#ifdef _WIN32
        if (b == (UBits)1610612736L)
            msg = "use D3DCREATE_FPU_PRESERVE with DirectX";
#endif
        if (b == (UBits)1127743488L)
            msg = "not compiled with SWAPPED_DOUBLE";
#endif
        if (BAD_SAR)
            msg = "arithmetic right-shift broken";
        luaL_error(L, "bit library self-test failed (%s)", msg);
        return false;
    }
    return true;
}

// LuaBitOp ends here

DEFINE_LUA_FUNCTION(bitshift, "num,shift")
{
    int shift = luaL_checkinteger(L, 2);
    if (shift < 0) {
        lua_pushinteger(L, -shift);
        lua_replace(L, 2);
        return bit_lshift(L);
    }
    else
        return bit_rshift(L);
}

DEFINE_LUA_FUNCTION(bitbit, "whichbit")
{
    int rv = 0;
    int numArgs = lua_gettop(L);
    for (int i = 1; i <= numArgs; i++) {
        int where = luaL_checkinteger(L, i);
        if (where >= 0 && where < 32)
            rv |= (1 << where);
    }
    lua_settop(L, 0);
    BRET(rv);
}

int gens_wait(lua_State* L);

void indicateBusy(lua_State* L, bool busy)
{
    // disabled because there have been complaints about this message being useless spam.
    // the script window's title changing should be sufficient, I guess.
    /*	if(busy)
        {
        const char* fmt = "script became busy (frozen?)";
        va_list argp;
        va_start(argp, fmt);
        luaL_where(L, 0);
        lua_pushvfstring(L, fmt, argp);
        va_end(argp);
        lua_concat(L, 2);
        LuaContextInfo& info = GetCurrentInfo();
        int uid = luaStateToUIDMap[L];
        if(info.print)
        {
        info.print(uid, lua_tostring(L,-1));
        info.print(uid, "\r\n");
        }
        else
        {
        fprintf(stderr, "%s\n", lua_tostring(L,-1));
        }
        lua_pop(L, 1);
        }
        */
#ifdef _WIN32
    int uid = luaStateToUIDMap[L];
    HWND hDlg = (HWND)uid;
    char str[1024];
    GetWindowText(hDlg, str, 1000);
    char* extra = strchr(str, '<');
    if (busy)
    {
        if (!extra)
            extra = str + strlen(str), *extra++ = ' ';
        strcpy(extra, "<BUSY>");
    }
    else
    {
        if (extra)
            extra[-1] = 0;
    }
    SetWindowText(hDlg, str);
#endif
}

#define HOOKCOUNT 4096
#define MAX_WORRY_COUNT 6000
void LuaRescueHook(lua_State* L, lua_Debug *dbg)
{
    LuaContextInfo& info = GetCurrentInfo();

    info.worryCount++;

    if (info.stopWorrying && !info.panic)
    {
        if (info.worryCount > (MAX_WORRY_COUNT >> 2))
        {
            // the user already said they're OK with the script being frozen,
            // but we don't trust their judgement completely,
            // so periodically update the main loop so they have a chance to manually stop it
            info.worryCount = 0;
            gens_wait(L);
            info.stopWorrying = true;
        }
        return;
    }

    if (info.worryCount > MAX_WORRY_COUNT || info.panic)
    {
        info.worryCount = 0;
        info.stopWorrying = false;

        bool stoprunning = true;
        bool stopworrying = true;
        if (!info.panic)
        {
            Clear_Sound_Buffer();
#if defined(ASK_USER_ON_FREEZE) && defined(_WIN32)
            DialogsOpen++;
            int answer = MessageBox(HWnd, "A Lua script has been running for quite a while. Maybe it is in an infinite loop.\n\nWould you like to stop the script?\n\n(Yes to stop it now,\n No to keep running and not ask again,\n Cancel to keep running but ask again later)", "Lua Alert", MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONASTERISK);
            DialogsOpen--;
            if (answer == IDNO)
                stoprunning = false;
            if (answer == IDCANCEL)
                stopworrying = false;
#else
            stoprunning = false;
#endif
        }

        if (!stoprunning && stopworrying)
        {
            info.stopWorrying = true; // don't remove the hook because we need it still running for RequestAbortLuaScript to work
            indicateBusy(info.L, true);
        }

        if (stoprunning)
        {
            //lua_sethook(L, NULL, 0, 0);
            assert(L->errfunc || L->errorJmp);
            luaL_error(L, info.panic ? info.panicMessage : "terminated by user");
        }

        info.panic = false;
    }
}

void printfToOutput(const char* fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    int len = vscprintf(fmt, list);
    char* str = new char[len + 1];
    vsprintf(str, fmt, list);
    va_end(list);
    LuaContextInfo& info = GetCurrentInfo();
    if (info.print)
    {
        lua_State* L = info.L;
        int uid = luaStateToUIDMap[L];
        info.print(uid, str);
        info.print(uid, "\r\n");
        worry(L, 300);
    }
    else
    {
        fprintf(stdout, "%s\n", str);
    }
    delete[] str;
}

bool FailVerifyAtFrameBoundary(lua_State* L, const char* funcName, int unstartedSeverity = 2, int inframeSeverity = 2)
{
    if (!Game)
    {
        static const char* msg = "cannot call %s() when emulation has not started.";
        switch (unstartedSeverity)
        {
        case 0: break;
        case 1: printfToOutput(msg, funcName); break;
        default: case 2: luaL_error(L, msg, funcName); break;
        }
        return true;
    }
    if (Inside_Frame)
    {
        static const char* msg = "cannot call %s() inside an emulation frame.";
        switch (inframeSeverity)
        {
        case 0: break;
        case 1: printfToOutput(msg, funcName); break;
        default: case 2: luaL_error(L, msg, funcName); break;
        }
        return true;
    }
    return false;
}

// acts similar to normal emulation update
// except without the user being able to activate emulator commands
DEFINE_LUA_FUNCTION(gens_emulateframe, "")
{
    if (FailVerifyAtFrameBoundary(L, "gens.emulateframe", 0, 1))
        return 0;

    Update_Emulation_One(HWnd);
    Prevent_Next_Frame_Skipping(); // so we don't skip a whole bunch of frames immediately after emulating many frames by this method

    worry(L, 300);
    return 0;
}

// acts as a fast-forward emulation update that still renders every frame
// and the user is unable to activate emulator commands during it
DEFINE_LUA_FUNCTION(gens_emulateframefastnoskipping, "")
{
    if (FailVerifyAtFrameBoundary(L, "gens.emulateframefastnoskipping", 0, 1))
        return 0;

    Update_Emulation_One_Before(HWnd);
    Update_Frame_Hook();
    Update_Emulation_After_Controlled(HWnd, true);
    Prevent_Next_Frame_Skipping(); // so we don't skip a whole bunch of frames immediately after a bout of fast-forward frames

    worry(L, 200);
    return 0;
}

// acts as a (very) fast-forward emulation update
// where the user is unable to activate emulator commands
DEFINE_LUA_FUNCTION(gens_emulateframefast, "")
{
    if (FailVerifyAtFrameBoundary(L, "gens.emulateframefast", 0, 1))
        return 0;

    disableVideoLatencyCompensationCount = VideoLatencyCompensation + 1;

    Update_Emulation_One_Before(HWnd);

    if (FrameCount % 16 == 0) // skip rendering 15 out of 16 frames
    {
        // update once and render
        Update_Frame_Hook();
        Update_Emulation_After_Controlled(HWnd, true);
    }
    else
    {
        // update once but skip rendering
        Update_Frame_Fast_Hook();
        Update_Emulation_After_Controlled(HWnd, false);
    }

    Prevent_Next_Frame_Skipping(); // so we don't skip a whole bunch of frames immediately AFTER a bout of fast-forward frames

    worry(L, 150);
    return 0;
}

// acts as an extremely-fast-forward emulation update
// that also doesn't render any graphics or generate any sounds,
// and the user is unable to activate emulator commands during it.
// if you load a savestate after calling this function,
// it should leave no trace of having been called,
// so you can do things like generate future emulation states every frame
// while the user continues to see and hear normal emulation
DEFINE_LUA_FUNCTION(gens_emulateframeinvisible, "")
{
    if (FailVerifyAtFrameBoundary(L, "gens.emulateframeinvisible", 0, 1))
        return 0;

    int oldDisableRamSearchUpdate = disableRamSearchUpdate;
    disableRamSearchUpdate = true;

    Update_Emulation_One_Before_Minimal();
    Update_Frame_Fast();
    UpdateLagCount();

    disableRamSearchUpdate = oldDisableRamSearchUpdate;

    // disable video latency compensation for a few frames
    // because it can get pretty slow if that's doing prediction updates every frame
    // when the lua script is also doing prediction updates
    disableVideoLatencyCompensationCount = VideoLatencyCompensation + 1;

    worry(L, 100);
    return 0;
}

DEFINE_LUA_FUNCTION(gens_speedmode, "mode")
{
    SpeedMode newSpeedMode = SPEEDMODE_NORMAL;
    if (lua_isnumber(L, 1))
        newSpeedMode = (SpeedMode)luaL_checkinteger(L, 1);
    else
    {
        const char* str = luaL_checkstring(L, 1);
        if (!stricmp(str, "normal"))
            newSpeedMode = SPEEDMODE_NORMAL;
        else if (!stricmp(str, "nothrottle"))
            newSpeedMode = SPEEDMODE_NOTHROTTLE;
        else if (!stricmp(str, "turbo"))
            newSpeedMode = SPEEDMODE_TURBO;
        else if (!stricmp(str, "maximum"))
            newSpeedMode = SPEEDMODE_MAXIMUM;
    }

    LuaContextInfo& info = GetCurrentInfo();
    info.speedMode = newSpeedMode;
    RefreshScriptSpeedStatus();
    return 0;
}

// tells Gens to wait while the script is doing calculations
// can call this periodically instead of gens.frameadvance
// note that the user can use hotkeys at this time
// (e.g. a savestate could possibly get loaded before gens.wait() returns)
DEFINE_LUA_FUNCTION(gens_wait, "")
{
    LuaContextInfo& info = GetCurrentInfo();

    switch (info.speedMode)
    {
    default:
    case SPEEDMODE_NORMAL:
        Step_Gens_MainLoop(true, false);
        break;
    case SPEEDMODE_NOTHROTTLE:
    case SPEEDMODE_TURBO:
    case SPEEDMODE_MAXIMUM:
        Step_Gens_MainLoop(Paused != 0, false);
        break;
    }

    return 0;
}

DEFINE_LUA_FUNCTION(gens_frameadvance, "")
{
    if (FailVerifyAtFrameBoundary(L, "gens.frameadvance", 0, 1))
        return gens_wait(L);

    int uid = luaStateToUIDMap[L];
    LuaContextInfo& info = GetCurrentInfo();

    if (!info.ranFrameAdvance)
    {
        // otherwise we'll never see the first frame of GUI drawing
        if (info.speedMode != SPEEDMODE_MAXIMUM)
            Show_Genesis_Screen();
        info.ranFrameAdvance = true;
    }

    switch (info.speedMode)
    {
    default:
    case SPEEDMODE_NORMAL:
        while (!Step_Gens_MainLoop(true, true) && !info.panic);
        break;
    case SPEEDMODE_NOTHROTTLE:
        while (!Step_Gens_MainLoop(Paused != 0, false) && !info.panic);
        if (!TurboMode)
            gens_emulateframefastnoskipping(L);
        else
            gens_emulateframefast(L);
        break;
    case SPEEDMODE_TURBO:
        while (!Step_Gens_MainLoop(Paused != 0, false) && !info.panic);
        gens_emulateframefast(L);
        break;
    case SPEEDMODE_MAXIMUM:
        while (!Step_Gens_MainLoop(Paused != 0, false) && !info.panic);
        gens_emulateframeinvisible(L);
        break;
    }
    return 0;
}

DEFINE_LUA_FUNCTION(gens_pause, "")
{
    LuaContextInfo& info = GetCurrentInfo();

    Paused = 1;
    while (!Step_Gens_MainLoop(true, false) && !info.panic);

    // allow the user to not have to manually unpause
    // after restarting a script that used gens.pause()
    if (info.panic)
        Paused = 0;

    return 0;
}

DEFINE_LUA_FUNCTION(gens_unpause, "")
{
    LuaContextInfo& info = GetCurrentInfo();

    Paused = 0;
    return 0;
}

DEFINE_LUA_FUNCTION(gens_redraw, "")
{
    Show_Genesis_Screen();
    worry(L, 250);
    return 0;
}

DEFINE_LUA_FUNCTION(memory_readbyte, "address")
{
    int address = luaL_checkinteger(L, 1);
    unsigned char value = (unsigned char)(ReadValueAtHardwareAddress(address, 1) & 0xFF);
    lua_settop(L, 0);
    lua_pushinteger(L, value);
    return 1; // we return the number of return values
}
DEFINE_LUA_FUNCTION(memory_readbytesigned, "address")
{
    int address = luaL_checkinteger(L, 1);
    signed char value = (signed char)(ReadValueAtHardwareAddress(address, 1) & 0xFF);
    lua_settop(L, 0);
    lua_pushinteger(L, value);
    return 1;
}
DEFINE_LUA_FUNCTION(memory_readword, "address")
{
    int address = luaL_checkinteger(L, 1);
    unsigned short value = (unsigned short)(ReadValueAtHardwareAddress(address, 2) & 0xFFFF);
    lua_settop(L, 0);
    lua_pushinteger(L, value);
    return 1;
}
DEFINE_LUA_FUNCTION(memory_readwordsigned, "address")
{
    int address = luaL_checkinteger(L, 1);
    signed short value = (signed short)(ReadValueAtHardwareAddress(address, 2) & 0xFFFF);
    lua_settop(L, 0);
    lua_pushinteger(L, value);
    return 1;
}
DEFINE_LUA_FUNCTION(memory_readdword, "address")
{
    int address = luaL_checkinteger(L, 1);
    unsigned long value = (unsigned long)(ReadValueAtHardwareAddress(address, 4));
    lua_settop(L, 0);
    lua_pushnumber(L, value); // can't use pushinteger in this case (out of range)
    return 1;
}
DEFINE_LUA_FUNCTION(memory_readdwordsigned, "address")
{
    int address = luaL_checkinteger(L, 1);
    signed long value = (signed long)(ReadValueAtHardwareAddress(address, 4));
    lua_settop(L, 0);
    lua_pushinteger(L, value);
    return 1;
}

DEFINE_LUA_FUNCTION(memory_writebyte, "address,value")
{
    int address = luaL_checkinteger(L, 1);
    unsigned char value = (unsigned char)(luaL_checkinteger(L, 2) & 0xFF);
    WriteValueAtHardwareRAMAddress(address, value, 1);
    return 0;
}
DEFINE_LUA_FUNCTION(memory_writeword, "address,value")
{
    int address = luaL_checkinteger(L, 1);
    unsigned short value = (unsigned short)(luaL_checkinteger(L, 2) & 0xFFFF);
    WriteValueAtHardwareRAMAddress(address, value, 2);
    return 0;
}
DEFINE_LUA_FUNCTION(memory_writedword, "address,value")
{
    int address = luaL_checkinteger(L, 1);
    unsigned long value = (unsigned long)(luaL_checkinteger(L, 2));
    WriteValueAtHardwareRAMAddress(address, value, 4);
    return 0;
}

DEFINE_LUA_FUNCTION(memory_readbyterange, "address,length")
{
    int address = luaL_checkinteger(L, 1);
    int length = luaL_checkinteger(L, 2);

    if (length < 0)
    {
        address += length;
        length = -length;
    }

    // push the array
    lua_createtable(L, abs(length), 0);

    // put all the values into the (1-based) array
    for (int a = address, n = 1; n <= length; a++, n++)
    {
        if (IsHardwareAddressValid(a))
        {
            unsigned char value = (unsigned char)(ReadValueAtHardwareAddress(a, 1) & 0xFF);
            lua_pushinteger(L, value);
            lua_rawseti(L, -2, n);
        }
        // else leave the value nil
    }

    return 1;
}
DEFINE_LUA_FUNCTION(memory_writebyterange, "address,[length,]data")
{
    int address = luaL_checkinteger(L, 1);
    int length = 0;
    int ind = 2;
    if (lua_isnumber(L, ind))
        length = luaL_checkinteger(L, ind++);

    if (length < 0)
    {
        address += length;
        length = -length;
    }
    int count = 0;
    if (length)
    {
        for (int a = address, n = 1; n <= length; a++, n++)
        {
            if (IsHardwareAddressValid(a))
            {
                lua_rawgeti(L, ind, n);
                if (!lua_isnil(L, -1)) {
                    WriteValueAtHardwareAddress(a, luaL_checkinteger(L, -1) & 0xFF, 1);
                    count++;
                }
                lua_pop(L, 1);
            }
        }
    }
    else
    {
        bool blah = false;
        int a = address;
        int i = 1;
        while (!blah && (a <= 0xFFFFFF)) {
            while (!IsHardwareAddressValid(a) && (a <= 0xFFFFFF))
                a++, i++;
            if (a > 0xFFFFFF)
                break;

            lua_rawgeti(L, ind, i++);
            blah = lua_isnil(L, -1);
            if (!blah) {
                WriteValueAtHardwareAddress(a++, luaL_checkinteger(L, -1) & 0xFF, 1);
                count++;
            }
        }
    }

    lua_pushinteger(L, count);
    return 1;
}

DEFINE_LUA_FUNCTION(memory_isvalid, "address")
{
    int address = luaL_checkinteger(L, 1);
    lua_settop(L, 0);
    lua_pushboolean(L, IsHardwareAddressValid(address));
    return 1;
}

struct registerPointerMap
{
    const char* registerName;
    unsigned int* pointer;
    int dataSize;
};

#define RPM_ENTRY(name,var) {name, (unsigned int*)&var, sizeof(var)},

registerPointerMap m68kPointerMap[] = {
    RPM_ENTRY("a0", main68k_context.areg[0])
    RPM_ENTRY("a1", main68k_context.areg[1])
    RPM_ENTRY("a2", main68k_context.areg[2])
    RPM_ENTRY("a3", main68k_context.areg[3])
    RPM_ENTRY("a4", main68k_context.areg[4])
    RPM_ENTRY("a5", main68k_context.areg[5])
    RPM_ENTRY("a6", main68k_context.areg[6])
    RPM_ENTRY("a7", main68k_context.areg[7])
    RPM_ENTRY("d0", main68k_context.dreg[0])
    RPM_ENTRY("d1", main68k_context.dreg[1])
    RPM_ENTRY("d2", main68k_context.dreg[2])
    RPM_ENTRY("d3", main68k_context.dreg[3])
    RPM_ENTRY("d4", main68k_context.dreg[4])
    RPM_ENTRY("d5", main68k_context.dreg[5])
    RPM_ENTRY("d6", main68k_context.dreg[6])
    RPM_ENTRY("d7", main68k_context.dreg[7])
    RPM_ENTRY("pc", M68kDW.last_pc)
    RPM_ENTRY("sr", main68k_context.sr)
    {}
};

struct cpuToRegisterMap
{
    const char* cpuName;
    registerPointerMap* rpmap;
}
cpuToRegisterMaps[] =
{
    { "m68k.", m68kPointerMap },
    { "main.", m68kPointerMap },
    { "", m68kPointerMap },
};

DEFINE_LUA_FUNCTION(memory_getregister, "cpu_dot_registername_string")
{
    const char* qualifiedRegisterName = luaL_checkstring(L, 1);
    lua_settop(L, 0);
    for (int cpu = 0; cpu < sizeof(cpuToRegisterMaps) / sizeof(*cpuToRegisterMaps); cpu++)
    {
        cpuToRegisterMap ctrm = cpuToRegisterMaps[cpu];
        int cpuNameLen = strlen(ctrm.cpuName);
        if (!strnicmp(qualifiedRegisterName, ctrm.cpuName, cpuNameLen))
        {
            qualifiedRegisterName += cpuNameLen;
            for (int reg = 0; ctrm.rpmap[reg].dataSize; reg++)
            {
                registerPointerMap rpm = ctrm.rpmap[reg];
                if (!stricmp(qualifiedRegisterName, rpm.registerName))
                {
                    switch (rpm.dataSize)
                    {
                    default:
                    case 1: lua_pushinteger(L, *(unsigned char*)rpm.pointer); break;
                    case 2: lua_pushinteger(L, *(unsigned short*)rpm.pointer); break;
                    case 4: lua_pushinteger(L, *(unsigned long*)rpm.pointer); break;
                    }
                    return 1;
                }
            }
            lua_pushnil(L);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}
DEFINE_LUA_FUNCTION(memory_setregister, "cpu_dot_registername_string,value")
{
    const char* qualifiedRegisterName = luaL_checkstring(L, 1);
    unsigned long value = (unsigned long)(luaL_checkinteger(L, 2));
    lua_settop(L, 0);
    for (int cpu = 0; cpu < sizeof(cpuToRegisterMaps) / sizeof(*cpuToRegisterMaps); cpu++)
    {
        cpuToRegisterMap ctrm = cpuToRegisterMaps[cpu];
        int cpuNameLen = strlen(ctrm.cpuName);
        if (!strnicmp(qualifiedRegisterName, ctrm.cpuName, cpuNameLen))
        {
            qualifiedRegisterName += cpuNameLen;
            for (int reg = 0; ctrm.rpmap[reg].dataSize; reg++)
            {
                registerPointerMap rpm = ctrm.rpmap[reg];
                if (!stricmp(qualifiedRegisterName, rpm.registerName))
                {
                    switch (rpm.dataSize)
                    {
                    default:
                    case 1: *(unsigned char*)rpm.pointer = (unsigned char)(value & 0xFF); break;
                    case 2: *(unsigned short*)rpm.pointer = (unsigned short)(value & 0xFFFF); break;
                    case 4: *(unsigned long*)rpm.pointer = value; break;
                    }
                    return 0;
                }
            }
            return 0;
        }
    }
    return 0;
}

DEFINE_LUA_FUNCTION(state_create, "[location]")
{
    if (lua_isnumber(L, 1))
    {
        // simply return the integer that got passed in
        // (that's as good a savestate object as any for a numbered savestate slot)
        lua_settop(L, 1);
        return 1;
    }

    int len = GENESIS_STATE_LENGTH;
    if (!Game)
        len += max(SEGACD_LENGTH_EX, G32X_LENGTH_EX);

    // allocate the in-memory/anonymous savestate
    unsigned char* stateBuffer = (unsigned char*)lua_newuserdata(L, len + 16); // 16 is for performance alignment reasons
    stateBuffer[0] = 0;

    return 1;
}

// savestate.save(location [, option])
// saves the current emulation state to the given location
// you can pass in either a savestate file number (an integer),
// OR you can pass in a savestate object that was returned by savestate.create()
// if option is "quiet" then any warning messages will be suppressed
// if option is "scriptdataonly" then the state will not actually be saved, but any save callbacks will still get called and their results will be saved (see savestate.registerload()/savestate.registersave())
DEFINE_LUA_FUNCTION(state_save, "location[,option]")
{
    const char* option = (lua_type(L, 2) == LUA_TSTRING) ? lua_tostring(L, 2) : NULL;
    if (option)
    {
        if (!stricmp(option, "quiet")) // I'm not sure if saving can generate warning messages, but we might as well support suppressing them should they turn out to exist
            g_disableStatestateWarnings = true;
        else if (!stricmp(option, "scriptdataonly"))
            g_onlyCallSavestateCallbacks = true;
    }
    struct Scope { ~Scope() { g_disableStatestateWarnings = false; g_onlyCallSavestateCallbacks = false; } } scope; // needs to run even if the following code throws an exception... maybe I should have put this in a "finally" block instead, but this project seems to have something against using the "try" statement

    if (!g_onlyCallSavestateCallbacks && FailVerifyAtFrameBoundary(L, "savestate.save", 2, 2))
        return 0;

    int type = lua_type(L, 1);
    switch (type)
    {
    case LUA_TNUMBER: // numbered save file
    default:
    {
        int stateNumber = luaL_checkinteger(L, 1);
        Set_Current_State(stateNumber, false, false);
        char Name[1024] = { 0 };
        Get_State_File_Name(Name);
        Save_State(Name);
    }	return 0;
    case LUA_TUSERDATA: // in-memory save slot
    {
        unsigned char* stateBuffer = (unsigned char*)lua_touserdata(L, 1);
        if (stateBuffer)
        {
            stateBuffer += ((16 - (int)stateBuffer) & 15); // for performance alignment reasons
            Save_State_To_Buffer(stateBuffer);
        }
    }	return 0;
    }
}

// savestate.load(location [, option])
// loads the current emulation state from the given location
// you can pass in either a savestate file number (an integer),
// OR you can pass in a savestate object that was returned by savestate.create() and has already saved to with savestate.save()
// if option is "quiet" then any warning messages will be suppressed
// if option is "scriptdataonly" then the state will not actually be loaded, but load callbacks will still get called and supplied with the data saved by save callbacks (see savestate.registerload()/savestate.registersave())
DEFINE_LUA_FUNCTION(state_load, "location[,option]")
{
    const char* option = (lua_type(L, 2) == LUA_TSTRING) ? lua_tostring(L, 2) : NULL;
    if (option)
    {
        if (!stricmp(option, "quiet"))
            g_disableStatestateWarnings = true;
        else if (!stricmp(option, "scriptdataonly"))
            g_onlyCallSavestateCallbacks = true;
    }
    struct Scope { ~Scope() { g_disableStatestateWarnings = false; g_onlyCallSavestateCallbacks = false; } } scope; // needs to run even if the following code throws an exception... maybe I should have put this in a "finally" block instead, but this project seems to have something against using the "try" statement

    if (!g_onlyCallSavestateCallbacks && FailVerifyAtFrameBoundary(L, "savestate.load", 2, 2))
        return 0;

    g_disableStatestateWarnings = lua_toboolean(L, 2) != 0;

    int type = lua_type(L, 1);
    switch (type)
    {
    case LUA_TNUMBER: // numbered save file
    default:
    {
        LuaContextInfo& info = GetCurrentInfo();
        if (info.rerecordCountingDisabled)
            SkipNextRerecordIncrement = true;
        int stateNumber = luaL_checkinteger(L, 1);
        Set_Current_State(stateNumber, false, !g_disableStatestateWarnings);
        char Name[1024] = { 0 };
        Get_State_File_Name(Name);
        Load_State(Name);
    }	return 0;
    case LUA_TUSERDATA: // in-memory save slot
    {
        unsigned char* stateBuffer = (unsigned char*)lua_touserdata(L, 1);
        if (stateBuffer)
        {
            stateBuffer += ((16 - (int)stateBuffer) & 15); // for performance alignment reasons
            if (stateBuffer[0])
                Load_State_From_Buffer(stateBuffer);
            else // the first byte of a valid savestate is never 0
                luaL_error(L, "attempted to load an anonymous savestate before saving it");
        }
    }	return 0;
    }
}

// savestate.loadscriptdata(location)
// returns the user data associated with the given savestate
// without actually loading the rest of that savestate or calling any callbacks.
// you can pass in either a savestate file number (an integer),
// OR you can pass in a savestate object that was returned by savestate.create()
// but note that currently only non-anonymous savestates can have associated scriptdata
//
// also note that this returns the same values
// that would be passed into a registered load function.
// the main reason this exists also is so you can register a load function that
// chooses whether or not to load the scriptdata instead of always loading it,
// and also to provide a nicer interface for loading scriptdata
// without needing to trigger savestate loading first
DEFINE_LUA_FUNCTION(state_loadscriptdata, "location")
{
    int type = lua_type(L, 1);
    switch (type)
    {
    case LUA_TNUMBER: // numbered save file
    default:
    {
        int stateNumber = luaL_checkinteger(L, 1);
        Set_Current_State(stateNumber, false, false);
        char Name[1024] = { 0 };
        Get_State_File_Name(Name);
        {
            LuaSaveData saveData;

            char luaSaveFilename[512];
            strncpy(luaSaveFilename, Name, 512);
            luaSaveFilename[512 - (1 + 7/*strlen(".luasav")*/)] = '\0';
            strcat(luaSaveFilename, ".luasav");
            FILE* luaSaveFile = fopen(luaSaveFilename, "rb");
            if (luaSaveFile)
            {
                saveData.ImportRecords(luaSaveFile);
                fclose(luaSaveFile);

                int uid = luaStateToUIDMap[L];
                LuaContextInfo& info = GetCurrentInfo();

                lua_settop(L, 0);
                saveData.LoadRecord(uid, info.dataLoadKey, (unsigned int)-1);
                return lua_gettop(L);
            }
        }
    }	return 0;
    case LUA_TUSERDATA: // in-memory save slot
    {	// there can be no user data associated with those, at least not yet
    }	return 0;
    }
}

// savestate.savescriptdata(location)
// same as savestate.save(location, "scriptdataonly")
// only provided for consistency with savestate.loadscriptdata(location)
DEFINE_LUA_FUNCTION(state_savescriptdata, "location")
{
    lua_settop(L, 1);
    lua_pushstring(L, "scriptdataonly");
    return state_save(L);
}

static const struct ButtonDesc
{
    unsigned short controllerNum;
    unsigned short bit;
    const char* name;
}
s_buttonDescs[] =
{
    { 1, 0, "up" },
    { 1, 1, "down" },
    { 1, 2, "left" },
    { 1, 3, "right" },
    { 1, 4, "A" },
    { 1, 5, "B" },
    { 1, 6, "C" },
    { 1, 7, "start" },
    { 1, 32, "X" },
    { 1, 33, "Y" },
    { 1, 34, "Z" },
    { 1, 35, "mode" },
    { 2, 24, "up" },
    { 2, 25, "down" },
    { 2, 26, "left" },
    { 2, 27, "right" },
    { 2, 28, "A" },
    { 2, 29, "B" },
    { 2, 30, "C" },
    { 2, 31, "start" },
    { 2, 36, "X" },
    { 2, 37, "Y" },
    { 2, 38, "Z" },
    { 2, 39, "mode" },
    { 0x1B, 8, "up" },
    { 0x1B, 9, "down" },
    { 0x1B, 10, "left" },
    { 0x1B, 11, "right" },
    { 0x1B, 12, "A" },
    { 0x1B, 13, "B" },
    { 0x1B, 14, "C" },
    { 0x1B, 15, "start" },
    { 0x1C, 16, "up" },
    { 0x1C, 17, "down" },
    { 0x1C, 18, "left" },
    { 0x1C, 19, "right" },
    { 0x1C, 20, "A" },
    { 0x1C, 21, "B" },
    { 0x1C, 22, "C" },
    { 0x1C, 23, "start" },
};

int joy_getArgControllerNum(lua_State* L, int& index)
{
    int controllerNumber;
    int type = lua_type(L, index);
    if (type == LUA_TSTRING || type == LUA_TNUMBER)
    {
        controllerNumber = 0;
        if (type == LUA_TSTRING)
        {
            const char* str = lua_tostring(L, index);
            if (!stricmp(str, "1C"))
                controllerNumber = 0x1C;
            else if (!stricmp(str, "1B"))
                controllerNumber = 0x1B;
            else if (!stricmp(str, "1A"))
                controllerNumber = 0x1A;
        }
        if (!controllerNumber)
            controllerNumber = luaL_checkinteger(L, index);
        index++;
    }
    else
    {
        // argument omitted; default to controller 1
        controllerNumber = 1;
    }

    if (controllerNumber == 0x1A)
        controllerNumber = 1;
    if (controllerNumber != 1 && controllerNumber != 2 && controllerNumber != 0x1B && controllerNumber != 0x1C)
        luaL_error(L, "controller number must be 1, 2, '1B', or '1C'");

    return controllerNumber;
}

// joypad.set(controllerNum = 1, inputTable)
// controllerNum can be 1, 2, '1B', or '1C'
DEFINE_LUA_FUNCTION(joy_set, "[controller=1,]inputtable")
{
    int index = 1;
    int controllerNumber = joy_getArgControllerNum(L, index);

    luaL_checktype(L, index, LUA_TTABLE);

    long long input = ~0;
    long long mask = 0;

    for (int i = 0; i < sizeof(s_buttonDescs) / sizeof(*s_buttonDescs); i++)
    {
        const ButtonDesc& bd = s_buttonDescs[i];
        if (bd.controllerNum == controllerNumber)
        {
            lua_getfield(L, index, bd.name);
            if (!lua_isnil(L, -1))
            {
                bool pressed = lua_toboolean(L, -1) != 0;
                long long bitmask = ((long long)1 << bd.bit);
                if (pressed)
                    input &= ~bitmask;
                else
                    input |= bitmask;
                mask |= bitmask;
            }
            lua_pop(L, 1);
        }
    }

    SetNextInputCondensed(input, mask);

    return 0;
}

// joypad.get(controllerNum = 1)
// controllerNum can be 1, 2, '1B', or '1C'
int joy_get_internal(lua_State* L, bool reportUp, bool reportDown)
{
    int index = 1;
    int controllerNumber = joy_getArgControllerNum(L, index);

    lua_newtable(L);

    long long input = GetCurrentInputCondensed();

    for (int i = 0; i < sizeof(s_buttonDescs) / sizeof(*s_buttonDescs); i++)
    {
        const ButtonDesc& bd = s_buttonDescs[i];
        if (bd.controllerNum == controllerNumber)
        {
            bool pressed = (input & ((long long)1 << bd.bit)) == 0;
            if ((pressed && reportDown) || (!pressed && reportUp))
            {
                lua_pushboolean(L, pressed);
                lua_setfield(L, -2, bd.name);
            }
        }
    }

    return 1;
}
// joypad.get(int controllerNumber = 1)
// returns a table of every game button,
// true meaning currently-held and false meaning not-currently-held
// this WILL read input from a currently-playing movie
DEFINE_LUA_FUNCTION(joy_get, "[controller=1]")
{
    return joy_get_internal(L, true, true);
}
// joypad.getdown(int controllerNumber = 1)
// returns a table of every game button that is currently held
DEFINE_LUA_FUNCTION(joy_getdown, "[controller=1]")
{
    return joy_get_internal(L, false, true);
}
// joypad.getup(int controllerNumber = 1)
// returns a table of every game button that is not currently held
DEFINE_LUA_FUNCTION(joy_getup, "[controller=1]")
{
    return joy_get_internal(L, true, false);
}

// joypad.peek(controllerNum = 1)
// controllerNum can be 1, 2, '1B', or '1C'
int joy_peek_internal(lua_State* L, bool reportUp, bool reportDown)
{
    int index = 1;
    int controllerNumber = joy_getArgControllerNum(L, index);

    lua_newtable(L);

    long long input = PeekInputCondensed();

    for (int i = 0; i < sizeof(s_buttonDescs) / sizeof(*s_buttonDescs); i++)
    {
        const ButtonDesc& bd = s_buttonDescs[i];
        if (bd.controllerNum == controllerNumber)
        {
            bool pressed = (input & ((long long)1 << bd.bit)) == 0;
            if ((pressed && reportDown) || (!pressed && reportUp))
            {
                lua_pushboolean(L, pressed);
                lua_setfield(L, -2, bd.name);
            }
        }
    }

    return 1;
}

// joypad.peek(int controllerNumber = 1)
// returns a table of every game button,
// true meaning currently-held and false meaning not-currently-held
// peek checks which joypad buttons are physically pressed, so it will NOT read input from a playing movie, it CAN read mid-frame input, and it will NOT pay attention to stuff like autofire or autohold or disabled L+R/U+D
DEFINE_LUA_FUNCTION(joy_peek, "[controller=1]")
{
    return joy_peek_internal(L, true, true);
}
// joypad.peekdown(int controllerNumber = 1)
// returns a table of every game button that is currently held (according to what joypad.peek() would return)
DEFINE_LUA_FUNCTION(joy_peekdown, "[controller=1]")
{
    return joy_peek_internal(L, false, true);
}
// joypad.peekup(int controllerNumber = 1)
// returns a table of every game button that is not currently held (according to what joypad.peek() would return)
DEFINE_LUA_FUNCTION(joy_peekup, "[controller=1]")
{
    return joy_peek_internal(L, true, false);
}

static const struct ColorMapping
{
    const char* name;
    unsigned int value;
}
s_colorMapping[] =
{
    { "white", 0xFFFFFFFF },
    { "black", 0x000000FF },
    { "clear", 0x00000000 },
    { "gray", 0x7F7F7FFF },
    { "grey", 0x7F7F7FFF },
    { "red", 0xFF0000FF },
    { "orange", 0xFF7F00FF },
    { "yellow", 0xFFFF00FF },
    { "chartreuse", 0x7FFF00FF },
    { "green", 0x00FF00FF },
    { "teal", 0x00FF7FFF },
    { "cyan", 0x00FFFFFF },
    { "blue", 0x0000FFFF },
    { "purple", 0x7F00FFFF },
    { "magenta", 0xFF00FFFF },
};

inline int getcolor_unmodified(lua_State *L, int idx, int defaultColor)
{
    int type = lua_type(L, idx);
    switch (type)
    {
    case LUA_TNUMBER:
    {
        return lua_tointeger(L, idx);
    }	break;
    case LUA_TSTRING:
    {
        const char* str = lua_tostring(L, idx);
        if (*str == '#')
        {
            int color;
            sscanf(str + 1, "%X", &color);
            int len = strlen(str + 1);
            int missing = max(0, 8 - len);
            color <<= missing << 2;
            if (missing >= 2) color |= 0xFF;
            return color;
        }
        else for (int i = 0; i < sizeof(s_colorMapping) / sizeof(*s_colorMapping); i++)
        {
            if (!stricmp(str, s_colorMapping[i].name))
                return s_colorMapping[i].value;
        }
        if (!strnicmp(str, "rand", 4))
            return ((rand() * 255 / RAND_MAX) << 8) | ((rand() * 255 / RAND_MAX) << 16) | ((rand() * 255 / RAND_MAX) << 24) | 0xFF;
    }	break;
    case LUA_TTABLE:
    {
        int color = 0xFF;
        lua_pushnil(L); // first key
        int keyIndex = lua_gettop(L);
        int valueIndex = keyIndex + 1;
        bool first = true;
        while (lua_next(L, idx))
        {
            bool keyIsString = (lua_type(L, keyIndex) == LUA_TSTRING);
            bool keyIsNumber = (lua_type(L, keyIndex) == LUA_TNUMBER);
            int key = keyIsString ? tolower(*lua_tostring(L, keyIndex)) : (keyIsNumber ? lua_tointeger(L, keyIndex) : 0);
            int value = lua_tointeger(L, valueIndex);
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            switch (key)
            {
            case 1: case 'r': color |= value << 24; break;
            case 2: case 'g': color |= value << 16; break;
            case 3: case 'b': color |= value << 8; break;
            case 4: case 'a': color = (color & ~0xFF) | value; break;
            }
            lua_pop(L, 1);
        }
        return color;
    }	break;
    case LUA_TFUNCTION:
        return 0;
    }
    return defaultColor;
}
int getcolor(lua_State *L, int idx, int defaultColor)
{
    int color = getcolor_unmodified(L, idx, defaultColor);
    LuaContextInfo& info = GetCurrentInfo();
    if (info.transparencyModifier != 255)
    {
        int alpha = (((color & 0xFF) * info.transparencyModifier) / 255);
        if (alpha > 255) alpha = 255;
        color = (color & ~0xFF) | alpha;
    }
    return color;
}
DEFINE_LUA_FUNCTION(tile_create, "")
{
    unsigned short *tile = (unsigned short *)lua_newuserdata(L, 32);
    memset(tile, 0, 32);

    return 1;
}
DEFINE_LUA_FUNCTION(tile_getpixel, "tile,x,y")
{
    int type = lua_type(L, 1);
    if ((type != LUA_TUSERDATA) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned char x = luaL_checkinteger(L, 2) - 1;
    unsigned char y = luaL_checkinteger(L, 3) - 1;
    if ((x < 0) || (y < 0) || (x > 7) || (y > 7))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned char *tile = (unsigned char *)lua_touserdata(L, 1);
    unsigned char color = tile[(y << 4) + (x >> 1)];
    if (!(x & 1))
        color >>= 4;
    color &= 0xF;
    lua_pushinteger(L, color);
    return 1;
}
DEFINE_LUA_FUNCTION(tile_setpixel, "tile,x,y,color")
{
    int type = lua_type(L, 1);
    if ((type != LUA_TUSERDATA) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || (!lua_isnil(L, 4) && !lua_isnumber(L, 4)))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned char x = luaL_checkinteger(L, 2) - 1;
    unsigned char y = luaL_checkinteger(L, 3) - 1;
    unsigned char color;
    if (lua_isnil(L, 4))
        color = 0;
    else
        color = luaL_checkinteger(L, 4);
    if ((x < 0) || (y < 0) || (x > 7) || (y > 7) || (color & ~0xF))
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    unsigned char *tile = (unsigned char *)lua_touserdata(L, 1);
    if (x & 1)
    {
        tile[(y << 2) + (x >> 1)] &= 0xF0;
        tile[(y << 2) + (x >> 1)] |= color;
    }
    else
    {
        tile[(y << 2) + (x >> 1)] &= 0x0F;
        tile[(y << 2) + (x >> 1)] |= (color << 4);
    }
    lua_pushinteger(L, 0);
    return 1;
}
DEFINE_LUA_FUNCTION(tile_getraw, "tile")
{
    int type = lua_type(L, 1);
    if (type != LUA_TUSERDATA)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned char *tile = (unsigned char *)lua_touserdata(L, 1);
    lua_newtable(L);
    int i = 0;
    while (i < 32) {
        lua_pushinteger(L, tile[i]);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
DEFINE_LUA_FUNCTION(pal_create, "")
{
    unsigned short *pal = (unsigned short *)lua_newuserdata(L, 32);
    memset(pal, ~0, 32);

    return 1;
}
DEFINE_LUA_FUNCTION(pal_getcolor, "palette,index")
{
    int type = lua_type(L, 1);
    if ((type != LUA_TUSERDATA) || !lua_isnumber(L, 2))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned short *pal = (unsigned short *)lua_touserdata(L, 1);
    unsigned short color = pal[luaL_checkinteger(L, 2) - 1];
    if (color == 0xFFFF)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, (color & 0xE) << 4);
    lua_setfield(L, -2, "R");
    lua_pushinteger(L, color & 0xE0);
    lua_setfield(L, -2, "G");
    lua_pushinteger(L, (color & 0xE00) >> 4);
    lua_setfield(L, -2, "B");
    lua_pushinteger(L, 255);
    lua_setfield(L, -2, "A");
    return 1;
}
DEFINE_LUA_FUNCTION(pal_setcolor, "palette,index,color")
{
    int type = lua_type(L, 1);
    if ((type != LUA_TUSERDATA) || !lua_isnumber(L, 2))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned short *pal = (unsigned short *)lua_touserdata(L, 1);
    unsigned short color;
    if (lua_isnil(L, 3))
        color = 0xFFFF;
    else
    {
        int color32 = getcolor_unmodified(L, 3, 0);
        if (color32 & 0xFF)
            color = ((color32 & 0xE000) >> 4) | ((color32 & 0xE00000) >> 16) | ((color32 & 0xE0000000) >> 28);
        else
            color = 0xFFFF;
    }
    pal[luaL_checkinteger(L, 2) - 1] = color;
    lua_pushinteger(L, 0);
    return 1;
}
DEFINE_LUA_FUNCTION(pal_getraw, "palette")
{
    int type = lua_type(L, 1);
    if (type != LUA_TUSERDATA)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    unsigned char *pal = (unsigned char *)lua_touserdata(L, 1);
    unsigned short *blah = (unsigned short *)pal;
    Byte_Swap(pal, 32);
    lua_newtable(L);
    int i = 0;
    while (i < 32) {
        if (pal[i] != 0xFF)
            lua_pushinteger(L, pal[i]);
        else
            lua_pushnil(L);
        lua_rawseti(L, -2, ++i);
    }
    Byte_Swap(pal, 32);
    return 1;
}
// r,g,b,a = gui.parsecolor(color)
// examples:
// local r,g,b = gui.parsecolor("green")
// local r,g,b,a = gui.parsecolor(0x7F3FFF7F)
DEFINE_LUA_FUNCTION(gui_parsecolor, "color")
{
    int color = getcolor_unmodified(L, 1, 0);
    int r = (color & 0xFF000000) >> 24;
    int g = (color & 0x00FF0000) >> 16;
    int b = (color & 0x0000FF00) >> 8;
    int a = (color & 0x000000FF);
    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);
    lua_pushinteger(L, a);
    return 4;
}

DEFINE_LUA_FUNCTION(gui_text, "x,y,str[,color=\"white\"[,outline=\"black\"]]")
{
    int x = luaL_checkinteger(L, 1) & 0xFFFF; // have to check for errors before deferring
    int y = luaL_checkinteger(L, 2) & 0xFFFF;

    if (DeferGUIFuncIfNeeded(L))
        return 0; // we have to wait until later to call this function because gens hasn't emulated the next frame yet
    // (the only way to avoid this deferring is to be in a gui.register or gens.registerafter callback)

    const char* str = toCString(L, 3); // better than using luaL_checkstring here (more permissive)

    if (str && *str)
    {
        int foreColor = getcolor(L, 4, 0xFFFFFFFF);
        int backColor = getcolor(L, 5, 0x000000FF);
        PutText2(str, x, y, foreColor, backColor);
    }

    return 0;
}

static inline void ApplyShaderToPixel(int off, std::map<int, int>& cachedShaderResults, lua_State* L, int idx)
{
    int color;
    if (Bits32)
        color = MD_Screen32[off];
    else
        color = DrawUtil::Pix16To32(MD_Screen[off]);

    int result;
    std::map<int, int>::const_iterator found = cachedShaderResults.find(color);
    if (found != cachedShaderResults.end())
    {
        result = found->second;
    }
    else
    {
        int b = (color & 0x000000FF);
        int g = (color & 0x0000FF00) >> 8;
        int r = (color & 0x00FF0000) >> 16;

        lua_pushvalue(L, idx);
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);

        lua_call(L, 3, 3);

        int rout = lua_tointeger(L, -3);
        int gout = lua_tointeger(L, -2);
        int bout = lua_tointeger(L, -1);
        lua_pop(L, 3);
        if (rout < 0) rout = 0; if (rout > 255) rout = 255;
        if (gout < 0) gout = 0; if (gout > 255) gout = 255;
        if (bout < 0) bout = 0; if (bout > 255) bout = 255;

        result = DrawUtil::Make32(rout, gout, bout);
        cachedShaderResults[color] = result;
    }
    if (Bits32)
        MD_Screen32[off] = result;
    else
        MD_Screen[off] = DrawUtil::Pix32To16(result);
}

#define SWAP_INTEGERS(x,y) x^=y, y^=x, x^=y

// performance note: for me, this function is extremely slow in debug builds,
// but when compiled with full optimizations turned on it becomes very fast.
void ApplyShaderToBox(int x1, int y1, int x2, int y2, lua_State* L, int idx)
{
    if ((x1 < 0 && x2 < 0) || (x1 > 319 && x2 > 319) || (y1 < 0 && y2 < 0) || (y1 > 223 && y2 > 223))
        return;

    // require x1,y1 <= x2,y2
    if (x1 > x2) SWAP_INTEGERS(x1, x2);
    if (y1 > y2) SWAP_INTEGERS(y1, y2);

    // avoid trying to draw any offscreen pixels
    if (x1 < 0)  x1 = 0;
    if (x1 > 319) x1 = 319;
    if (x2 < 0)  x2 = 0;
    if (x2 > 319) x2 = 319;
    if (y1 < 0)  y1 = 0;
    if (y1 > 223) y1 = 223;
    if (y2 < 0)  y2 = 0;
    if (y2 > 223) y2 = 223;

    std::map<int, int> cachedShaderResults;

    for (short y = y1; y <= y2; y++)
    {
        int off = (y * 336) + x1 + 8;
        for (short x = x1; x <= x2; x++, off++)
        {
            ApplyShaderToPixel(off, cachedShaderResults, L, idx);
        }
    }
}

void ApplyShaderToBoxOutline(int x1, int y1, int x2, int y2, lua_State* L, int idx)
{
    // require x1,y1 <= x2,y2
    if (x1 > x2) SWAP_INTEGERS(x1, x2);
    if (y1 > y2) SWAP_INTEGERS(y1, y2);

    // avoid trying to draw any offscreen pixels
    if (x1 < -1)  x1 = -1;
    if (x1 > 320) x1 = 320;
    if (x2 < -1)  x2 = -1;
    if (x2 > 320) x2 = 320;
    if (y1 < -1)  y1 = -1;
    if (y1 > 224) y1 = 224;
    if (y2 < -1)  y2 = -1;
    if (y2 > 224) y2 = 224;

    std::map<int, int> cachedShaderResults;

    if (y1 >= 0 && y1 < 224)
        for (short x = x1 + 1; x < x2; x++)
            ApplyShaderToPixel((y1 * 336) + x + 8, cachedShaderResults, L, idx);
    if (x1 >= 0 && x1 < 320)
        for (short y = y1; y <= y2; y++)
            ApplyShaderToPixel((y * 336) + x1 + 8, cachedShaderResults, L, idx);
    if (y1 != y2 && y2 >= 0 && y2 < 224)
        for (short x = x1 + 1; x < x2; x++)
            ApplyShaderToPixel((y2 * 336) + x + 8, cachedShaderResults, L, idx);
    if (x1 != x2 && x2 >= 0 && x2 < 320)
        for (short y = y1; y <= y2; y++)
            ApplyShaderToPixel((y * 336) + x2 + 8, cachedShaderResults, L, idx);
}

int amplifyShader(lua_State* L)
{
    int rin = lua_tointeger(L, 1);
    int gin = lua_tointeger(L, 2);
    int bin = lua_tointeger(L, 3);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 3, 3);
    int rout = lua_tointeger(L, 1);
    int gout = lua_tointeger(L, 2);
    int bout = lua_tointeger(L, 3);
    lua_settop(L, 0);
    lua_pushinteger(L, rout * 4 - rin * 3);
    lua_pushinteger(L, gout * 4 - gin * 3);
    lua_pushinteger(L, bout * 4 - bin * 3);
    return 3;
}

DEFINE_LUA_FUNCTION(gui_box, "x1,y1,x2,y2[,fill[,outline]]")
{
    // have to check for errors before deferring
    int x1 = luaL_checkinteger(L, 1); // & 0xFFFF removed because it was turning -1 into 65535 which screwed up the out-of-bounds checking in ApplyShaderToBox
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);

    if (DeferGUIFuncIfNeeded(L))
        return 0;

    int fillcolor = getcolor(L, 5, 0xFFFFFF3F);
    int outlinecolor = getcolor(L, 6, fillcolor | 0xFF);
    if (!lua_isfunction(L, 5) || !lua_isnoneornil(L, 6))
    {
        DrawBoxPP2(x1, y1, x2, y2, fillcolor, outlinecolor);
        if (lua_isfunction(L, 5))
            ApplyShaderToBox(x1 + 1, y1 + 1, x2 - 1, y2 - 1, L, 5);
        if (lua_isfunction(L, 6))
            ApplyShaderToBoxOutline(x1, y1, x2, y2, L, 6);
    }
    else // fill is a shader and outline is not specified, so make the outline a more "opaque" version of the shader to match up with the default color behavior
    {
        ApplyShaderToBox(x1 + 1, y1 + 1, x2 - 1, y2 - 1, L, 5);
        lua_settop(L, 5);
        lua_pushvalue(L, 5);
        lua_pushcclosure(L, amplifyShader, 1);
        ApplyShaderToBoxOutline(x1, y1, x2, y2, L, 6);
    }

    return 0;
}
// gui.setpixel(x,y,color)
// color can be a RGB web color like '#ff7030', or with alpha RGBA like '#ff703060'
//   or it can be an RGBA hex number like 0xFF703060
//   or it can be a preset color like 'red', 'orange', 'blue', 'white', etc.
DEFINE_LUA_FUNCTION(gui_pixel, "x,y[,color=\"white\"]")
{
    int x = luaL_checkinteger(L, 1) & 0xFFFF; // have to check for errors before deferring
    int y = luaL_checkinteger(L, 2) & 0xFFFF;

    if (DeferGUIFuncIfNeeded(L))
        return 0;

    int color = getcolor(L, 3, 0xFFFFFFFF);
    int color32 = color >> 8;
    int color16 = DrawUtil::Pix32To16(color32);
    int Opac = color & 0xFF;

    if (Opac)
        Pixel(x, y, color32, color16, 0, Opac);

    return 0;
}
// r,g,b = gui.getpixel(x,y)
DEFINE_LUA_FUNCTION(gui_getpixel, "x,y")
{
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);

    int xres = FULL_X_RESOLUTION;
    int yres = FULL_Y_RESOLUTION;

    x = max(0, min(xres, x));
    y = max(0, min(yres, y));

    int off = (y * 336) + x + 8;

    int color;
    if (Bits32)
        color = MD_Screen32[off];
    else
        color = DrawUtil::Pix16To32(MD_Screen[off]);

    int b = (color & 0x000000FF);
    int g = (color & 0x0000FF00) >> 8;
    int r = (color & 0x00FF0000) >> 16;

    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);

    return 3;
}
DEFINE_LUA_FUNCTION(gui_line, "x1,y1,x2,y2[,color=\"white\"[,skipfirst=false]]")
{
    int x1 = luaL_checkinteger(L, 1) & 0xFFFF; // have to check for errors before deferring
    int y1 = luaL_checkinteger(L, 2) & 0xFFFF;
    int x2 = luaL_checkinteger(L, 3) & 0xFFFF;
    int y2 = luaL_checkinteger(L, 4) & 0xFFFF;

    if (DeferGUIFuncIfNeeded(L))
        return 0;

    int color = getcolor(L, 5, 0xFFFFFFFF);
    int color32 = color >> 8;
    int color16 = DrawUtil::Pix32To16(color32);
    int Opac = color & 0xFF;

    if (Opac)
    {
        int skipFirst = lua_toboolean(L, 6);
        DrawLine(x1, y1, x2, y2, color32, color16, 0, Opac, skipFirst);
    }

    return 0;
}

// gui.opacity(number alphaValue)
// sets the transparency of subsequent draw calls
// 0.0 is completely transparent, 1.0 is completely opaque
// non-integer values are supported and meaningful, as are values greater than 1.0
// it is not necessary to use this function to get transparency (or the less-recommended gui.transparency() either),
// because you can provide an alpha value in the color argument of each draw call.
// however, it can be convenient to be able to globally modify the drawing transparency
DEFINE_LUA_FUNCTION(gui_setopacity, "alpha_0_to_1")
{
    lua_Number opacF = luaL_checknumber(L, 1);
    opacF *= 255.0;
    if (opacF < 0) opacF = 0;
    int opac;
    lua_number2int(opac, opacF);
    LuaContextInfo& info = GetCurrentInfo();
    info.transparencyModifier = opac;
    return 0;
}

// gui.transparency(number transparencyValue)
// sets the transparency of subsequent draw calls
// 0.0 is completely opaque, 4.0 is completely transparent
// non-integer values are supported and meaningful, as are values less than 0.0
// this is a legacy function, and the range is from 0 to 4 solely for this reason
// it does the exact same thing as gui.opacity() but with a different argument range
DEFINE_LUA_FUNCTION(gui_settransparency, "transparency_4_to_0")
{
    lua_Number transp = luaL_checknumber(L, 1);
    lua_Number opacF = 4 - transp;
    opacF *= 255.0 / 4.0;
    if (opacF < 0) opacF = 0;
    int opac;
    lua_number2int(opac, opacF);
    LuaContextInfo& info = GetCurrentInfo();
    info.transparencyModifier = opac;
    return 0;
}

// takes a screenshot and returns it in gdstr format
// example: gd.createFromGdStr(gui.gdscreenshot()):png("outputimage.png")
DEFINE_LUA_FUNCTION(gui_gdscreenshot, "")
{
    int width = FULL_X_RESOLUTION;
    int height = FULL_Y_RESOLUTION;
    int size = 11 + width * height * 4;

    char* str = new char[size + 1];
    str[size] = 0;
    unsigned char* ptr = (unsigned char*)str;

    // GD format header for truecolor image (11 bytes)
    *ptr++ = (65534 >> 8) & 0xFF;
    *ptr++ = (65534) & 0xFF;
    *ptr++ = (width >> 8) & 0xFF;
    *ptr++ = (width) & 0xFF;
    *ptr++ = (height >> 8) & 0xFF;
    *ptr++ = (height) & 0xFF;
    *ptr++ = 1;
    *ptr++ = 255;
    *ptr++ = 255;
    *ptr++ = 255;
    *ptr++ = 255;

    unsigned char *Src = Bits32 ? (unsigned char*)(MD_Screen32 + 8) : (unsigned char*)(MD_Screen + 8);

    if (Bits32)
    {
        for (int y = 0; y < height; y++, Src += 336 * 4)
        {
            for (int x = 0; x < width; x++)
            {
                *ptr++ = Src[4 * x + 3];
                *ptr++ = Src[4 * x + 2];
                *ptr++ = Src[4 * x + 1];
                *ptr++ = Src[4 * x + 0];
            }
        }
    }
    else if ((Mode_555 & 1) == 0)
    {
        for (int y = 0; y < height; y++, Src += 336 * 2)
        {
            for (int x = 0; x < width; x++)
            {
                int pix = DrawUtil::Pix16To32((pix16)(Src[2 * x] + (Src[2 * x + 1] << 8)));
                *ptr++ = ((unsigned char*)&pix)[3];
                *ptr++ = ((unsigned char*)&pix)[2];
                *ptr++ = ((unsigned char*)&pix)[1];
                *ptr++ = ((unsigned char*)&pix)[0];
            }
        }
    }
    else
    {
        for (int y = 0; y < height; y++, Src += 336 * 2)
        {
            for (int x = 0; x < width; x++)
            {
                int pix = DrawUtil::Pix15To32((pix15)(Src[2 * x] + (Src[2 * x + 1] << 8)));
                *ptr++ = ((unsigned char*)&pix)[3];
                *ptr++ = ((unsigned char*)&pix)[2];
                *ptr++ = ((unsigned char*)&pix)[1];
                *ptr++ = ((unsigned char*)&pix)[0];
            }
        }
    }

    lua_pushlstring(L, str, size);
    delete[] str;
    return 1;
}

// draws a gd image that's in gdstr format to the screen
// example: gui.gdoverlay(gd.createFromPng("myimage.png"):gdStr())
DEFINE_LUA_FUNCTION(gui_gdoverlay, "[x=0,y=0,]gdimage[,alphamul]")
{
    int xStart = 0;
    int yStart = 0;

    int index = 1;
    if (lua_type(L, index) == LUA_TNUMBER)
    {
        xStart = lua_tointeger(L, index++);
        if (lua_type(L, index) == LUA_TNUMBER)
            yStart = lua_tointeger(L, index++);
    }

    luaL_checktype(L, index, LUA_TSTRING); // have to check for errors before deferring

    if (DeferGUIFuncIfNeeded(L))
        return 0;

    const unsigned char* ptr = (const unsigned char*)lua_tostring(L, index++);

    // GD format header for truecolor image (11 bytes)
    ptr++;
    ptr++;
    int width = *ptr++ << 8;
    width |= *ptr++;
    int height = *ptr++ << 8;
    height |= *ptr++;
    ptr += 5;

    int maxWidth = FULL_X_RESOLUTION;
    int maxHeight = FULL_Y_RESOLUTION;

    unsigned char *Dst = Bits32 ? (unsigned char*)(MD_Screen32 + 8) : (unsigned char*)(MD_Screen + 8);

    LuaContextInfo& info = GetCurrentInfo();
    int alphaMul = info.transparencyModifier;
    if (lua_isnumber(L, index))
        alphaMul = (int)(alphaMul * lua_tonumber(L, index++));
    if (alphaMul <= 0)
        return 0;

    // since there aren't that many possible opacity levels,
    // do the opacity modification calculations beforehand instead of per pixel
    int opacMap[256];
    for (int i = 0; i < 256; i++)
    {
        int opac = 255 - (i << 1); // not sure why, but gdstr seems to divide each alpha value by 2
        opac = (opac * alphaMul) / 255;
        if (opac < 0) opac = 0;
        if (opac > 255) opac = 255;
        opacMap[i] = 255 - opac;
    }

    if (Bits32)
    {
        Dst += yStart * 336 * 4;
        for (int y = yStart; y < height + yStart && y < maxHeight; y++, Dst += 336 * 4)
        {
            if (y < 0)
                ptr += width * 4;
            else
            {
                int xA = (xStart < 0 ? 0 : xStart);
                int xB = (xStart + width > maxWidth ? maxWidth : xStart + width);
                ptr += (xA - xStart) * 4;
                for (int x = xA; x < xB; x++)
                {
                    //Dst[4*x+3] = *ptr++;
                    //Dst[4*x+2] = *ptr++;
                    //Dst[4*x+1] = *ptr++;
                    //Dst[4*x+0] = *ptr++;

                    int opac = opacMap[ptr[0]];
                    pix32 pix = (ptr[3] | (ptr[2] << 8) | (ptr[1] << 16));
                    pix32 prev = Dst[4 * x] | (Dst[4 * x + 1] << 8) | (Dst[4 * x + 2] << 16);
                    pix = DrawUtil::Blend(prev, pix, opac);
                    Dst[4 * x] = pix & 0xFF;
                    Dst[4 * x + 1] = (pix >> 8) & 0xFF;
                    Dst[4 * x + 2] = (pix >> 16) & 0xFF;
                    ptr += 4;
                }
                ptr += (xStart + width - xB) * 4;
            }
        }
    }
    else if ((Mode_555 & 1) == 0)
    {
        Dst += yStart * 336 * 2;
        for (int y = yStart; y < height + yStart && y < maxHeight; y++, Dst += 336 * 2)
        {
            if (y < 0)
                ptr += width * 4;
            else
            {
                int xA = (xStart < 0 ? 0 : xStart);
                int xB = (xStart + width > maxWidth ? maxWidth : xStart + width);
                ptr += (xA - xStart) * 4;
                for (int x = xA; x < xB; x++)
                {
                    int opac = opacMap[ptr[0]];
                    pix32 pixh = (ptr[3] | (ptr[2] << 8) | (ptr[1] << 16));
                    pix32 prev = DrawUtil::Pix16To32(Dst[2 * x] | (Dst[2 * x + 1] << 8));
                    pix16 pix = DrawUtil::Pix32To16(DrawUtil::Blend(prev, pixh, opac));
                    Dst[2 * x] = pix & 0xFF;
                    Dst[2 * x + 1] = (pix >> 8) & 0xFF;
                    ptr += 4;
                }
                ptr += (xStart + width - xB) * 4;
            }
        }
    }
    else
    {
        Dst += yStart * 336 * 2;
        for (int y = yStart; y < height + yStart && y < maxHeight; y++, Dst += 336 * 2)
        {
            if (y < 0)
                ptr += width * 4;
            else
            {
                int xA = (xStart < 0 ? 0 : xStart);
                int xB = (xStart + width > maxWidth ? maxWidth : xStart + width);
                ptr += (xA - xStart) * 4;
                for (int x = xA; x < xB; x++)
                {
                    int opac = opacMap[ptr[0]];
                    pix32 pixh = (ptr[3] | (ptr[2] << 8) | (ptr[1] << 16));
                    pix32 prev = DrawUtil::Pix15To32(Dst[2 * x] | (Dst[2 * x + 1] << 8));
                    pix15 pix = DrawUtil::Pix32To15(DrawUtil::Blend(prev, pixh, opac));
                    Dst[2 * x] = pix & 0xFF;
                    Dst[2 * x + 1] = (pix >> 8) & 0xFF;
                    ptr += 4;
                }
                ptr += (xStart + width - xB) * 4;
            }
        }
    }

    return 0;
}

static void GetCurrentScriptDir(char* buffer, int bufLen)
{
    LuaContextInfo& info = GetCurrentInfo();
    strncpy(buffer, info.lastFilename.c_str(), bufLen);
    buffer[bufLen - 1] = 0;
    char* slash = max(strrchr(buffer, '/'), strrchr(buffer, '\\'));
    if (slash)
        slash[1] = 0;
}

DEFINE_LUA_FUNCTION(gens_openscript, "filename")
{
    char curScriptDir[1024]; GetCurrentScriptDir(curScriptDir, 1024); // make sure we can always find scripts that are in the same directory as the current script
    const char* filename = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    const char* errorMsg = GensOpenScript(filename, curScriptDir, true);
    if (errorMsg)
        luaL_error(L, errorMsg);
    return 0;
}

DEFINE_LUA_FUNCTION(gens_loadrom, "filename")
{
    struct Temp { Temp() { EnableStopAllLuaScripts(false); } ~Temp() { EnableStopAllLuaScripts(true); } } dontStopScriptsHere;
    const char* filename = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    char curScriptDir[1024]; GetCurrentScriptDir(curScriptDir, 1024);
    filename = MakeRomPathAbsolute(filename, curScriptDir);
    int result = GensLoadRom(filename);
    if (result <= 0)
        luaL_error(L, "Failed to load ROM \"%s\": %s", filename, result ? "invalid or unsupported" : "cancelled or not found");
    CallRegisteredLuaFunctions(LUACALL_ONSTART);
    return 0;
}

DEFINE_LUA_FUNCTION(gens_hardreset, "")
{
    if (!(Game))
        return 0;

    if (Genesis_Started)
        Reset_Genesis();

    FrameCount = 0;
    LagCount = 0;
    LagCountPersistent = 0;
    return 0;
}

DEFINE_LUA_FUNCTION(gens_getframecount, "")
{
    lua_pushinteger(L, FrameCount);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_getlagcount, "")
{
    lua_pushinteger(L, LagCountPersistent);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_lagged, "")
{
    lua_pushboolean(L, Lag_Frame);
    return 1;
}
DEFINE_LUA_FUNCTION(gens_emulating, "")
{
    lua_pushboolean(L, (Genesis_Started));
    return 1;
}
DEFINE_LUA_FUNCTION(gens_atframeboundary, "")
{
    lua_pushboolean(L, !Inside_Frame);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_getlength, "")
{
    lua_pushinteger(L, MainMovie.File ? MainMovie.LastFrame : 0);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_isactive, "")
{
    lua_pushboolean(L, MainMovie.File != NULL);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_rerecordcount, "")
{
    lua_pushinteger(L, MainMovie.File ? MainMovie.NbRerecords : 0);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_setrerecordcount, "")
{
    MainMovie.NbRerecords = luaL_checkinteger(L, 1);
    return 0;
}
DEFINE_LUA_FUNCTION(gens_rerecordcounting, "[enabled]")
{
    LuaContextInfo& info = GetCurrentInfo();
    if (lua_gettop(L) == 0)
    {
        // if no arguments given, return the current value
        lua_pushboolean(L, !info.rerecordCountingDisabled);
        return 1;
    }
    else
    {
        // set rerecord disabling
        info.rerecordCountingDisabled = !lua_toboolean(L, 1);
        return 0;
    }
}
DEFINE_LUA_FUNCTION(movie_getreadonly, "")
{
    lua_pushboolean(L, MainMovie.File ? MainMovie.ReadOnly : 0);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_setreadonly, "readonly")
{
    int readonly = lua_toboolean(L, 1) ? 1 : 0;
    if (MainMovie.ReadOnly != 2)
        MainMovie.ReadOnly = readonly;
    else if (!readonly)
        luaL_error(L, "movie.setreadonly failed: write permission denied");

    return 0;
}
DEFINE_LUA_FUNCTION(movie_isrecording, "")
{
    lua_pushboolean(L, MainMovie.Status == MOVIE_RECORDING);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_isplaying, "")
{
    lua_pushboolean(L, MainMovie.Status == MOVIE_PLAYING);
    return 1;
}
DEFINE_LUA_FUNCTION(movie_getmode, "")
{
    switch (MainMovie.Status)
    {
    case MOVIE_PLAYING:
        lua_pushstring(L, "playback");
        break;
    case MOVIE_RECORDING:
        lua_pushstring(L, "record");
        break;
    case MOVIE_FINISHED:
        lua_pushstring(L, "finished");
        break;
    default:
        lua_pushnil(L);
        break;
    }
    return 1;
}
DEFINE_LUA_FUNCTION(movie_getname, "")
{
    lua_pushstring(L, MainMovie.FileName);
    return 1;
}
// movie.play() -- plays a movie of the user's choice
// movie.play(filename) -- starts playing a particular movie
// throws an error (with a description) if for whatever reason the movie couldn't be played
DEFINE_LUA_FUNCTION(movie_play, "[filename]")
{
    const char* filename = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    const char* errorMsg = GensPlayMovie(filename, true);
    if (errorMsg)
        luaL_error(L, errorMsg);
    return 0;
}
DEFINE_LUA_FUNCTION(movie_replay, "")
{
    if (MainMovie.File)
        GensReplayMovie();
    else
        luaL_error(L, "it is invalid to call movie.replay when no movie open.");
    return 0;
}
DEFINE_LUA_FUNCTION(movie_close, "")
{
    CloseMovieFile(&MainMovie);
    return 0;
}

extern "C"
{
    extern unsigned int AR_TAB[128];
    extern unsigned int DR_TAB[96];
    extern unsigned int DT_TAB[8][32];
    extern unsigned int SL_TAB[16];
    extern unsigned int NULL_RATE[32];
    extern const unsigned int LFO_AMS_TAB[4];
    extern const unsigned int LFO_FMS_TAB[8];
}
DEFINE_LUA_FUNCTION(sound_get, "")
{
    lua_newtable(L);
    lua_newtable(L);
    lua_newtable(L);
    for (int channel = 0; channel < 6; channel++)
    {
        lua_newtable(L);
        lua_pushinteger(L, YM2612.CHANNEL[channel].ALGO);
        lua_setfield(L, -2, "AL");
        lua_pushinteger(L, YM2612.CHANNEL[channel].FB);
        lua_setfield(L, -2, "FB");

        lua_newtable(L);
        for (int slot = 0; slot < 4; slot++)
        {
            lua_newtable(L);

            // un-pointerify
            // TODO: prevent double un-pointerify
            int AR = (int)((int*)YM2612.CHANNEL[channel].SLOT[slot].AR - (int*)&AR_TAB);
            if ((int*)(YM2612.CHANNEL[channel].SLOT[slot].AR) != (int*)(&NULL_RATE[0]))
            {
                assert(AR % 2 == 0);
                AR >>= 1;
            }
            else
            {
                AR = 0;
            }
            int DR = (int)((int*)YM2612.CHANNEL[channel].SLOT[slot].DR - (int*)&DR_TAB);
            if ((int*)(YM2612.CHANNEL[channel].SLOT[slot].DR) != (int*)(&NULL_RATE[0]))
            {
                assert(DR % 2 == 0);
                DR >>= 1;
            }
            else
            {
                DR = 0;
            }
            int SR = (int)((int*)YM2612.CHANNEL[channel].SLOT[slot].SR - (int*)&DR_TAB);
            if ((int*)(YM2612.CHANNEL[channel].SLOT[slot].SR) != (int*)(&NULL_RATE[0]))
            {
                assert(SR % 2 == 0);
                SR >>= 1;
            }
            else
            {
                SR = 0;
            }
            int RR = (int)((int*)YM2612.CHANNEL[channel].SLOT[slot].RR - (int*)&DR_TAB);
            assert((RR - 2) % 4 == 0);
            RR = (RR - 2) >> 2;
            int DT = 0;
            for (int i = 0; i < 8; i++)
            {
                if ((int*)(YM2612.CHANNEL[channel].SLOT[slot].DT) == (int*)DT_TAB[i])
                {
                    DT = i;
                    break;
                }
            }
            int SL = 0;
            for (int i = 0; i < 16; i++)
            {
                if (YM2612.CHANNEL[channel].SLOT[slot].SLL == SL_TAB[i])
                {
                    SL = i;
                    break;
                }
            }
            int TL = YM2612.CHANNEL[channel].SLOT[slot].TL;
            int KS = 3 - YM2612.CHANNEL[channel].SLOT[slot].KSR_S;
            int ML = (YM2612.CHANNEL[channel].SLOT[slot].MUL > 1) ?
                (YM2612.CHANNEL[channel].SLOT[slot].MUL >> 1) : 0;

            lua_pushinteger(L, AR);
            lua_setfield(L, -2, "AR");
            lua_pushinteger(L, DR);
            lua_setfield(L, -2, "DR");
            lua_pushinteger(L, SR);
            lua_setfield(L, -2, "SR");
            lua_pushinteger(L, RR);
            lua_setfield(L, -2, "RR");
            lua_pushinteger(L, SL);
            lua_setfield(L, -2, "SL");
            lua_pushinteger(L, TL);
            lua_setfield(L, -2, "TL");
            lua_pushinteger(L, KS);
            lua_setfield(L, -2, "KS");
            lua_pushinteger(L, ML);
            lua_setfield(L, -2, "ML");
            lua_pushinteger(L, DT);
            lua_setfield(L, -2, "DT");
            lua_pushinteger(L, YM2612.CHANNEL[channel].SLOT[slot].AMSon ? 1 : 0);
            lua_setfield(L, -2, "AME");

            lua_rawseti(L, -2, 1 + slot);
        }
        lua_setfield(L, -2, "slot");

        int AMS = 0;
        for (int i = 0; i < 4; i++)
        {
            if (YM2612.CHANNEL[channel].AMS == LFO_AMS_TAB[i])
            {
                AMS = i;
                break;
            }
        }
        lua_pushinteger(L, AMS);
        lua_setfield(L, -2, "AMS");
        int PMS = 0;
        for (int i = 0; i < 8; i++)
        {
            if (YM2612.CHANNEL[channel].FMS == LFO_FMS_TAB[i])
            {
                PMS = i;
                break;
            }
        }
        lua_pushinteger(L, PMS);
        lua_setfield(L, -2, "PMS");

        lua_rawseti(L, -2, 1 + channel);
    }
    lua_setfield(L, -2, "channel");
    lua_setfield(L, -2, "ym2612");
    return 1;
}

DEFINE_LUA_FUNCTION(sound_clear, "")
{
    Clear_Sound_Buffer();
    return 0;
}

#ifdef _WIN32
const char* s_keyToName[256] =
{
    NULL,
    "leftclick",
    "rightclick",
    NULL,
    "middleclick",
    NULL,
    NULL,
    NULL,
    "backspace",
    "tab",
    NULL,
    NULL,
    NULL,
    "enter",
    NULL,
    NULL,
    "shift", // 0x10
    "control",
    "alt",
    "pause",
    "capslock",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "escape",
    NULL,
    NULL,
    NULL,
    NULL,
    "space", // 0x20
    "pageup",
    "pagedown",
    "end",
    "home",
    "left",
    "up",
    "right",
    "down",
    NULL,
    NULL,
    NULL,
    NULL,
    "insert",
    "delete",
    NULL,
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "numpad0", "numpad1", "numpad2", "numpad3", "numpad4", "numpad5", "numpad6", "numpad7", "numpad8", "numpad9",
    "numpad*", "numpad+",
    NULL,
    "numpad-", "numpad.", "numpad/",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "numlock",
    "scrolllock",
    NULL, // 0x92
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, // 0xB9
    "semicolon",
    "plus",
    "comma",
    "minus",
    "period",
    "slash",
    "tilde",
    NULL, // 0xC1
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, // 0xDA
    "leftbracket",
    "backslash",
    "rightbracket",
    "quote",
};
#endif

// input.get()
// takes no input, returns a lua table of entries representing the current input state,
// independent of the joypad buttons the emulated game thinks are pressed
// for example:
//   if the user is holding the W key and the left mouse button
//   and has the mouse at the bottom-right corner of the game screen,
//   then this would return {W=true, leftclick=true, xmouse=319, ymouse=223}
DEFINE_LUA_FUNCTION(input_getcurrentinputstatus, "")
{
    lua_newtable(L);

#ifdef _WIN32
    // keyboard and mouse button status
    {
        unsigned char keys[256];
        if (!BackgroundInput)
        {
            if (GetKeyboardState(keys))
            {
                for (int i = 1; i < 255; i++)
                {
                    int mask = (i == VK_CAPITAL || i == VK_NUMLOCK || i == VK_SCROLL) ? 0x01 : 0x80;
                    if (keys[i] & mask)
                    {
                        const char* name = s_keyToName[i];
                        if (name)
                        {
                            lua_pushboolean(L, true);
                            lua_setfield(L, -2, name);
                        }
                    }
                }
            }
        }
        else // use a slightly different method that will detect background input:
        {
            for (int i = 1; i < 255; i++)
            {
                const char* name = s_keyToName[i];
                if (name)
                {
                    int active;
                    if (i == VK_CAPITAL || i == VK_NUMLOCK || i == VK_SCROLL)
                        active = GetKeyState(i) & 0x01;
                    else
                        active = GetAsyncKeyState(i) & 0x8000;
                    if (active)
                    {
                        lua_pushboolean(L, true);
                        lua_setfield(L, -2, name);
                    }
                }
            }
        }
    }
    // mouse position in game screen pixel coordinates
    {
        POINT point;
        RECT rect, srcRectUnused;
        GetCursorPos(&point);
        void CalculateDrawArea(HWND hWnd, RECT& RectDest, RECT& RectSrc);
        CalculateDrawArea(HWnd, rect, srcRectUnused);
        int xres = FULL_X_RESOLUTION;
        int yres = FULL_Y_RESOLUTION;
        int x = ((point.x - rect.left) * xres) / max(1, rect.right - rect.left);
        int y = ((point.y - rect.top) * yres) / max(1, rect.bottom - rect.top);
        lua_pushinteger(L, x);
        lua_setfield(L, -2, "xmouse");
        lua_pushinteger(L, y);
        lua_setfield(L, -2, "ymouse");
    }
#else
    // NYI (well, return an empty table)
#endif

    return 1;
}

// gens.breakpoint(string message)
DEFINE_LUA_FUNCTION(gens_breakpoint, "[message=\"\"]")
{
    prompt_str = (const char *)(lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : "Breakpoint from LUA");
    M68kDW.Breakpoint(M68kDW.last_pc);
    return 0;
}

// resets our "worry" counter of the Lua state
int dontworry(LuaContextInfo& info)
{
    if (info.stopWorrying)
    {
        info.stopWorrying = false;
        if (info.worryCount)
            indicateBusy(info.L, false);
    }
    info.worryCount = 0;
    return 0;
}

static const struct luaL_reg genslib[] =
{
    { "frameadvance", gens_frameadvance },
    { "speedmode", gens_speedmode },
    { "wait", gens_wait },
    { "pause", gens_pause },
    { "unpause", gens_unpause },
    { "emulateframe", gens_emulateframe },
    //{"emulateframefastnoskipping", gens_emulateframefastnoskipping}, // removed from library because probably nobody would notice the difference from gens_emulateframe
    { "emulateframefast", gens_emulateframefast },
    { "emulateframeinvisible", gens_emulateframeinvisible },
    { "redraw", gens_redraw },
    { "framecount", gens_getframecount },
    { "lagcount", gens_getlagcount },
    { "lagged", gens_lagged },
    { "emulating", gens_emulating },
    { "atframeboundary", gens_atframeboundary },
    { "registerbefore", gens_registerbefore },
    { "registerafter", gens_registerafter },
    { "registerstart", gens_registerstart },
    { "registerexit", gens_registerexit },
    { "persistglobalvariables", gens_persistglobalvariables },
    { "message", gens_message },
    { "print", print }, // sure, why not
    { "openscript", gens_openscript },
    { "loadrom", gens_loadrom },
    { "hardreset", gens_hardreset },
    // alternative names
    { "openrom", gens_loadrom },
    // r57shell mod
    { "breakpoint", gens_breakpoint },
    { NULL, NULL }
};
static const struct luaL_reg guilib[] =
{
    { "register", gui_register },
    { "text", gui_text },
    { "box", gui_box },
    { "line", gui_line },
    { "pixel", gui_pixel },
    { "getpixel", gui_getpixel },
    { "opacity", gui_setopacity },
    { "transparency", gui_settransparency },
    { "popup", gui_popup },
    { "parsecolor", gui_parsecolor },
    { "gdscreenshot", gui_gdscreenshot },
    { "gdoverlay", gui_gdoverlay },
    { "redraw", gens_redraw }, // some people might think of this as more of a GUI function
    // alternative names
    { "drawtext", gui_text },
    { "drawbox", gui_box },
    { "drawline", gui_line },
    { "drawpixel", gui_pixel },
    { "setpixel", gui_pixel },
    { "writepixel", gui_pixel },
    { "readpixel", gui_getpixel },
    { "rect", gui_box },
    { "drawrect", gui_box },
    { "drawimage", gui_gdoverlay },
    { "image", gui_gdoverlay },
    { NULL, NULL }
};
static const struct luaL_reg statelib[] =
{
    { "create", state_create },
    { "save", state_save },
    { "load", state_load },
    { "loadscriptdata", state_loadscriptdata },
    { "savescriptdata", state_savescriptdata },
    { "registersave", state_registersave },
    { "registerload", state_registerload },
    { NULL, NULL }
};
static const struct luaL_reg memorylib[] =
{
    { "readbyte", memory_readbyte },
    { "readbytesigned", memory_readbytesigned },
    { "readword", memory_readword },
    { "readwordsigned", memory_readwordsigned },
    { "readdword", memory_readdword },
    { "readdwordsigned", memory_readdwordsigned },
    { "readbyterange", memory_readbyterange },
    { "writebyte", memory_writebyte },
    { "writeword", memory_writeword },
    { "writedword", memory_writedword },
    { "writebyterange", memory_writebyterange },
    { "isvalid", memory_isvalid },
    { "getregister", memory_getregister },
    { "setregister", memory_setregister },
    // alternate naming scheme for word and double-word and unsigned
    { "readbyteunsigned", memory_readbyte },
    { "readwordunsigned", memory_readword },
    { "readdwordunsigned", memory_readdword },
    { "readshort", memory_readword },
    { "readshortunsigned", memory_readword },
    { "readshortsigned", memory_readwordsigned },
    { "readlong", memory_readdword },
    { "readlongunsigned", memory_readdword },
    { "readlongsigned", memory_readdwordsigned },
    { "writeshort", memory_writeword },
    { "writelong", memory_writedword },

    // memory hooks
    { "registerwrite", memory_registerwrite },
    { "registerread", memory_registerread },
    { "registerexec", memory_registerexec },
    // alternate names
    { "register", memory_registerwrite },
    { "registerrun", memory_registerexec },
    { "registerexecute", memory_registerexec },

    { NULL, NULL }
};
static const struct luaL_reg joylib[] =
{
    { "get", joy_get },
    { "getdown", joy_getdown },
    { "getup", joy_getup },
    { "peek", joy_peek },
    { "peekdown", joy_peekdown },
    { "peekup", joy_peekup },
    { "set", joy_set },
    // alternative names
    { "read", joy_get },
    { "write", joy_set },
    { "readdown", joy_getdown },
    { "readup", joy_getup },
    { NULL, NULL }
};
static const struct luaL_reg inputlib[] =
{
    { "get", input_getcurrentinputstatus },
    { "registerhotkey", input_registerhotkey },
    { "popup", input_popup },
    { "prompt", input_prompt },
    // alternative names
    { "read", input_getcurrentinputstatus },
    { NULL, NULL }
};
static const struct luaL_reg movielib[] =
{
    { "active", movie_isactive },
    { "recording", movie_isrecording },
    { "playing", movie_isplaying },
    { "mode", movie_getmode },

    { "length", movie_getlength },
    { "name", movie_getname },
    { "rerecordcount", movie_rerecordcount },
    { "setrerecordcount", movie_setrerecordcount },

    { "rerecordcounting", gens_rerecordcounting },
    { "readonly", movie_getreadonly },
    { "setreadonly", movie_setreadonly },
    { "framecount", gens_getframecount }, // for those familiar with other emulators that have movie.framecount() instead of emulatorname.framecount()

    { "play", movie_play },
    { "replay", movie_replay },
    { "stop", movie_close },

    // alternative names
    { "open", movie_play },
    { "close", movie_close },
    { "getname", movie_getname },
    { "playback", movie_play },
    { "getreadonly", movie_getreadonly },
    { NULL, NULL }
};
static const struct luaL_reg soundlib[] =
{
    { "get", sound_get },
    { "clear", sound_clear },
    { NULL, NULL }
};
static const struct luaL_reg tilelib[] =
{
    { "new", tile_create },
    { "create", tile_create },
    { "getpixel", tile_getpixel },
    { "readpixel", tile_getpixel },
    { "setpixel", tile_setpixel },
    { "writepixel", tile_setpixel },
    { "rawdata", tile_getraw },
    { NULL, NULL }
};
static const struct luaL_reg pallib[] =
{
    { "new", pal_create },
    { "create", pal_create },
    { "getcolor", pal_getcolor },
    { "readcolor", pal_getcolor },
    { "setcolor", pal_setcolor },
    { "writecolor", pal_setcolor },
    { "rawdata", pal_getraw },
    { NULL, NULL }
};
static const struct luaL_reg vdplib[] =
{
    { "readcell", vdp_readcell },
    { "writecell", vdp_writecell },
    { "readpalette", vdp_readpalette },
    { "writepalette", vdp_writepalette },
    { NULL, NULL }
};

static const struct CFuncInfo
{
    const char* library;
    const char* name;
    const char* args;
    bool registry;
}
cFuncInfo[] = // this info is stored here to avoid having to change all of Lua's libraries to use something like DEFINE_LUA_FUNCTION
{
    { LUA_STRLIBNAME, "byte", "str[,start[,end]]" },
    { LUA_STRLIBNAME, "char", "...[bytes]" },
    { LUA_STRLIBNAME, "dump", "func" },
    { LUA_STRLIBNAME, "find", "str,pattern[,init[,plain]]" },
    { LUA_STRLIBNAME, "format", "formatstring,..." },
    { LUA_STRLIBNAME, "gfind", "!deprecated!" },
    { LUA_STRLIBNAME, "gmatch", "str,pattern" },
    { LUA_STRLIBNAME, "gsub", "str,pattern,repl[,n]" },
    { LUA_STRLIBNAME, "len", "str" },
    { LUA_STRLIBNAME, "lower", "str" },
    { LUA_STRLIBNAME, "match", "str,pattern[,init]" },
    { LUA_STRLIBNAME, "rep", "str,n" },
    { LUA_STRLIBNAME, "reverse", "str" },
    { LUA_STRLIBNAME, "sub", "str,start[,end]" },
    { LUA_STRLIBNAME, "upper", "str" },
    { NULL, "module", "name[,...]" },
    { NULL, "require", "modname" },
    { LUA_LOADLIBNAME, "loadlib", "libname,funcname" },
    { LUA_LOADLIBNAME, "seeall", "module" },
    { LUA_COLIBNAME, "create", "func" },
    { LUA_COLIBNAME, "resume", "co[,val1,...]" },
    { LUA_COLIBNAME, "running", "" },
    { LUA_COLIBNAME, "status", "co" },
    { LUA_COLIBNAME, "wrap", "func" },
    { LUA_COLIBNAME, "yield", "..." },
    { NULL, "assert", "cond[,message]" },
    { NULL, "collectgarbage", "opt[,arg]" },
    { NULL, "gcinfo", "" },
    { NULL, "dofile", "filename" },
    { NULL, "error", "message[,level]" },
    { NULL, "getfenv", "[level_or_func]" },
    { NULL, "getmetatable", "object" },
    { NULL, "ipairs", "arraytable" },
    { NULL, "load", "func[,chunkname]" },
    { NULL, "loadfile", "[filename]" },
    { NULL, "loadstring", "str[,chunkname]" },
    { NULL, "next", "table[,index]" },
    { NULL, "pairs", "table" },
    { NULL, "pcall", "func,arg1,..." },
    { NULL, "rawequal", "v1,v2" },
    { NULL, "rawget", "table,index" },
    { NULL, "rawset", "table,index,value" },
    { NULL, "select", "index,..." },
    { NULL, "setfenv", "level_or_func,envtable" },
    { NULL, "setmetatable", "table,metatable" },
    { NULL, "tonumber", "str_or_num[,base]" },
    { NULL, "type", "obj" },
    { NULL, "unpack", "list[,i=1[,j=#list]]" },
    { NULL, "xpcall", "func,errhandler" },
    { NULL, "newproxy", "hasmeta" },
    { LUA_MATHLIBNAME, "abs", "x" },
    { LUA_MATHLIBNAME, "acos", "x" },
    { LUA_MATHLIBNAME, "asin", "x" },
    { LUA_MATHLIBNAME, "atan", "x" },
    { LUA_MATHLIBNAME, "atan2", "y,x" },
    { LUA_MATHLIBNAME, "ceil", "x" },
    { LUA_MATHLIBNAME, "cos", "rads" },
    { LUA_MATHLIBNAME, "cosh", "x" },
    { LUA_MATHLIBNAME, "deg", "rads" },
    { LUA_MATHLIBNAME, "exp", "x" },
    { LUA_MATHLIBNAME, "floor", "x" },
    { LUA_MATHLIBNAME, "fmod", "x,y" },
    { LUA_MATHLIBNAME, "frexp", "x" },
    { LUA_MATHLIBNAME, "ldexp", "m,e" },
    { LUA_MATHLIBNAME, "log", "x" },
    { LUA_MATHLIBNAME, "log10", "x" },
    { LUA_MATHLIBNAME, "max", "x,..." },
    { LUA_MATHLIBNAME, "min", "x,..." },
    { LUA_MATHLIBNAME, "modf", "x" },
    { LUA_MATHLIBNAME, "pow", "x,y" },
    { LUA_MATHLIBNAME, "rad", "degs" },
    { LUA_MATHLIBNAME, "random", "[m[,n]]" },
    { LUA_MATHLIBNAME, "randomseed", "x" },
    { LUA_MATHLIBNAME, "sin", "rads" },
    { LUA_MATHLIBNAME, "sinh", "x" },
    { LUA_MATHLIBNAME, "sqrt", "x" },
    { LUA_MATHLIBNAME, "tan", "rads" },
    { LUA_MATHLIBNAME, "tanh", "x" },
    { LUA_IOLIBNAME, "close", "[file]" },
    { LUA_IOLIBNAME, "flush", "" },
    { LUA_IOLIBNAME, "input", "[file]" },
    { LUA_IOLIBNAME, "lines", "[filename]" },
    { LUA_IOLIBNAME, "open", "filename[,mode=\"r\"]" },
    { LUA_IOLIBNAME, "output", "[file]" },
    { LUA_IOLIBNAME, "popen", "prog,[model]" },
    { LUA_IOLIBNAME, "read", "..." },
    { LUA_IOLIBNAME, "tmpfile", "" },
    { LUA_IOLIBNAME, "type", "obj" },
    { LUA_IOLIBNAME, "write", "..." },
    { LUA_OSLIBNAME, "clock", "" },
    { LUA_OSLIBNAME, "date", "[format[,time]]" },
    { LUA_OSLIBNAME, "difftime", "t2,t1" },
    { LUA_OSLIBNAME, "execute", "[command]" },
    { LUA_OSLIBNAME, "exit", "[code]" },
    { LUA_OSLIBNAME, "getenv", "varname" },
    { LUA_OSLIBNAME, "remove", "filename" },
    { LUA_OSLIBNAME, "rename", "oldname,newname" },
    { LUA_OSLIBNAME, "setlocale", "locale[,category]" },
    { LUA_OSLIBNAME, "time", "[timetable]" },
    { LUA_OSLIBNAME, "tmpname", "" },
    { LUA_DBLIBNAME, "debug", "" },
    { LUA_DBLIBNAME, "getfenv", "o" },
    { LUA_DBLIBNAME, "gethook", "[thread]" },
    { LUA_DBLIBNAME, "getinfo", "[thread,]function[,what]" },
    { LUA_DBLIBNAME, "getlocal", "[thread,]level,local" },
    { LUA_DBLIBNAME, "getmetatable", "[object]" },
    { LUA_DBLIBNAME, "getregistry", "" },
    { LUA_DBLIBNAME, "getupvalue", "func,up" },
    { LUA_DBLIBNAME, "setfenv", "object,table" },
    { LUA_DBLIBNAME, "sethook", "[thread,]hook,mask[,count]" },
    { LUA_DBLIBNAME, "setlocal", "[thread,]level,local,value" },
    { LUA_DBLIBNAME, "setmetatable", "object,table" },
    { LUA_DBLIBNAME, "setupvalue", "func,up,value" },
    { LUA_DBLIBNAME, "traceback", "[thread,][message][,level]" },
    { LUA_TABLIBNAME, "concat", "table[,sep[,i[,j]]]" },
    { LUA_TABLIBNAME, "insert", "table,[pos,]value" },
    { LUA_TABLIBNAME, "maxn", "table" },
    { LUA_TABLIBNAME, "remove", "table[,pos]" },
    { LUA_TABLIBNAME, "sort", "table[,comp]" },
    { LUA_TABLIBNAME, "foreach", "table,func" },
    { LUA_TABLIBNAME, "foreachi", "table,func" },
    { LUA_TABLIBNAME, "getn", "table" },
    { LUA_TABLIBNAME, "maxn", "table" },
    { LUA_TABLIBNAME, "setn", "table,value" }, // I know some of these are obsolete but they should still have argument info if they're exposed to the user
    { LUA_FILEHANDLE, "setvbuf", "mode[,size]", true },
    { LUA_FILEHANDLE, "lines", "", true },
    { LUA_FILEHANDLE, "read", "...", true },
    { LUA_FILEHANDLE, "flush", "", true },
    { LUA_FILEHANDLE, "seek", "[whence][,offset]", true },
    { LUA_FILEHANDLE, "write", "...", true },
    { LUA_FILEHANDLE, "__tostring", "obj", true },
    { LUA_FILEHANDLE, "__gc", "", true },
    { "_LOADLIB", "__gc", "", true },
};

void registerLibs(lua_State* L)
{
    luaL_openlibs(L);

    luaL_register(L, "emu", genslib); // added for better cross-emulator compatibility
    luaL_register(L, "gens", genslib); // kept for backward compatibility
    luaL_register(L, "gui", guilib);
    luaL_register(L, "savestate", statelib);
    luaL_register(L, "memory", memorylib);
    luaL_register(L, "joypad", joylib); // for game input
    luaL_register(L, "input", inputlib); // for user input
    luaL_register(L, "movie", movielib);
    luaL_register(L, "sound", soundlib);
    luaL_register(L, "bit", bit_funcs); // LuaBitOp library
    luaL_register(L, "vdp", vdplib);
    luaL_register(L, "tile", tilelib);
    luaL_register(L, "palette", pallib);
    luaL_register(L, "pal", pallib);
    lua_settop(L, 0); // clean the stack, because each call to luaL_register leaves a table on top

    // register a few utility functions outside of libraries (in the global namespace)
    lua_register(L, "print", print);
    lua_register(L, "tostring", tostring);
    lua_register(L, "addressof", addressof);
    lua_register(L, "copytable", copytable);

    // old bit operation functions
    lua_register(L, "AND", bit_band);
    lua_register(L, "OR", bit_bor);
    lua_register(L, "XOR", bit_bxor);
    lua_register(L, "SHIFT", bitshift);
    lua_register(L, "BIT", bitbit);

    luabitop_validate(L);

    // populate s_cFuncInfoMap the first time
    static bool once = true;
    if (once)
    {
        once = false;

        for (int i = 0; i < sizeof(cFuncInfo) / sizeof(*cFuncInfo); i++)
        {
            const CFuncInfo& cfi = cFuncInfo[i];
            if (cfi.registry)
            {
                lua_getregistry(L);
                lua_getfield(L, -1, cfi.library);
                lua_remove(L, -2);
                lua_getfield(L, -1, cfi.name);
                lua_remove(L, -2);
            }
            else if (cfi.library)
            {
                lua_getfield(L, LUA_GLOBALSINDEX, cfi.library);
                lua_getfield(L, -1, cfi.name);
                lua_remove(L, -2);
            }
            else
            {
                lua_getfield(L, LUA_GLOBALSINDEX, cfi.name);
            }

            lua_CFunction func = lua_tocfunction(L, -1);
            s_cFuncInfoMap[func] = cfi.args;
            lua_pop(L, 1);
        }

        // deal with some stragglers
        lua_getfield(L, LUA_GLOBALSINDEX, "package");
        lua_getfield(L, -1, "loaders");
        lua_remove(L, -2);
        if (lua_istable(L, -1))
        {
            for (int i = 1;; i++)
            {
                lua_rawgeti(L, -1, i);
                lua_CFunction func = lua_tocfunction(L, -1);
                lua_pop(L, 1);
                if (!func)
                    break;
                s_cFuncInfoMap[func] = "name";
            }
        }
        lua_pop(L, 1);
    }

    // push arrays for storing hook functions in
    for (int i = 0; i < LUAMEMHOOK_COUNT; i++)
    {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, luaMemHookTypeStrings[i]);
    }
}

void ResetInfo(LuaContextInfo& info)
{
    info.L = NULL;
    info.started = false;
    info.running = false;
    info.returned = false;
    info.crashed = false;
    info.restart = false;
    info.restartLater = false;
    info.worryCount = 0;
    info.stopWorrying = false;
    info.panic = false;
    info.ranExit = false;
    info.numDeferredGUIFuncs = 0;
    info.ranFrameAdvance = false;
    info.transparencyModifier = 255;
    info.speedMode = SPEEDMODE_NORMAL;
    info.guiFuncsNeedDeferring = false;
    info.dataSaveKey = 0;
    info.dataLoadKey = 0;
    info.dataSaveLoadKeySet = false;
    info.rerecordCountingDisabled = false;
    info.numMemHooks = 0;
    info.persistVars.clear();
    info.newDefaultData.ClearRecords();
}

void OpenLuaContext(int uid, void(*print)(int uid, const char* str), void(*onstart)(int uid), void(*onstop)(int uid, bool statusOK))
{
    LuaContextInfo* newInfo = new LuaContextInfo();
    ResetInfo(*newInfo);
    newInfo->print = print;
    newInfo->onstart = onstart;
    newInfo->onstop = onstop;
    luaContextInfo[uid] = newInfo;
}

void RunLuaScriptFile(int uid, const char* filenameCStr)
{
    if (luaContextInfo.find(uid) == luaContextInfo.end())
        return;
    StopLuaScript(uid);

    LuaContextInfo& info = *luaContextInfo[uid];

#ifdef USE_INFO_STACK
    infoStack.insert(infoStack.begin(), &info);
    struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope; // doing it like this makes sure that the info stack gets cleaned up even if an exception is thrown
#endif

    info.nextFilename = filenameCStr;

    if (info.running)
    {
        // it's a little complicated, but... the call to luaL_dofile below
        // could call a C function that calls this very function again
        // additionally, if that happened then the above call to StopLuaScript
        // probably couldn't stop the script yet, so instead of continuing,
        // we'll set a flag that tells the first call of this function to loop again
        // when the script is able to stop safely
        info.restart = true;
        return;
    }

    do
    {
        std::string filename = info.nextFilename;

        lua_State* L = lua_open();
#ifndef USE_INFO_STACK
        luaStateToContextMap[L] = &info;
#endif
        luaStateToUIDMap[L] = uid;
        ResetInfo(info);
        info.L = L;
        info.guiFuncsNeedDeferring = true;
        info.lastFilename = filename;

        SetSaveKey(info, FilenameFromPath(filename.c_str()));
        info.dataSaveLoadKeySet = false;

        registerLibs(L);

        // register a function to periodically check for inactivity
        lua_sethook(L, LuaRescueHook, LUA_MASKCOUNT, HOOKCOUNT);

        // deferred evaluation table
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, deferredGUIIDString);

        info.started = true;
        RefreshScriptStartedStatus();
        if (info.onstart)
            info.onstart(uid);
        info.running = true;
        RefreshScriptSpeedStatus();
        info.returned = false;
        int errorcode = luaL_dofile(L, filename.c_str());
        info.running = false;
        RefreshScriptSpeedStatus();
        info.returned = true;

        if (errorcode)
        {
            info.crashed = true;
            if (info.print)
            {
                info.print(uid, lua_tostring(L, -1));
                info.print(uid, "\r\n");
            }
            else
            {
                fprintf(stderr, "%s\n", lua_tostring(L, -1));
            }
            StopLuaScript(uid);
        }
        else
        {
            Show_Genesis_Screen();
            StopScriptIfFinished(uid, true);
        }
    } while (info.restart);
}

void StopScriptIfFinished(int uid, bool justReturned)
{
    LuaContextInfo& info = *luaContextInfo[uid];
    if (!info.returned)
        return;

    // the script has returned, but it is not necessarily done running
    // because it may have registered a function that it expects to keep getting called
    // so check if it has any registered functions and stop the script only if it doesn't

    bool keepAlive = (info.numMemHooks != 0);
    for (int calltype = 0; calltype < LUACALL_COUNT && !keepAlive; calltype++)
    {
        lua_State* L = info.L;
        if (L)
        {
            const char* idstring = luaCallIDStrings[calltype];
            lua_getfield(L, LUA_REGISTRYINDEX, idstring);
            bool isFunction = lua_isfunction(L, -1);
            lua_pop(L, 1);

            if (isFunction)
                keepAlive = true;
        }
    }

    if (keepAlive)
    {
        if (justReturned)
        {
            if (info.print)
                info.print(uid, "script returned but is still running registered functions\r\n");
            else
                fprintf(stderr, "%s\n", "script returned but is still running registered functions");
        }
    }
    else
    {
        if (info.print)
            info.print(uid, "script finished running\r\n");
        else
            fprintf(stderr, "%s\n", "script finished running");

        StopLuaScript(uid);
    }
}

void RequestAbortLuaScript(int uid, const char* message)
{
    if (luaContextInfo.find(uid) == luaContextInfo.end())
        return;
    LuaContextInfo& info = *luaContextInfo[uid];
    lua_State* L = info.L;
    if (L)
    {
        // this probably isn't the right way to do it
        // but calling luaL_error here is positively unsafe
        // (it seemingly works fine but sometimes corrupts the emulation state in colorful ways)
        // and this works pretty well and is definitely safe, so screw it
        info.L->hookcount = 1; // run hook function as soon as possible
        info.panic = true; // and call luaL_error once we're inside the hook function
        if (message)
        {
            strncpy(info.panicMessage, message, sizeof(info.panicMessage));
            info.panicMessage[sizeof(info.panicMessage) - 1] = 0;
        }
        else
        {
            // attach file/line info because this is the case where it's most necessary to see that,
            // and often it won't be possible for the later luaL_error call to retrieve it otherwise.
            // this means sometimes printing multiple file/line numbers if luaL_error does find something,
            // but that's fine since more information is probably better anyway.
            luaL_where(L, 0); // should be 0 and not 1 here to get useful (on force stop) messages
            const char* whereString = lua_tostring(L, -1);
            snprintf(info.panicMessage, sizeof(info.panicMessage), "%sscript terminated", whereString);
            lua_pop(L, 1);
        }
    }
}

void SetSaveKey(LuaContextInfo& info, const char* key)
{
    info.dataSaveKey = crc32(0, (const unsigned char*)key, strlen(key));

    if (!info.dataSaveLoadKeySet)
    {
        info.dataLoadKey = info.dataSaveKey;
        info.dataSaveLoadKeySet = true;
    }
}
void SetLoadKey(LuaContextInfo& info, const char* key)
{
    info.dataLoadKey = crc32(0, (const unsigned char*)key, strlen(key));

    if (!info.dataSaveLoadKeySet)
    {
        info.dataSaveKey = info.dataLoadKey;
        info.dataSaveLoadKeySet = true;
    }
}

void HandleCallbackError(lua_State* L, LuaContextInfo& info, int uid, bool stopScript)
{
    info.crashed = true;
    if (L->errfunc || L->errorJmp)
        luaL_error(L, lua_tostring(L, -1));
    else
    {
        if (info.print)
        {
            info.print(uid, lua_tostring(L, -1));
            info.print(uid, "\r\n");
        }
        else
        {
            fprintf(stderr, "%s\n", lua_tostring(L, -1));
        }
        if (stopScript)
            StopLuaScript(uid);
    }
}

void CallExitFunction(int uid)
{
    LuaContextInfo& info = *luaContextInfo[uid];
    lua_State* L = info.L;

    if (!L)
        return;

    dontworry(info);

    // first call the registered exit function if there is one
    if (!info.ranExit)
    {
        info.ranExit = true;

#ifdef USE_INFO_STACK
        infoStack.insert(infoStack.begin(), &info);
        struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope;
#endif

        lua_settop(L, 0);
        lua_getfield(L, LUA_REGISTRYINDEX, luaCallIDStrings[LUACALL_BEFOREEXIT]);

        int errorcode = 0;
        if (lua_isfunction(L, -1))
        {
            bool wasRunning = info.running;
            info.running = true;
            RefreshScriptSpeedStatus();

            bool wasPanic = info.panic;
            info.panic = false; // otherwise we could barely do anything in the exit function

            errorcode = lua_pcall(L, 0, 0, 0);

            info.panic |= wasPanic; // restore panic

            info.running = wasRunning;
            RefreshScriptSpeedStatus();
        }

        // save persisted variable info after the exit function runs (even if it crashed)
        {
            // gather the final value of the variables we're supposed to persist
            LuaSaveData newExitData;
            {
                int numPersistVars = info.persistVars.size();
                for (int i = 0; i < numPersistVars; i++)
                {
                    const char* varName = info.persistVars[i].c_str();
                    lua_getfield(L, LUA_GLOBALSINDEX, varName);
                    int type = lua_type(L, -1);
                    unsigned int varNameCRC = crc32(0, (const unsigned char*)varName, strlen(varName));
                    newExitData.SaveRecordPartial(uid, varNameCRC, -1);
                    lua_pop(L, 1);
                }
            }

            char path[1024] = { 0 };
            char* pathTypeChrPtr = ConstructScriptSaveDataPath(path, 1024, info);

            *pathTypeChrPtr = 'd';
            if (info.newDefaultData.recordList)
            {
                FILE* defaultsFile = fopen(path, "wb");
                if (defaultsFile)
                {
                    info.newDefaultData.ExportRecords(defaultsFile);
                    fclose(defaultsFile);
                }
            }
            else unlink(path);

            *pathTypeChrPtr = 'e';
            if (newExitData.recordList)
            {
                FILE* persistFile = fopen(path, "wb");
                if (persistFile)
                {
                    newExitData.ExportRecords(persistFile);
                    fclose(persistFile);
                }
            }
            else unlink(path);
        }

        if (errorcode)
            HandleCallbackError(L, info, uid, false);
    }
}

void StopLuaScript(int uid)
{
    LuaContextInfo* infoPtr = luaContextInfo[uid];
    if (!infoPtr)
        return;

    LuaContextInfo& info = *infoPtr;

    if (info.running)
    {
        // if it's currently running then we can't stop it now without crashing
        // so the best we can do is politely request for it to go kill itself
        RequestAbortLuaScript(uid);
        return;
    }

    lua_State* L = info.L;
    if (L)
    {
        CallExitFunction(uid);

        if (info.onstop)
        {
            info.stopWorrying = true, info.worryCount++, dontworry(info); // clear "busy" status
            info.onstop(uid, !info.crashed); // must happen before closing L and after the exit function, otherwise the final GUI state of the script won't be shown properly or at all
        }

        if (info.started) // this check is necessary
        {
            lua_close(L);
#ifndef USE_INFO_STACK
            luaStateToContextMap.erase(L);
#endif
            luaStateToUIDMap.erase(L);
            info.L = NULL;
            info.started = false;

            info.numMemHooks = 0;
            for (int i = 0; i < LUAMEMHOOK_COUNT; i++)
                CalculateMemHookRegions((LuaMemHookType)i);
        }
        RefreshScriptStartedStatus();
    }
}

void CloseLuaContext(int uid)
{
    StopLuaScript(uid);
    delete luaContextInfo[uid];
    luaContextInfo.erase(uid);
}

// the purpose of this structure is to provide a way of
// QUICKLY determining whether a memory address range has a hook associated with it,
// with a bias toward fast rejection because the majority of addresses will not be hooked.
// (it must not use any part of Lua or perform any per-script operations,
//  otherwise it would definitely be too slow.)
// calculating the regions when a hook is added/removed may be slow,
// but this is an intentional tradeoff to obtain a high speed of checking during later execution
struct TieredRegion
{
    template<unsigned int maxGap>
    struct Region
    {
        struct Island
        {
            unsigned int start;
            unsigned int end;
            __forceinline bool Contains(unsigned int address, int size) const { return address < end && address + size > start; }
        };
        std::vector<Island> islands;

        void Calculate(const std::vector<unsigned int>& bytes)
        {
            islands.clear();

            unsigned int lastEnd = ~0;

            std::vector<unsigned int>::const_iterator iter = bytes.begin();
            std::vector<unsigned int>::const_iterator end = bytes.end();
            for (; iter != end; ++iter)
            {
                unsigned int addr = *iter;
                if (addr < lastEnd || addr > lastEnd + (long long)maxGap)
                {
                    islands.push_back(Island());
                    islands.back().start = addr;
                }
                islands.back().end = addr + 1;
                lastEnd = addr + 1;
            }
        }

        bool Contains(unsigned int address, int size) const
        {
            std::vector<Island>::const_iterator iter = islands.begin();
            std::vector<Island>::const_iterator end = islands.end();
            for (; iter != end; ++iter)
                if (iter->Contains(address, size))
                    return true;
            return false;
        }
    };

    Region<0xFFFFFFFF> broad;
    Region<0x1000> mid;
    Region<0> narrow;

    void Calculate(std::vector<unsigned int>& bytes)
    {
        std::sort(bytes.begin(), bytes.end());

        broad.Calculate(bytes);
        mid.Calculate(bytes);
        narrow.Calculate(bytes);
    }

    TieredRegion()
    {
        Calculate(std::vector<unsigned int>());
    }

    __forceinline int NotEmpty()
    {
        return broad.islands.size();
    }

    // note: it is illegal to call this if NotEmpty() returns 0
    __forceinline bool Contains(unsigned int address, int size)
    {
        return broad.islands[0].Contains(address, size) &&
            mid.Contains(address, size) &&
            narrow.Contains(address, size);
    }
};
TieredRegion hookedRegions[LUAMEMHOOK_COUNT];

static void CalculateMemHookRegions(LuaMemHookType hookType)
{
    std::vector<unsigned int> hookedBytes;
    std::map<int, LuaContextInfo*>::iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::iterator end = luaContextInfo.end();
    while (iter != end)
    {
        LuaContextInfo& info = *iter->second;
        if (info.numMemHooks)
        {
            lua_State* L = info.L;
            if (L)
            {
                lua_settop(L, 0);
                lua_getfield(L, LUA_REGISTRYINDEX, luaMemHookTypeStrings[hookType]);
                lua_pushnil(L);
                while (lua_next(L, -2))
                {
                    if (lua_isfunction(L, -1))
                    {
                        unsigned int addr = lua_tointeger(L, -2);
                        hookedBytes.push_back(addr);
                    }
                    lua_pop(L, 1);
                }
                lua_settop(L, 0);
            }
        }
        ++iter;
    }
    hookedRegions[hookType].Calculate(hookedBytes);
}

static void CallRegisteredLuaMemHook_LuaMatch(unsigned int address, int size, unsigned int value, LuaMemHookType hookType)
{
    std::map<int, LuaContextInfo*>::iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::iterator end = luaContextInfo.end();
    while (iter != end)
    {
        LuaContextInfo& info = *iter->second;
        if (info.numMemHooks)
        {
            lua_State* L = info.L;
            if (L && !info.panic)
            {
#ifdef USE_INFO_STACK
                infoStack.insert(infoStack.begin(), &info);
                struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope;
#endif
                lua_settop(L, 0);
                lua_getfield(L, LUA_REGISTRYINDEX, luaMemHookTypeStrings[hookType]);
                for (int i = address; i != address + size; i++)
                {
                    lua_rawgeti(L, -1, i);
                    if (lua_isfunction(L, -1))
                    {
                        bool wasRunning = info.running;
                        info.running = true;
                        RefreshScriptSpeedStatus();
                        lua_pushinteger(L, address);
                        lua_pushinteger(L, size);
                        int errorcode = lua_pcall(L, 2, 0, 0);
                        info.running = wasRunning;
                        RefreshScriptSpeedStatus();
                        if (errorcode)
                        {
                            int uid = iter->first;
                            HandleCallbackError(L, info, uid, true);
                        }
                        break;
                    }
                    else
                    {
                        lua_pop(L, 1);
                    }
                }
                lua_settop(L, 0);
            }
        }
        ++iter;
    }
}
void CallRegisteredLuaMemHook(unsigned int address, int size, unsigned int value, LuaMemHookType hookType)
{
    // performance critical! (called VERY frequently)
    // I suggest timing a large number of calls to this function in Release if you change anything in here,
    // before and after, because even the most innocent change can make it become 30% to 400% slower.
    // a good amount to test is: 100000000 calls with no hook set, and another 100000000 with a hook set.
    // (on my system that consistently took 200 ms total in the former case and 350 ms total in the latter case)
    if (hookedRegions[hookType].NotEmpty())
    {
        if ((hookType <= LUAMEMHOOK_EXEC) && (address >= 0xE00000))
            address |= 0xFF0000; // account for mirroring of RAM
        if (hookedRegions[hookType].Contains(address, size))
            CallRegisteredLuaMemHook_LuaMatch(address, size, value, hookType); // something has hooked this specific address
    }
}

void CallRegisteredLuaFunctions(LuaCallID calltype)
{
    assert((unsigned int)calltype < (unsigned int)LUACALL_COUNT);
    const char* idstring = luaCallIDStrings[calltype];

    std::map<int, LuaContextInfo*>::iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::iterator end = luaContextInfo.end();
    while (iter != end)
    {
        int uid = iter->first;
        LuaContextInfo& info = *iter->second;
        lua_State* L = info.L;
        if (L && (!info.panic || calltype == LUACALL_BEFOREEXIT))
        {
#ifdef USE_INFO_STACK
            infoStack.insert(infoStack.begin(), &info);
            struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope;
#endif
            // handle deferred GUI function calls and disabling deferring when unnecessary
            if (calltype == LUACALL_AFTEREMULATIONGUI || calltype == LUACALL_AFTEREMULATION)
                info.guiFuncsNeedDeferring = false;
            if (calltype == LUACALL_AFTEREMULATIONGUI)
                CallDeferredFunctions(L, deferredGUIIDString);

            lua_settop(L, 0);
            lua_getfield(L, LUA_REGISTRYINDEX, idstring);

            if (lua_isfunction(L, -1))
            {
                bool wasRunning = info.running;
                info.running = true;
                RefreshScriptSpeedStatus();
                int errorcode = lua_pcall(L, 0, 0, 0);
                info.running = wasRunning;
                RefreshScriptSpeedStatus();
                if (errorcode)
                    HandleCallbackError(L, info, uid, true);
            }
            else
            {
                lua_pop(L, 1);
            }

            info.guiFuncsNeedDeferring = true;
        }

        ++iter;
    }
}

void CallRegisteredLuaSaveFunctions(int savestateNumber, LuaSaveData& saveData)
{
    const char* idstring = luaCallIDStrings[LUACALL_BEFORESAVE];

    std::map<int, LuaContextInfo*>::iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::iterator end = luaContextInfo.end();
    while (iter != end)
    {
        int uid = iter->first;
        LuaContextInfo& info = *iter->second;
        lua_State* L = info.L;
        if (L)
        {
#ifdef USE_INFO_STACK
            infoStack.insert(infoStack.begin(), &info);
            struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope;
#endif

            lua_settop(L, 0);
            lua_getfield(L, LUA_REGISTRYINDEX, idstring);

            if (lua_isfunction(L, -1))
            {
                bool wasRunning = info.running;
                info.running = true;
                RefreshScriptSpeedStatus();
                lua_pushinteger(L, savestateNumber);
                int errorcode = lua_pcall(L, 1, LUA_MULTRET, 0);
                info.running = wasRunning;
                RefreshScriptSpeedStatus();
                if (errorcode)
                    HandleCallbackError(L, info, uid, true);
                saveData.SaveRecord(uid, info.dataSaveKey);
            }
            else
            {
                lua_pop(L, 1);
            }
        }

        ++iter;
    }
}

void CallRegisteredLuaLoadFunctions(int savestateNumber, const LuaSaveData& saveData)
{
    const char* idstring = luaCallIDStrings[LUACALL_AFTERLOAD];

    std::map<int, LuaContextInfo*>::iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::iterator end = luaContextInfo.end();
    while (iter != end)
    {
        int uid = iter->first;
        LuaContextInfo& info = *iter->second;
        lua_State* L = info.L;
        if (L)
        {
#ifdef USE_INFO_STACK
            infoStack.insert(infoStack.begin(), &info);
            struct Scope { ~Scope() { infoStack.erase(infoStack.begin()); } } scope;
#endif

            lua_settop(L, 0);
            lua_getfield(L, LUA_REGISTRYINDEX, idstring);

            if (lua_isfunction(L, -1))
            {
                bool wasRunning = info.running;
                info.running = true;
                RefreshScriptSpeedStatus();

                // since the scriptdata can be very expensive to load
                // (e.g. the registered save function returned some huge tables)
                // check the number of parameters the registered load function expects
                // and don't bother loading the parameters it wouldn't receive anyway
                int numParamsExpected = (L->top - 1)->value.gc->cl.l.p->numparams;
                if (numParamsExpected) numParamsExpected--; // minus one for the savestate number we always pass in

                int prevGarbage = lua_gc(L, LUA_GCCOUNT, 0);

                lua_pushinteger(L, savestateNumber);
                saveData.LoadRecord(uid, info.dataLoadKey, numParamsExpected);
                int n = lua_gettop(L) - 1;

                int errorcode = lua_pcall(L, n, 0, 0);
                info.running = wasRunning;
                RefreshScriptSpeedStatus();
                if (errorcode)
                    HandleCallbackError(L, info, uid, true);
                else
                {
                    int newGarbage = lua_gc(L, LUA_GCCOUNT, 0);
                    if (newGarbage - prevGarbage > 50)
                    {
                        // now seems to be a very good time to run the garbage collector
                        // it might take a while now but that's better than taking 10 whiles 9 loads from now
                        lua_gc(L, LUA_GCCOLLECT, 0);
                    }
                }
            }
            else
            {
                lua_pop(L, 1);
            }
        }

        ++iter;
    }
}

static const unsigned char* s_dbg_dataStart = NULL;
static int s_dbg_dataSize = 0;

// can't remember what the best way of doing this is...
#if defined(i386) || defined(__i386) || defined(__i386__) || defined(M_I86) || defined(_M_IX86) || defined(_WIN32)
#define IS_LITTLE_ENDIAN
#endif

// push a value's bytes onto the output stack
template<typename T>
void PushBinaryItem(T item, std::vector<unsigned char>& output)
{
    unsigned char* buf = (unsigned char*)&item;
#ifdef IS_LITTLE_ENDIAN
    for (int i = sizeof(T); i; i--)
        output.push_back(*buf++);
#else
    int vecsize = output.size();
    for (int i = sizeof(T); i; i--)
        output.insert(output.begin() + vecsize, *buf++);
#endif
}
// read a value from the byte stream and advance the stream by its size
template<typename T>
T AdvanceByteStream(const unsigned char*& data, unsigned int& remaining)
{
#ifdef IS_LITTLE_ENDIAN
    T rv = *(T*)data;
    data += sizeof(T);
#else
    T rv; unsigned char* rvptr = (unsigned char*)&rv;
    for (int i = sizeof(T) - 1; i >= 0; i--)
        rvptr[i] = *data++;
#endif
    remaining -= sizeof(T);
    return rv;
}
// advance the byte stream by a certain size without reading a value
void AdvanceByteStream(const unsigned char*& data, unsigned int& remaining, int amount)
{
    data += amount;
    remaining -= amount;
}

#define LUAEXT_TLONG		30 // 0x1E // 4-byte signed integer
#define LUAEXT_TUSHORT		31 // 0x1F // 2-byte unsigned integer
#define LUAEXT_TSHORT		32 // 0x20 // 2-byte signed integer
#define LUAEXT_TBYTE		33 // 0x21 // 1-byte unsigned integer
#define LUAEXT_TNILS		34 // 0x22 // multiple nils represented by a 4-byte integer (warning: becomes multiple stack entities)
#define LUAEXT_TTABLE		0x40 // 0x40 through 0x4F // tables of different sizes:
#define LUAEXT_BITS_1A		0x01 // size of array part fits in a 1-byte unsigned integer
#define LUAEXT_BITS_2A		0x02 // size of array part fits in a 2-byte unsigned integer
#define LUAEXT_BITS_4A		0x03 // size of array part fits in a 4-byte unsigned integer
#define LUAEXT_BITS_1H		0x04 // size of hash part fits in a 1-byte unsigned integer
#define LUAEXT_BITS_2H		0x08 // size of hash part fits in a 2-byte unsigned integer
#define LUAEXT_BITS_4H		0x0C // size of hash part fits in a 4-byte unsigned integer
#define BITMATCH(x,y) (((x) & (y)) == (y))

static void PushNils(std::vector<unsigned char>& output, int& nilcount)
{
    int count = nilcount;
    nilcount = 0;

    static const int minNilsWorthEncoding = 6; // because a LUAEXT_TNILS entry is 5 bytes

    if (count < minNilsWorthEncoding)
    {
        for (int i = 0; i < count; i++)
            output.push_back(LUA_TNIL);
    }
    else
    {
        output.push_back(LUAEXT_TNILS);
        PushBinaryItem<UINT32>(count, output);
    }
}

static void LuaStackToBinaryConverter(lua_State* L, int i, std::vector<unsigned char>& output)
{
    int type = lua_type(L, i);

    // the first byte of every serialized item says what Lua type it is
    output.push_back(type & 0xFF);

    switch (type)
    {
    default:
    {
        //printf("wrote unknown type %d (0x%x)\n", type, type);
        //assert(0);

        LuaContextInfo& info = GetCurrentInfo();
        if (info.print)
        {
            char errmsg[1024];
            sprintf(errmsg, "values of type \"%s\" are not allowed to be returned from registered save functions.\r\n", luaL_typename(L, i));
            info.print(luaStateToUIDMap[L], errmsg);
        }
        else
        {
            fprintf(stderr, "values of type \"%s\" are not allowed to be returned from registered save functions.\n", luaL_typename(L, i));
        }
    }
    break;
    case LUA_TNIL:
        // no information necessary beyond the type
        break;
    case LUA_TBOOLEAN:
        // serialize as 0 or 1
        output.push_back(lua_toboolean(L, i));
        break;
    case LUA_TSTRING:
        // serialize as a 0-terminated string of characters
    {
        const char* str = lua_tostring(L, i);
        while (*str)
            output.push_back(*str++);
        output.push_back('\0');
    }
    break;
    case LUA_TNUMBER:
    {
        double num = (double)lua_tonumber(L, i);
        INT32 inum = (INT32)lua_tointeger(L, i);
        if (num != inum)
        {
            PushBinaryItem(num, output);
        }
        else
        {
            if ((inum & ~0xFF) == 0)
                type = LUAEXT_TBYTE;
            else if ((UINT16)(inum & 0xFFFF) == inum)
                type = LUAEXT_TUSHORT;
            else if ((INT16)(inum & 0xFFFF) == inum)
                type = LUAEXT_TSHORT;
            else
                type = LUAEXT_TLONG;
            output.back() = type;
            switch (type)
            {
            case LUAEXT_TLONG:
                PushBinaryItem<INT32>(inum, output);
                break;
            case LUAEXT_TUSHORT:
                PushBinaryItem<UINT16>(inum, output);
                break;
            case LUAEXT_TSHORT:
                PushBinaryItem<INT16>(inum, output);
                break;
            case LUAEXT_TBYTE:
                output.push_back(inum);
                break;
            }
        }
    }
    break;
    case LUA_TTABLE:
        // serialize as a type that describes how many bytes are used for storing the counts,
        // followed by the number of array entries if any, then the number of hash entries if any,
        // then a Lua value per array entry, then a (key,value) pair of Lua values per hashed entry
        // note that the structure of table references are not faithfully serialized (yet)
    {
        int outputTypeIndex = output.size() - 1;
        int arraySize = 0;
        int hashSize = 0;

        if (lua_checkstack(L, 4) && std::find(s_tableAddressStack.begin(), s_tableAddressStack.end(), lua_topointer(L, i)) == s_tableAddressStack.end())
        {
            s_tableAddressStack.push_back(lua_topointer(L, i));
            struct Scope { ~Scope() { s_tableAddressStack.pop_back(); } } scope;

            bool wasnil = false;
            int nilcount = 0;
            arraySize = lua_objlen(L, i);
            int arrayValIndex = lua_gettop(L) + 1;
            for (int j = 1; j <= arraySize; j++)
            {
                lua_rawgeti(L, i, j);
                bool isnil = lua_isnil(L, arrayValIndex);
                if (isnil)
                    nilcount++;
                else
                {
                    if (wasnil)
                        PushNils(output, nilcount);
                    LuaStackToBinaryConverter(L, arrayValIndex, output);
                }
                lua_pop(L, 1);
                wasnil = isnil;
            }
            if (wasnil)
                PushNils(output, nilcount);

            if (arraySize)
                lua_pushinteger(L, arraySize); // before first key
            else
                lua_pushnil(L); // before first key

            int keyIndex = lua_gettop(L);
            int valueIndex = keyIndex + 1;
            while (lua_next(L, i))
            {
                assert(lua_type(L, keyIndex) && "nil key in Lua table, impossible");
                assert(lua_type(L, valueIndex) && "nil value in Lua table, impossible");
                LuaStackToBinaryConverter(L, keyIndex, output);
                LuaStackToBinaryConverter(L, valueIndex, output);
                lua_pop(L, 1);
                hashSize++;
            }
        }

        int outputType = LUAEXT_TTABLE;
        if (arraySize & 0xFFFF0000)
            outputType |= LUAEXT_BITS_4A;
        else if (arraySize & 0xFF00)
            outputType |= LUAEXT_BITS_2A;
        else if (arraySize & 0xFF)
            outputType |= LUAEXT_BITS_1A;
        if (hashSize & 0xFFFF0000)
            outputType |= LUAEXT_BITS_4H;
        else if (hashSize & 0xFF00)
            outputType |= LUAEXT_BITS_2H;
        else if (hashSize & 0xFF)
            outputType |= LUAEXT_BITS_1H;
        output[outputTypeIndex] = outputType;

        int insertIndex = outputTypeIndex;
        if (BITMATCH(outputType, LUAEXT_BITS_4A) || BITMATCH(outputType, LUAEXT_BITS_2A) || BITMATCH(outputType, LUAEXT_BITS_1A))
            output.insert(output.begin() + (++insertIndex), arraySize & 0xFF);
        if (BITMATCH(outputType, LUAEXT_BITS_4A) || BITMATCH(outputType, LUAEXT_BITS_2A))
            output.insert(output.begin() + (++insertIndex), (arraySize & 0xFF00) >> 8);
        if (BITMATCH(outputType, LUAEXT_BITS_4A))
            output.insert(output.begin() + (++insertIndex), (arraySize & 0x00FF0000) >> 16),
            output.insert(output.begin() + (++insertIndex), (arraySize & 0xFF000000) >> 24);
        if (BITMATCH(outputType, LUAEXT_BITS_4H) || BITMATCH(outputType, LUAEXT_BITS_2H) || BITMATCH(outputType, LUAEXT_BITS_1H))
            output.insert(output.begin() + (++insertIndex), hashSize & 0xFF);
        if (BITMATCH(outputType, LUAEXT_BITS_4H) || BITMATCH(outputType, LUAEXT_BITS_2H))
            output.insert(output.begin() + (++insertIndex), (hashSize & 0xFF00) >> 8);
        if (BITMATCH(outputType, LUAEXT_BITS_4H))
            output.insert(output.begin() + (++insertIndex), (hashSize & 0x00FF0000) >> 16),
            output.insert(output.begin() + (++insertIndex), (hashSize & 0xFF000000) >> 24);
    }	break;
    }
}

// complements LuaStackToBinaryConverter
void BinaryToLuaStackConverter(lua_State* L, const unsigned char*& data, unsigned int& remaining)
{
    assert(s_dbg_dataSize - (data - s_dbg_dataStart) == remaining);

    unsigned char type = AdvanceByteStream<unsigned char>(data, remaining);

    switch (type)
    {
    default:
    {
        //printf("read unknown type %d (0x%x)\n", type, type);
        //assert(0);

        LuaContextInfo& info = GetCurrentInfo();
        if (info.print)
        {
            char errmsg[1024];
            if (type <= 10 && type != LUA_TTABLE)
                sprintf(errmsg, "values of type \"%s\" are not allowed to be loaded into registered load functions. The save state's Lua save data file might be corrupted.\r\n", lua_typename(L, type));
            else
                sprintf(errmsg, "The save state's Lua save data file seems to be corrupted.\r\n");
            info.print(luaStateToUIDMap[L], errmsg);
        }
        else
        {
            if (type <= 10 && type != LUA_TTABLE)
                fprintf(stderr, "values of type \"%s\" are not allowed to be loaded into registered load functions. The save state's Lua save data file might be corrupted.\n", lua_typename(L, type));
            else
                fprintf(stderr, "The save state's Lua save data file seems to be corrupted.\n");
        }
    }
    break;
    case LUA_TNIL:
        lua_pushnil(L);
        break;
    case LUA_TBOOLEAN:
        lua_pushboolean(L, AdvanceByteStream<UINT8>(data, remaining));
        break;
    case LUA_TSTRING:
        lua_pushstring(L, (const char*)data);
        AdvanceByteStream(data, remaining, strlen((const char*)data) + 1);
        break;
    case LUA_TNUMBER:
        lua_pushnumber(L, AdvanceByteStream<double>(data, remaining));
        break;
    case LUAEXT_TLONG:
        lua_pushinteger(L, AdvanceByteStream<INT32>(data, remaining));
        break;
    case LUAEXT_TUSHORT:
        lua_pushinteger(L, AdvanceByteStream<UINT16>(data, remaining));
        break;
    case LUAEXT_TSHORT:
        lua_pushinteger(L, AdvanceByteStream<INT16>(data, remaining));
        break;
    case LUAEXT_TBYTE:
        lua_pushinteger(L, AdvanceByteStream<UINT8>(data, remaining));
        break;
    case LUAEXT_TTABLE:
    case LUAEXT_TTABLE | LUAEXT_BITS_1A:
    case LUAEXT_TTABLE | LUAEXT_BITS_2A:
    case LUAEXT_TTABLE | LUAEXT_BITS_4A:
    case LUAEXT_TTABLE | LUAEXT_BITS_1H:
    case LUAEXT_TTABLE | LUAEXT_BITS_2H:
    case LUAEXT_TTABLE | LUAEXT_BITS_4H:
    case LUAEXT_TTABLE | LUAEXT_BITS_1A | LUAEXT_BITS_1H:
    case LUAEXT_TTABLE | LUAEXT_BITS_2A | LUAEXT_BITS_1H:
    case LUAEXT_TTABLE | LUAEXT_BITS_4A | LUAEXT_BITS_1H:
    case LUAEXT_TTABLE | LUAEXT_BITS_1A | LUAEXT_BITS_2H:
    case LUAEXT_TTABLE | LUAEXT_BITS_2A | LUAEXT_BITS_2H:
    case LUAEXT_TTABLE | LUAEXT_BITS_4A | LUAEXT_BITS_2H:
    case LUAEXT_TTABLE | LUAEXT_BITS_1A | LUAEXT_BITS_4H:
    case LUAEXT_TTABLE | LUAEXT_BITS_2A | LUAEXT_BITS_4H:
    case LUAEXT_TTABLE | LUAEXT_BITS_4A | LUAEXT_BITS_4H:
    {
        unsigned int arraySize = 0;
        if (BITMATCH(type, LUAEXT_BITS_4A) || BITMATCH(type, LUAEXT_BITS_2A) || BITMATCH(type, LUAEXT_BITS_1A))
            arraySize |= AdvanceByteStream<UINT8>(data, remaining);
        if (BITMATCH(type, LUAEXT_BITS_4A) || BITMATCH(type, LUAEXT_BITS_2A))
            arraySize |= ((UINT16)AdvanceByteStream<UINT8>(data, remaining)) << 8;
        if (BITMATCH(type, LUAEXT_BITS_4A))
            arraySize |= ((UINT32)AdvanceByteStream<UINT8>(data, remaining)) << 16,
            arraySize |= ((UINT32)AdvanceByteStream<UINT8>(data, remaining)) << 24;

        unsigned int hashSize = 0;
        if (BITMATCH(type, LUAEXT_BITS_4H) || BITMATCH(type, LUAEXT_BITS_2H) || BITMATCH(type, LUAEXT_BITS_1H))
            hashSize |= AdvanceByteStream<UINT8>(data, remaining);
        if (BITMATCH(type, LUAEXT_BITS_4H) || BITMATCH(type, LUAEXT_BITS_2H))
            hashSize |= ((UINT16)AdvanceByteStream<UINT8>(data, remaining)) << 8;
        if (BITMATCH(type, LUAEXT_BITS_4H))
            hashSize |= ((UINT32)AdvanceByteStream<UINT8>(data, remaining)) << 16,
            hashSize |= ((UINT32)AdvanceByteStream<UINT8>(data, remaining)) << 24;

        lua_createtable(L, arraySize, hashSize);

        unsigned int n = 1;
        while (n <= arraySize)
        {
            if (*data == LUAEXT_TNILS)
            {
                AdvanceByteStream(data, remaining, 1);
                n += AdvanceByteStream<UINT32>(data, remaining);
            }
            else
            {
                BinaryToLuaStackConverter(L, data, remaining); // push value
                lua_rawseti(L, -2, n); // table[n] = value
                n++;
            }
        }

        for (unsigned int h = 1; h <= hashSize; h++)
        {
            BinaryToLuaStackConverter(L, data, remaining); // push key
            BinaryToLuaStackConverter(L, data, remaining); // push value
            lua_rawset(L, -3); // table[key] = value
        }
    }
    break;
    }
}

static const unsigned char luaBinaryMajorVersion = 9;
static const unsigned char luaBinaryMinorVersion = 1;

unsigned char* LuaStackToBinary(lua_State* L, unsigned int& size)
{
    int n = lua_gettop(L);
    if (n == 0)
        return NULL;

    std::vector<unsigned char> output;
    output.push_back(luaBinaryMajorVersion);
    output.push_back(luaBinaryMinorVersion);

    for (int i = 1; i <= n; i++)
        LuaStackToBinaryConverter(L, i, output);

    unsigned char* rv = new unsigned char[output.size()];
    memcpy(rv, &output.front(), output.size());
    size = output.size();
    return rv;
}

void BinaryToLuaStack(lua_State* L, const unsigned char* data, unsigned int size, unsigned int itemsToLoad)
{
    unsigned char major = *data++;
    unsigned char minor = *data++;
    size -= 2;
    if (luaBinaryMajorVersion != major || luaBinaryMinorVersion != minor)
        return;

    while (size > 0 && itemsToLoad > 0)
    {
        BinaryToLuaStackConverter(L, data, size);
        itemsToLoad--;
    }
}

// saves Lua stack into a record and pops it
void LuaSaveData::SaveRecord(int uid, unsigned int key)
{
    LuaContextInfo& info = *luaContextInfo[uid];
    lua_State* L = info.L;
    if (!L)
        return;

    Record* cur = new Record();
    cur->key = key;
    cur->data = LuaStackToBinary(L, cur->size);
    cur->next = NULL;

    lua_settop(L, 0);

    if (cur->size <= 0)
    {
        delete cur;
        return;
    }

    Record* last = recordList;
    while (last && last->next)
        last = last->next;
    if (last)
        last->next = cur;
    else
        recordList = cur;
}

// pushes a record's data onto the Lua stack
void LuaSaveData::LoadRecord(int uid, unsigned int key, unsigned int itemsToLoad) const
{
    LuaContextInfo& info = *luaContextInfo[uid];
    lua_State* L = info.L;
    if (!L)
        return;

    Record* cur = recordList;
    while (cur)
    {
        if (cur->key == key)
        {
            s_dbg_dataStart = cur->data;
            s_dbg_dataSize = cur->size;
            BinaryToLuaStack(L, cur->data, cur->size, itemsToLoad);
            return;
        }
        cur = cur->next;
    }
}

// saves part of the Lua stack (at the given index) into a record and does NOT pop anything
void LuaSaveData::SaveRecordPartial(int uid, unsigned int key, int idx)
{
    LuaContextInfo& info = *luaContextInfo[uid];
    lua_State* L = info.L;
    if (!L)
        return;

    if (idx < 0)
        idx += lua_gettop(L) + 1;

    Record* cur = new Record();
    cur->key = key;
    cur->next = NULL;

    if (idx <= lua_gettop(L))
    {
        std::vector<unsigned char> output;
        output.push_back(luaBinaryMajorVersion);
        output.push_back(luaBinaryMinorVersion);

        LuaStackToBinaryConverter(L, idx, output);

        unsigned char* rv = new unsigned char[output.size()];
        memcpy(rv, &output.front(), output.size());
        cur->size = output.size();
        cur->data = rv;
    }

    if (cur->size <= 0)
    {
        delete cur;
        return;
    }

    Record* last = recordList;
    while (last && last->next)
        last = last->next;
    if (last)
        last->next = cur;
    else
        recordList = cur;
}

void fwriteint(unsigned int value, FILE* file)
{
    for (int i = 0; i < 4; i++)
    {
        int w = value & 0xFF;
        fwrite(&w, 1, 1, file);
        value >>= 8;
    }
}
void freadint(unsigned int& value, FILE* file)
{
    int rv = 0;
    for (int i = 0; i < 4; i++)
    {
        int r = 0;
        fread(&r, 1, 1, file);
        rv |= r << (i * 8);
    }
    value = rv;
}

// writes all records to an already-open file
void LuaSaveData::ExportRecords(void* fileV) const
{
    FILE* file = (FILE*)fileV;
    if (!file)
        return;

    Record* cur = recordList;
    while (cur)
    {
        fwriteint(cur->key, file);
        fwriteint(cur->size, file);
        fwrite(cur->data, cur->size, 1, file);
        cur = cur->next;
    }
}

// reads records from an already-open file
void LuaSaveData::ImportRecords(void* fileV)
{
    FILE* file = (FILE*)fileV;
    if (!file)
        return;

    ClearRecords();

    Record rec;
    Record* cur = &rec;
    Record* last = NULL;
    while (1)
    {
        freadint(cur->key, file);
        freadint(cur->size, file);

        if (feof(file) || ferror(file))
            break;

        cur->data = new unsigned char[cur->size];
        fread(cur->data, cur->size, 1, file);

        Record* next = new Record();
        memcpy(next, cur, sizeof(Record));
        next->next = NULL;

        if (last)
            last->next = next;
        else
            recordList = next;
        last = next;
    }
}

void LuaSaveData::ClearRecords()
{
    Record* cur = recordList;
    while (cur)
    {
        Record* del = cur;
        cur = cur->next;

        delete[] del->data;
        delete del;
    }

    recordList = NULL;
}

void DontWorryLua() // everything's going to be OK
{
    std::map<int, LuaContextInfo*>::const_iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::const_iterator end = luaContextInfo.end();
    while (iter != end)
    {
        dontworry(*iter->second);
        ++iter;
    }
}

void EnableStopAllLuaScripts(bool enable)
{
    g_stopAllScriptsEnabled = enable;
}

void StopAllLuaScripts()
{
    if (!g_stopAllScriptsEnabled)
        return;

    std::map<int, LuaContextInfo*>::const_iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::const_iterator end = luaContextInfo.end();
    while (iter != end)
    {
        int uid = iter->first;
        LuaContextInfo& info = *iter->second;
        bool wasStarted = info.started;
        StopLuaScript(uid);
        info.restartLater = wasStarted;
        ++iter;
    }
}

void RestartAllLuaScripts()
{
    if (!g_stopAllScriptsEnabled)
        return;

    std::map<int, LuaContextInfo*>::const_iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::const_iterator end = luaContextInfo.end();
    while (iter != end)
    {
        int uid = iter->first;
        LuaContextInfo& info = *iter->second;
        if (info.restartLater || info.started)
        {
            info.restartLater = false;
            RunLuaScriptFile(uid, info.lastFilename.c_str());
        }
        ++iter;
    }
}

// sets anything that needs to depend on the total number of scripts running
void RefreshScriptStartedStatus()
{
    int numScriptsStarted = 0;

    std::map<int, LuaContextInfo*>::const_iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::const_iterator end = luaContextInfo.end();
    while (iter != end)
    {
        LuaContextInfo& info = *iter->second;
        if (info.started)
            numScriptsStarted++;
        ++iter;
    }

    frameadvSkipLagForceDisable = (numScriptsStarted != 0); // disable while scripts are running because currently lag skipping makes lua callbacks get called twice per frame advance
    g_numScriptsStarted = numScriptsStarted;
}

// sets anything that needs to depend on speed mode or running status of scripts
void RefreshScriptSpeedStatus()
{
    g_anyScriptsHighSpeed = false;

    std::map<int, LuaContextInfo*>::const_iterator iter = luaContextInfo.begin();
    std::map<int, LuaContextInfo*>::const_iterator end = luaContextInfo.end();
    while (iter != end)
    {
        LuaContextInfo& info = *iter->second;
        if (info.running)
            if (info.speedMode == SPEEDMODE_TURBO || info.speedMode == SPEEDMODE_MAXIMUM)
                g_anyScriptsHighSpeed = true;
        ++iter;
    }
}