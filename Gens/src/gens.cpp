#include <stdio.h>
#include "gens.h"
#include "g_main.h"
#include "g_ddraw.h"
#include "g_dsound.h"
#include "g_input.h"
#include "rom.h"
#include "mem_m68k.h"
#include "ym2612.h"
#include "psg.h"
#include "cpu_68k.h"
#include "cpu_z80.h"
#include "z80.h"
#include "vdp_io.h"
#include "vdp_rend.h"
#include "joypads.h"
#include "misc.h"
#include "save.h"
#include "ggenie.h"
#include "wave.h"
#include "pcm.h"
#include "pwm.h"
#include "movie.h"
#include "ram_search.h"
#include "luascript.h"

// uncomment this to run a simple test every frame for potential desyncs
// you do not need to start any movie for the test to work, just play
// expect it to be pretty slow, though
//#define TEST_GENESIS_FOR_DESYNCS

// uncomment this to test the sega-cd specific parts of the savestates for desyncs
// (you should still use TEST_GENESIS_FOR_DESYNCS to test for desyncs in sega cd games,
//  the only reason to use this instead is to do an extra-finicky check that could fail earlier)
// IMPORTANT: in Visual Studio you'll need to increase the Stack Reserve Size
// to something like 4194304 in your Project Settings > Linker > System tab
// otherwise this will give you a stack overflow error
//#define TEST_SEGACD_FOR_DESYNCS

// uncomment this to test the 32x-specific parts of the savestates for desyncs
//#define TEST_32X_FOR_DESYNCS

// uncomment this in addition to one of the above to allow for
// calling DesyncDetection from elsewhere without forcing it on all frame updates
//#define MANUAL_DESYNC_CHECKS_ONLY

#if defined(TEST_GENESIS_FOR_DESYNCS) || defined(TEST_SEGACD_FOR_DESYNCS) || defined(TEST_32X_FOR_DESYNCS)
#define TEST_FOR_DESYNCS
#include <assert.h>
#include <map>

#ifdef TEST_GENESIS_FOR_DESYNCS
static const int CHECKED_STATE_LENGTH = GENESIS_STATE_LENGTH;
#define Export_Func Export_Genesis
#if defined(TEST_SEGACD_FOR_DESYNCS) || defined(TEST_32X_FOR_DESYNCS)
#error Sorry, only one of TEST_GENESIS_FOR_DESYNCS, TEST_SEGACD_FOR_DESYNCS, TEST_32X_FOR_DESYNCS can be defined at a time
#endif
#elif defined(TEST_SEGACD_FOR_DESYNCS)
static const int CHECKED_STATE_LENGTH = SEGACD_LENGTH_EX;
#define Export_Func Export_SegaCD
#if defined(TEST_32X_FOR_DESYNCS)
#error Sorry, only one of TEST_GENESIS_FOR_DESYNCS, TEST_SEGACD_FOR_DESYNCS, TEST_32X_FOR_DESYNCS can be defined at a time
#endif
#elif defined(TEST_32X_FOR_DESYNCS)
static const int CHECKED_STATE_LENGTH = G32X_LENGTH_EX;
#define Export_Func Export_32X
#endif

struct SaveStateData
{
    unsigned char State_Buffer[CHECKED_STATE_LENGTH];
};
std::map<int, SaveStateData> saveStateDataMap;

int firstFailureByte = -1;
bool CompareSaveStates(SaveStateData& data1, SaveStateData& data2)
{
    SaveStateData difference;
    bool ok = true;
    firstFailureByte = -1;
    static const int maxFailures = 4000; // print at most this many failure at once
    int numFailures = 0;
    for (int i = 0; i < CHECKED_STATE_LENGTH; i++)
    {
        unsigned char diff = data2.State_Buffer[i] - data1.State_Buffer[i];
        difference.State_Buffer[i] = diff;
        if (diff)
        {
            char desc[128];
            sprintf(desc, "byte %d (0x%x) was 0x%x is 0x%x\n", i, i, data1.State_Buffer[i], data2.State_Buffer[i]);
            OutputDebugString(desc);
            if (ok)
            {
                ok = false;
                firstFailureByte = i;
            }
            numFailures++;
            if (numFailures > maxFailures)
                break;
        }
    }
    return ok;
}

