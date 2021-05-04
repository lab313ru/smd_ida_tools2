#include <stdio.h>
#include <windows.h>
#include <map>

extern "C" {
#include "z80.h"
#include "cpu_z80.h"
#include "mem_m68k.h"
#include "mem_z80.h"
}

static int bank_counter = 0;
static int m68k_bank_base = 0;

typedef struct bank_min_t {
  unsigned short bank_min;
  unsigned short bank_max;
} bank_min_t;

static std::map<int, bank_min_t> banked;

static void __fastcall Z80_WriteB_Bank_DebugCallback(unsigned int Adr, unsigned char Data)
{
  Z80_WriteB_Bank(Adr, Data);

  if (Adr == 0x6000) {
    bank_counter++;

    if (bank_counter == 9) {
      //printf("Bank = %.8X\n", Bank_Z80);
      m68k_bank_base = Bank_Z80;

      banked[m68k_bank_base] = { 0xFFFF, 0x8000 };
    }

    if (bank_counter == 9) {
      bank_counter = 0;
    }
  }
}

static unsigned char __fastcall Z80_ReadB_68K_Ram_DebugCallback(unsigned int Adr)
{
  unsigned char res = Z80_ReadB_68K_Ram(Adr);

  if (Adr < banked[m68k_bank_base].bank_min) {
    banked[m68k_bank_base].bank_min = Adr;
  }

  if (Adr > banked[m68k_bank_base].bank_max) {
    banked[m68k_bank_base].bank_max = Adr;
  }

  return res;
}

static unsigned short __fastcall Z80_ReadW_68K_Ram_DebugCallback(unsigned int Adr)
{
  unsigned short res = Z80_ReadW_68K_Ram(Adr);

  if (Adr < banked[m68k_bank_base].bank_min) {
    banked[m68k_bank_base].bank_min = Adr;
  }

  if (Adr > banked[m68k_bank_base].bank_max) {
    banked[m68k_bank_base].bank_max = Adr;
  }

  return res;
}

/*** Z80_Reset - reset z80 sub-unit ***/

extern "C" void Z80_Reset(void)
{
    // Reset Z80

    memset(Ram_Z80, 0, 8 * 1024);

    //	Bank_Z80 = 0x000000;
    Bank_Z80 = 0xFF8000;

    Z80_State &= 1;
    //	Z80_State |= 2;		// RZWL needs it but Breath of Fire 3 don't want it...

    Last_BUS_REQ_Cnt = 0;
    Last_BUS_REQ_St = 0;

    z80_Reset(&M_Z80);
}

/*** Z80_Init - initialise this sub-unit ***/

extern "C" int Z80_Init(void)
{
    // Init Z80

    z80_Init(&M_Z80);

    z80_Add_Fetch(&M_Z80, 0x00, 0x1F, &Ram_Z80[0]);
    z80_Add_Fetch(&M_Z80, 0x20, 0x3F, &Ram_Z80[0]);

    z80_Add_ReadB(&M_Z80, 0x00, 0xFF, Z80_ReadB_Bad);
    z80_Add_ReadB(&M_Z80, 0x00, 0x3F, Z80_ReadB_Ram);
    z80_Add_ReadB(&M_Z80, 0x40, 0x5F, Z80_ReadB_YM2612);
    z80_Add_ReadB(&M_Z80, 0x60, 0x6F, Z80_ReadB_Bank);
    z80_Add_ReadB(&M_Z80, 0x70, 0x7F, Z80_ReadB_PSG);
    z80_Add_ReadB(&M_Z80, 0x80, 0xFF, Z80_ReadB_68K_Ram_DebugCallback);

    z80_Add_ReadW(&M_Z80, 0x00, 0xFF, Z80_ReadW_Bad);
    z80_Add_ReadW(&M_Z80, 0x00, 0x3F, Z80_ReadW_Ram);
    z80_Add_ReadW(&M_Z80, 0x40, 0x5F, Z80_ReadW_YM2612);
    z80_Add_ReadW(&M_Z80, 0x60, 0x6F, Z80_ReadW_Bank);
    z80_Add_ReadW(&M_Z80, 0x70, 0x7F, Z80_ReadW_PSG);
    z80_Add_ReadW(&M_Z80, 0x80, 0xFF, Z80_ReadW_68K_Ram_DebugCallback);

    z80_Add_WriteB(&M_Z80, 0x00, 0xFF, Z80_WriteB_Bad);
    z80_Add_WriteB(&M_Z80, 0x00, 0x3F, Z80_WriteB_Ram);
    z80_Add_WriteB(&M_Z80, 0x40, 0x5F, Z80_WriteB_YM2612);
    z80_Add_WriteB(&M_Z80, 0x60, 0x6F, Z80_WriteB_Bank_DebugCallback);
    z80_Add_WriteB(&M_Z80, 0x70, 0x7F, Z80_WriteB_PSG);
    z80_Add_WriteB(&M_Z80, 0x80, 0xFF, Z80_WriteB_68K_Ram);

    z80_Add_WriteW(&M_Z80, 0x00, 0xFF, Z80_WriteW_Bad);
    z80_Add_WriteW(&M_Z80, 0x00, 0x3F, Z80_WriteW_Ram);
    z80_Add_WriteW(&M_Z80, 0x40, 0x5F, Z80_WriteW_YM2612);
    z80_Add_WriteW(&M_Z80, 0x60, 0x6F, Z80_WriteW_Bank);
    z80_Add_WriteW(&M_Z80, 0x70, 0x7F, Z80_WriteW_PSG);
    z80_Add_WriteW(&M_Z80, 0x80, 0xFF, Z80_WriteW_68K_Ram);

    Z80_Reset();

    return 1;
}

extern "C" void Write_To_Bank(int val)
{
#ifdef DEBUG_CD
    fprintf(debug_SCD_file, "Z80 write bank : %d     Bank = %.8X\n", val, Bank_Z80);
#endif
}

extern "C" void Read_To_68K_Space(int adr)
{
#ifdef DEBUG_CD
    fprintf(debug_SCD_file, "Z80 read in 68K space : $(%.8X)\n", adr);
#endif
}

extern "C" void Write_To_68K_Space(int adr, int data)
{
#ifdef DEBUG_CD
    fprintf(debug_SCD_file, "Z80 write in 68K space : $(%.8X) = %.8X\n", adr, data);
#endif
}