static SaveStateData tempData;

void DesyncDetection(bool forceCheckingDesync = false, bool forcePart = false)
{
#ifdef TEST_GENESIS_FOR_DESYNCS
    if (!Game) return;
#endif
#ifdef TEST_SEGACD_FOR_DESYNCS
    if (!SegaCD_Started) return;
#endif
#ifdef TEST_32X_FOR_DESYNCS
    if (!_32X_Started) return;
#endif
    // (only if forceCheckingDesync is false)
    // hold control to save a savestate for frames in a movie for later checking
    // then turn on scroll lock when replaying those frames later to check them
    bool checkingDesync = (GetKeyState(VK_LCONTROL) & 0x8000) != 0;
    int checkingDesyncPart = GetKeyState(VK_SCROLL) ? 1 : 0;
    if (!forceCheckingDesync)
        checkingDesync |= !!checkingDesyncPart;
    else
    {
        checkingDesync = true;
        checkingDesyncPart = forcePart ? 1 : 0;
    }

    if (checkingDesync)
    {
        if (checkingDesyncPart == 0)
        {
            // first part: just save states
            static SaveStateData data;

            //Save_State_To_Buffer(data.State_Buffer);
            memset(data.State_Buffer, 0, sizeof(data.State_Buffer));
            Export_Func(data.State_Buffer);

            saveStateDataMap[FrameCount] = data;
        }
        else
        {
            // second part: compare to saved states
            static SaveStateData dataNow;

            //Save_State_To_Buffer(dataNow.State_Buffer);
            memset(dataNow.State_Buffer, 0, sizeof(dataNow.State_Buffer));
            Export_Func(dataNow.State_Buffer);

            if (saveStateDataMap.find(FrameCount) != saveStateDataMap.end())
            {
                SaveStateData& dataThen = saveStateDataMap[FrameCount];
                if (!CompareSaveStates(dataThen, dataNow))
                {
                    memset(tempData.State_Buffer, tempData.State_Buffer[firstFailureByte] ? 0 : 0xFF, sizeof(tempData.State_Buffer));

                    // if this assert fails, congratulations, you got a desync!
                    assert(0);
                    // check your output window for details.
                    // now you might want to know which part of the savestate went wrong.
                    // to do that, go to the Breakpoints window and add a new Data breakpoint and
                    // set it to break when the data at &tempData.State_Buffer[firstFailureByte] changes,
                    // then continue running and it should break inside the export function where the desyncing data gets saved.

                    Export_Func(tempData.State_Buffer);
                }
            }
        }
    }
}

#ifndef MANUAL_DESYNC_CHECKS_ONLY

int Do_Genesis_Frame_Real();
int Do_Genesis_Frame_No_VDP_Real();
int Do_SegaCD_Frame_Real();
int Do_SegaCD_Frame_No_VDP_Real();
int Do_SegaCD_Frame_Cycle_Accurate_Real();
int Do_SegaCD_Frame_No_VDP_Cycle_Accurate_Real();
int Do_32X_Frame_Real();
int Do_32X_Frame_No_VDP_Real();

#define DO_FRAME_HEADER(name, fastname) \
	int name() \
	{ \
		if(!((TurboMode))) { \
			Save_State_To_Buffer(State_Buffer); /* save */ \
			disableSound = true; \
			fastname##_Real(); /* run 2 frames */ \
			fastname##_Real(); \
			DesyncDetection(1,0); /* save */ \
			Load_State_From_Buffer(State_Buffer); /* load */ \
			fastname##_Real(); /* run 2 frames */ \
			fastname##_Real(); \
			DesyncDetection(1,1); /* check that it's identical to last save ... this is the slow part */ \
			Load_State_From_Buffer(State_Buffer); /* load */ \
			saveStateDataMap.clear(); \
			disableSound = false; \
                		} \
		return name##_Real(); /* run the frame for real */ \
	} \
	int name##_Real()
#else // MANUAL_DESYNC_CHECKS_ONLY:
#define DO_FRAME_HEADER(name, fastname) int name()
#endif

BOOL IsAsyncAllowed(void)
{
    // no asynchronous stuff allowed when testing for desyncs
    return false;
}

#else // !TEST_FOR_DESYNCS:

#define DO_FRAME_HEADER(name, fastname) int name()

BOOL IsAsyncAllowed(void)
{
    // no asynchronous stuff allowed when playng or recording a movie
    if (MainMovie.Status == MOVIE_RECORDING)
        return false;
    if (MainMovie.Status == MOVIE_PLAYING)
        return false;
    return true;
}

#endif // TEST_FOR_DESYNCS

int Frame_Skip;
int Frame_Number;
int Inside_Frame = 0;
int RMax_Level;
int GMax_Level;
int BMax_Level;
int Contrast_Level;
int Brightness_Level;
int Greyscale;
int Invert_Color;
int FakeVDPScreen = true;
int VDP_Reg_Set2_Current;
int VDP_Reg_Set4_Current;

unsigned char CD_Data[1024];		// Used for hard reset to know the game name
int SRAM_Was_On = 0;

int Round_Double(double val)
{
    if ((val - (double)(int)val) > 0.5) return (int)(val + 1);
    else return (int)val;
}

void Recalculate_Palettes(void)
{
    int i;
    int r, g, b;
    int rf, gf, bf;
    int bright, cont;

    for (r = 0; r < 0x10; r++)
    {
        for (g = 0; g < 0x10; g++)
        {
            for (b = 0; b < 0x10; b++)
            {
                rf = (r & 0xF) << 2;
                gf = (g & 0xF) << 2;
                bf = (b & 0xF) << 2;

                rf = (int)((double)(rf)* ((double)(RMax_Level) / 224.0));
                gf = (int)((double)(gf)* ((double)(GMax_Level) / 224.0));
                bf = (int)((double)(bf)* ((double)(BMax_Level) / 224.0));

                // Compute colors here (64 levels)

                bright = Brightness_Level;
                bright -= 100;
                bright *= 32;
                bright /= 100;

                rf += bright;
                gf += bright;
                bf += bright;

                if (rf < 0) rf = 0;
                else if (rf > 0x3F) rf = 0x3F;
                if (gf < 0) gf = 0;
                else if (gf > 0x3F) gf = 0x3F;
                if (bf < 0) bf = 0;
                else if (bf > 0x3F) bf = 0x3F;

                cont = Contrast_Level;

                rf = (rf * cont) / 100;
                gf = (gf * cont) / 100;
                bf = (bf * cont) / 100;

                if (rf < 0) rf = 0;
                else if (rf > 0x3F) rf = 0x3F;
                if (gf < 0) gf = 0;
                else if (gf > 0x3F) gf = 0x3F;
                if (bf < 0) bf = 0;
                else if (bf > 0x3F) bf = 0x3F;

                rf <<= 18;
                gf <<= 10;
                bf <<= 2;

                Palette32[(b << 8) | (g << 4) | r] = 0xFF000000 | rf | gf | bf; // alpha added

                rf >>= 18;
                gf >>= 10;
                bf >>= 2;

                if (Mode_555 & 1)
                {
                    rf = (rf >> 1) << 10;
                    gf = (gf >> 1) << 5;
                }
                else
                {
                    rf = (rf >> 1) << 11;
                    gf = (gf >> 0) << 5;
                }
                bf = (bf >> 1) << 0;

                Palette[(b << 8) | (g << 4) | r] = rf | gf | bf;
            }
        }
    }

    if (Greyscale)
    {
        for (i = 0; i < 0x1000; i++)
        {
            r = (Palette32[i] >> 16) & 0xFF;
            g = (Palette32[i] >> 8) & 0xFF;
            b = Palette32[i] & 0xFF;

            r = (r * (unsigned int)(0.30 * 65536.0)) >> 16;
            g = (g * (unsigned int)(0.59 * 65536.0)) >> 16;
            b = (b * (unsigned int)(0.11 * 65536.0)) >> 16;

            r = g = b = r + g + b;

            r <<= 16;
            g <<= 8;

            Palette32[i] = 0xFF000000 | r | g | b; // alpha added

            if (Mode_555 & 1)
            {
                r = ((Palette[i] >> 10) & 0x1F) << 1;
                g = ((Palette[i] >> 5) & 0x1F) << 1;
            }
            else
            {
                r = ((Palette[i] >> 11) & 0x1F) << 1;
                g = (Palette[i] >> 5) & 0x3F;
            }

            b = ((Palette[i] >> 0) & 0x1F) << 1;

            r = (r * (unsigned int)(0.30 * 65536.0)) >> 16;
            g = (g * (unsigned int)(0.59 * 65536.0)) >> 16;
            b = (b * (unsigned int)(0.11 * 65536.0)) >> 16;

            r = g = b = r + g + b;

            if (Mode_555 & 1)
            {
                r = (r >> 1) << 10;
                g = (g >> 1) << 5;
            }
            else
            {
                r = (r >> 1) << 11;
                g = (g >> 0) << 5;
            }

            b = (b >> 1) << 0;

            Palette[i] = r | g | b;
        }
    }

    if (Invert_Color)
    {
        for (i = 0; i < 0x1000; i++)
        {
            Palette[i] ^= 0xFFFF;
            Palette32[i] ^= 0xFFFFFF;
        }
    }

    for (i = 0; i < 0x1000; i++)
    {
        Palette32[0x1000 | i] = Palette32[i >> 1];  // shadow
        Palette32[0x2000 | i] = Palette32[(i >> 1) + 0x777];	// highlight
        Palette32[0x3000 | i] = Palette32[i];	// normal
        Palette[0x1000 | i] = Palette[i >> 1];		// shadow
        Palette[0x2000 | i] = Palette[(i >> 1) + 0x777];		// highlight
        Palette[0x3000 | i] = Palette[i];		// normal
    }

    // colors for alpha = 1
    for (i = 0; i < 0x4000; i++)
    {
        Palette32[0x4000 | i] = Palette32[i];
        Palette[0x4000 | i] = Palette[i];
    }

    unsigned short pink_555 = (Mode_555 & 1) ? 0x7C1F : 0xF81F;
    if (PinkBG)
        for (i = 0; i < 0x4000; i++)
        {
            Palette32[i] = 0xFF00FF;
            Palette[i] = pink_555;
        }
}

void Check_Country_Order(void)
{
    int i, j;
    bool bad = false;

    for (i = 0; i < 3; ++i)
        for (j = i + 1; j < 3; ++j)
            if (Country_Order[i] == Country_Order[j])
                bad = true;

    for (i = 0; i < 3; ++i)
        if (Country_Order[i] > 2
            || Country_Order[i] < 0)
            bad = true;

    if (bad)
        for (i = 0; i < 3; ++i)
            Country_Order[i] = i;
}

void Detect_Country_Genesis(void)
{
    int c_tab[3] = { 4, 1, 8 };
    int gm_tab[3] = { 1, 0, 1 };
    int cm_tab[3] = { 0, 0, 1 };
    int i, coun = 0;
    char c;

    if (!strnicmp((char *)&Rom_Data[0x1F0], "eur", 3)) coun |= 8;
    else if (!strnicmp((char *)&Rom_Data[0x1F0], "usa", 3)) coun |= 4;
    else if (!strnicmp((char *)&Rom_Data[0x1F0], "jap", 3)) coun |= 1;
    else for (i = 0; i < 4; i++)
    {
        c = toupper(Rom_Data[0x1F0 + i]);

        if (c == 'U') coun |= 4;
        else if (c == 'J') coun |= 1;
        else if (c == 'E') coun |= 8;
        else if (c < 16) coun |= c;
        else if ((c >= '0') && (c <= '9')) coun |= c - '0';
        else if ((c >= 'A') && (c <= 'F')) coun |= c - 'A' + 10;
    }

    if (coun & c_tab[Country_Order[0]])
    {
        Game_Mode = gm_tab[Country_Order[0]];
        CPU_Mode = cm_tab[Country_Order[0]];
    }
    else if (coun & c_tab[Country_Order[1]])
    {
        Game_Mode = gm_tab[Country_Order[1]];
        CPU_Mode = cm_tab[Country_Order[1]];
    }
    else if (coun & c_tab[Country_Order[2]])
    {
        Game_Mode = gm_tab[Country_Order[2]];
        CPU_Mode = cm_tab[Country_Order[2]];
    }
    else if (coun & 2)
    {
        Game_Mode = 0;
        CPU_Mode = 1;
    }
    else
    {
        Game_Mode = 1;
        CPU_Mode = 0;
    }

    if (Game_Mode)
    {
        if (CPU_Mode) Put_Info("Europe system (50 FPS)");
        else Put_Info("USA system (60 FPS)");
    }
    else
    {
        if (CPU_Mode) Put_Info("Japan system (50 FPS)");
        else Put_Info("Japan system (60 FPS)");
    }

    if (CPU_Mode)
    {
        VDP_Status |= 0x0001;
    }
    else
    {
        VDP_Status &= 0xFFFE;
    }
}

// a few things previously uninitialized by one or more systems,
// causing even savestates made on frame 0 to differ.
// the init functions should call this first so that the actual initialization
// can overwrite what this does (in the cases where that system does initialize it).
// the reset functions shouldn't call this at all,
// it's correct that those don't fully reset the emulation state.
static void Misc_Genesis_Init()
{
    //M_Z80.CycleCnt = 0;
    memset(&M_Z80.AF, 0, sizeof(M_Z80.CycleSup) + ((char*)&M_Z80.CycleSup - (char*)&M_Z80.AF));
    M_Z80.RetIC = 0;
    M_Z80.IntAckC = 0;
    Controller_1_Delay = 0;
    Controller_2_Delay = 0;
    Ctrl.DMA_Mode = 0;
    VDP_Reg.DMA_Address = 0;
    VDP_Current_Line = 0;
    Cycles_M68K = 0;
    Cycles_Z80 = 0;
    Bank_M68K = 0;
    Lag_Frame = 0;
    main68k_context.cycles_leftover = 0;
    main68k_context.xflag = 0;
    main68k_context.io_cycle_counter = -1;
    main68k_context.io_fetchbase = 0;
    main68k_context.io_fetchbased_pc = 0;
    main68k_context.access_address = 0;
    main68k_context.odometer = 0;
}

/*************************************/
/*              GENESIS              */
/*************************************/

void Init_Genesis_Bios(void)
{
    Misc_Genesis_Init();

    FILE *f;

    if (f = fopen(Genesis_Bios, "rb"))
    {
        fread(&Genesis_Rom[0], 1, 2 * 1024, f);
        Byte_Swap(&Genesis_Rom[0], 2 * 1024);
        fclose(f);
    }
    else memset(Genesis_Rom, 0, 2 * 1024);

    Rom_Size = 2 * 1024;
    memcpy(Rom_Data, Genesis_Rom, 2 * 1024);
    Game_Mode = 0;
    CPU_Mode = 0;
    VDP_Num_Vis_Lines = 224;
    M68K_Reset(0, 1);
    Z80_Reset();
    Reset_VDP();
    FakeVDPScreen = true;
    CPL_Z80 = Round_Double((((double)CLOCK_NTSC / 15.0) / 60.0) / 262.0);
    CPL_M68K = Round_Double((((double)CLOCK_NTSC / 7.0) / 60.0) / 262.0);
    VDP_Num_Lines = 262;
    VDP_Status &= 0xFFFE;
    YM2612_Init(CLOCK_NTSC / 7, Sound_Rate, 1);
    PSG_Init(CLOCK_NTSC / 15, Sound_Rate);
}

int Init_Genesis(struct Rom *MD_Rom)
{
    Misc_Genesis_Init();

    char Str_Err[256];

    Flag_Clr_Scr = 1;
    Paused = Frame_Number = 0;
    SRAM_Start = SRAM_End = SRAM_ON = SRAM_Write = 0;
    Controller_1_COM = Controller_2_COM = 0;

    if ((MD_Rom->Ram_Infos[8] == 'R') && (MD_Rom->Ram_Infos[9] == 'A') && (MD_Rom->Ram_Infos[10] & 0x40))
    {
        SRAM_Start = MD_Rom->Ram_Start_Address & 0x0F80000;		// multiple de 0x080000
        SRAM_End = MD_Rom->Ram_End_Address;
    }
    else
    {
        SRAM_Start = 0x200000;
        SRAM_End = 0x20FFFF;
    }

    if ((SRAM_Start > SRAM_End) || ((SRAM_End - SRAM_Start) >= (64 * 1024)))
        SRAM_End = SRAM_Start + (64 * 1024) - 1;

    if (Rom_Size <= (2 * 1024 * 1024))
    {
        SRAM_ON = 1;
        SRAM_Write = 1;
    }

    SRAM_Start &= 0xFFFFFFFE;
    SRAM_End |= 0x00000001;

    //		sprintf(Str_Err, "deb = %.8X end = %.8X", SRAM_Start, SRAM_End);
    //		MessageBox(NULL, Str_Err, "", MB_OK);

    if ((SRAM_End - SRAM_Start) <= 2) SRAM_Custom = 1;
    else SRAM_Custom = 0;

    Load_SRAM();

    switch (Country)
    {
    default:
    case -1:
        Detect_Country_Genesis();
        break;

    case 0:
        Game_Mode = 0;
        CPU_Mode = 0;

        break;

    case 1:
        Game_Mode = 1;
        CPU_Mode = 0;
        break;

    case 2:
        Game_Mode = 1;
        CPU_Mode = 1;
        break;

    case 3:
        Game_Mode = 0;
        CPU_Mode = 1;
        break;
    }

    if ((CPU_Mode == 1) || (Game_Mode == 0))
        sprintf(Str_Err, "[%s] " GENS_NAME " - Megadrive : %s", DebugPort, MD_Rom->Rom_Name_W);
    else
        sprintf(Str_Err, "[%s] " GENS_NAME " - Genesis : %s", DebugPort, MD_Rom->Rom_Name_W);

    // Modif N. - remove double-spaces from title bar
    for (int i = 0; i < (int)strlen(Str_Err) - 1; i++)
        if (Str_Err[i] == Str_Err[i + 1] && Str_Err[i] == ' ')
            strcpy(Str_Err + i, Str_Err + i + 1), i--;

    SetWindowText(HWnd, Str_Err);

    VDP_Num_Vis_Lines = 224;
    Gen_Version = 0x20 + 0x0;	 	// Version de la megadrive (0x0 - 0xF)

    Byte_Swap(Rom_Data, Rom_Size);

    M68K_Init(); // Modif N. -- added for symmetry, maybe it helps something
    M68K_Reset(0, 1);
    Z80_Reset();
    Reset_VDP();
    FakeVDPScreen = true;

    if (CPU_Mode)
    {
        CPL_Z80 = Round_Double((((double)CLOCK_PAL / 15.0) / 50.0) / 312.0);
        CPL_M68K = Round_Double((((double)CLOCK_PAL / 7.0) / 50.0) / 312.0);

        VDP_Num_Lines = 312;
        VDP_Status |= 0x0001;

        YM2612_Init(CLOCK_PAL / 7, Sound_Rate, 1);
        PSG_Init(CLOCK_PAL / 15, Sound_Rate);
    }
    else
    {
        CPL_Z80 = Round_Double((((double)CLOCK_NTSC / 15.0) / 60.0) / 262.0);
        CPL_M68K = Round_Double((((double)CLOCK_NTSC / 7.0) / 60.0) / 262.0);

        VDP_Num_Lines = 262;
        VDP_Status &= 0xFFFE;

        YM2612_Init(CLOCK_NTSC / 7, Sound_Rate, 1);
        PSG_Init(CLOCK_NTSC / 15, Sound_Rate);
    }

    if (Auto_Fix_CS) Fix_Checksum();

    End_Sound();

    Init_Sound(HWnd);
    Play_Sound();

    Load_Patch_File();
    Build_Main_Menu();

    Last_Time = GetTickCount();
    New_Time = 0;
    Used_Time = 0;

    Update_Frame = Do_Genesis_Frame;
    Update_Frame_Fast = Do_Genesis_Frame_No_VDP;

    Genesis_Started = 1; // used inside reset_address_info
    reset_address_info();
    RestartAllLuaScripts();

    return 1;
}

void Reset_Genesis()
{
    Controller_1_COM = Controller_2_COM = 0;
    //Paused = 0;

    if (Rom_Size <= (2 * 1024 * 1024))
    {
        SRAM_ON = 1;
        SRAM_Write = 1;
    }
    else
    {
        SRAM_ON = 0;
        SRAM_Write = 0;
    }

    M68K_Reset(0, 1);
    Z80_Reset();
    Reset_VDP();
    FakeVDPScreen = true;
    YM2612_Reset();

    if (CPU_Mode) VDP_Status |= 1;
    else VDP_Status &= ~1;

    if (Auto_Fix_CS) Fix_Checksum();
}

extern "C"
{
    int disableRamSearchUpdate = false;
}

// to make two different compiled functions
template<int bits>
void Render_MD_Screen_()
{
    int Line;
    for (Line = 0; Line < VDP_Num_Vis_Lines; Line++)
    {
        for (unsigned long Pixel = TAB336[Line] + 8; Pixel < TAB336[Line] + 336; Pixel++)
        {
            if (bits == 32)
                MD_Screen32[Pixel] = Palette32[Screen_16X[Pixel]];
            if (bits == 16)
                MD_Screen[Pixel] = Palette[Screen_16X[Pixel]];
        }
    }
    // fixes for filters
    // bottom row
    for (int Pixel = 336 * Line + 8; Pixel < 336 * Line + 336; Pixel++)
    {
        if (bits == 32)
            MD_Screen32[Pixel] = 0;
        if (bits == 16)
            MD_Screen[Pixel] = 0;
    }
    // right row
    if (VDP_REG_SET4 & 0x1)
        for (Line = 0; Line < VDP_Num_Vis_Lines; Line++)
        {
            int Pixel = TAB336[Line] + 8 + 320;
            if (bits == 32)
                MD_Screen32[Pixel] = 0;
            if (bits == 16)
                MD_Screen[Pixel] = 0;
        }
    else
        for (Line = 0; Line < VDP_Num_Vis_Lines; Line++)
        {
            int Pixel = TAB336[Line] + 8 + 256;
            if (bits == 32)
                MD_Screen32[Pixel] = 0;
            if (bits == 16)
                MD_Screen[Pixel] = 0;
        }
}

void Render_MD_Screen()
{
    if (Bits32)
        Render_MD_Screen_<32>();
    else
        Render_MD_Screen_<16>();
}

#ifdef SONICCAMHACK

#else

int Do_Genesis_Frame(bool fast)
{
    struct Scope { Scope() { Inside_Frame = 1; } ~Scope() { Inside_Frame = 0; } } scope;

    int *buf[2];
    int HInt_Counter;

    if ((CPU_Mode) && (VDP_Reg.Set2 & 0x8))	VDP_Num_Vis_Lines = 240;
    else VDP_Num_Vis_Lines = 224;

    Cycles_M68K = Cycles_Z80 = 0;
    Last_BUS_REQ_Cnt = -1000;
    main68k_tripOdometer();
    z80_Clear_Odo(&M_Z80);

    Patch_Codes();

    VRam_Flag = 1;

    VDP_Status &= 0xFFF7;							// Clear V Blank
    if (VDP_Reg.Set4 & 0x2) VDP_Status ^= 0x0010;
    VDP_Reg_Set2_Current = VDP_Reg.Set2;
    VDP_Reg_Set4_Current = VDP_Reg.Set4;

    HInt_Counter = VDP_Reg.H_Int;					// Hint_Counter = step d'interruption H

    if (fast)
        FakeVDPScreen = true;

    for (VDP_Current_Line = 0; VDP_Current_Line < VDP_Num_Vis_Lines; VDP_Current_Line++)
    {
      buf[0] = Seg_L + Sound_Extrapol[VDP_Current_Line][0];
      buf[1] = Seg_R + Sound_Extrapol[VDP_Current_Line][0];
      YM2612_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
      PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (DMAT_Length) main68k_addCycles(Update_DMA());
        VDP_Status |= 0x0004;			// HBlank = 1
        //		main68k_exec(Cycles_M68K - 436);
        main68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xFFFB;			// HBlank = 0

        if (--HInt_Counter < 0)
        {
            HInt_Counter = VDP_Reg.H_Int;
            VDP_Int |= 0x4;
            Update_IRQ_Line();
        }

        if (!fast)
            Render_Line();

        main68k_exec(Cycles_M68K);
        if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
        else z80_Set_Odo(&M_Z80, Cycles_Z80);
    }

    if (!fast)
    {
        FakeVDPScreen = false;
        Render_MD_Screen();
    }

    buf[0] = Seg_L + Sound_Extrapol[VDP_Current_Line][0];
    buf[1] = Seg_R + Sound_Extrapol[VDP_Current_Line][0];
    YM2612_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
    PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

    Fix_Controllers();
    Cycles_M68K += CPL_M68K;
    Cycles_Z80 += CPL_Z80;
    if (DMAT_Length) main68k_addCycles(Update_DMA());
    if (--HInt_Counter < 0)
    {
        VDP_Int |= 0x4;
        Update_IRQ_Line();
    }

    VDP_Status |= 0x000C;			// VBlank = 1 et HBlank = 1 (retour de balayage vertical en cours)
    main68k_exec(Cycles_M68K - 360);
    if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80 - 168);
    else z80_Set_Odo(&M_Z80, Cycles_Z80 - 168);

    VDP_Status &= 0xFFFB;			// HBlank = 0
    VDP_Status |= 0x0080;			// V Int happened

    VDP_Int |= 0x8;
    Update_IRQ_Line();
    z80_Interrupt(&M_Z80, 0xFF);

    main68k_exec(Cycles_M68K);
    if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
    else z80_Set_Odo(&M_Z80, Cycles_Z80);

    for (VDP_Current_Line++; VDP_Current_Line < VDP_Num_Lines; VDP_Current_Line++)
    {
      buf[0] = Seg_L + Sound_Extrapol[VDP_Current_Line][0];
      buf[1] = Seg_R + Sound_Extrapol[VDP_Current_Line][0];
      YM2612_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
      PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (DMAT_Length) main68k_addCycles(Update_DMA());
        VDP_Status |= 0x0004;					// HBlank = 1
        //		main68k_exec(Cycles_M68K - 436);
        main68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xFFFB;					// HBlank = 0

        main68k_exec(Cycles_M68K);
        if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
        else z80_Set_Odo(&M_Z80, Cycles_Z80);
    }

    PSG_Special_Update();
    YM2612_Special_Update();

#ifdef RKABOXHACK
    CamX = CheatRead<short>(0xB158);
    CamY = CheatRead<short>(0xB1D6);
#endif
    if (!fast)
        Update_RAM_Search();
    //	if (SRAM_ON != SRAM_Was_On)
    //	{
    //		SRAM_Was_On = SRAM_ON;
    //		if (SRAM_ON) sprintf(Str_Tmp,"SRAM enabled");
    //		else		 sprintf(Str_Tmp,"SRAM disabled");;
    //		Put_Info(Str_Tmp);
    //	}
    return(1);
}

DO_FRAME_HEADER(Do_Genesis_Frame, Do_Genesis_Frame_No_VDP)
{
    return Do_Genesis_Frame(false);
}

DO_FRAME_HEADER(Do_Genesis_Frame_No_VDP, Do_Genesis_Frame_No_VDP)
{
    return Do_Genesis_Frame(true);
}

#endif

int Do_VDP_Refresh()
{
    int VDP_Current_Line_bak = VDP_Current_Line;

    if ((CPU_Mode) && (VDP_Reg.Set2 & 0x8))	VDP_Num_Vis_Lines = 240;
    else VDP_Num_Vis_Lines = 224;

    if (Genesis_Started)
    {
      if (FakeVDPScreen)
        for (VDP_Current_Line = 0; VDP_Current_Line < VDP_Num_Vis_Lines; VDP_Current_Line++)
          Render_Line();
      Render_MD_Screen();
    }
    else // emulation hasn't started so just set all pixels to black
    {
      memset(MD_Screen, 0, sizeof(MD_Screen));
      memset(MD_Screen32, 0, sizeof(MD_Screen32));
    }

    VDP_Current_Line = VDP_Current_Line_bak;

    Update_RAM_Search();
#ifdef RKABOXHACK
    CamX = CheatRead<short>(0xB158);
    CamY = CheatRead<short>(0xB1D6);
    DrawBoxes();
#endif
#ifdef SONICCAMHACK
    CamX = CheatRead<short>(CAMOFFSET1);
    CamY = CheatRead<short>(CAMOFFSET1 + 4);
    DrawBoxes();
#endif

    CallRegisteredLuaFunctions(LUACALL_AFTEREMULATIONGUI);

    return(0);
}
