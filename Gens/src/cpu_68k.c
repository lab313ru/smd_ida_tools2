#include <stdio.h>
#include <windows.h>
#include "cpu_68k.h"
#include "mem_m68k.h"
#include "save.h"
#include "ym2612.h"
#include "misc.h"

#include "joypads.h"

#define GENESIS 0

/*** global variables ***/

struct S68000CONTEXT Context_68K;

struct STARSCREAM_PROGRAMREGION M68K_Fetch[] =
{
    { 0x000000, 0x3FFFFF, (unsigned)0x000000 },
    { 0xFF0000, 0xFFFFFF, (unsigned)&Ram_68k[0] - 0xFF0000 },
    { 0xF00000, 0xF0FFFF, (unsigned)&Ram_68k[0] - 0xF00000 },
    { 0xEF0000, 0xEFFFFF, (unsigned)&Ram_68k[0] - 0xEF0000 },
    { -1, -1, (unsigned)NULL },
    { -1, -1, (unsigned)NULL },
    { -1, -1, (unsigned)NULL }
};

struct STARSCREAM_DATAREGION M68K_Read_Byte[5] =
{
    { 0x000000, 0x3FFFFF, NULL, NULL },
    { 0xFF0000, 0xFFFFFF, NULL, &Ram_68k[0] },
    { 0x400000, 0xFEFFFF, (void *)M68K_RB, NULL },
    { -1, -1, NULL, NULL }
};

struct STARSCREAM_DATAREGION M68K_Read_Word[5] =
{
    { 0x000000, 0x3FFFFF, NULL, NULL },
    { 0xFF0000, 0xFFFFFF, NULL, &Ram_68k[0] },
    { 0x400000, 0xFEFFFF, (void *)M68K_RW, NULL },
    { -1, -1, NULL, NULL }
};

struct STARSCREAM_DATAREGION M68K_Write_Byte[] =
{
    { 0xFF0000, 0xFFFFFF, NULL, &Ram_68k[0] },
    { 0x000000, 0xFEFFFF, (void *)M68K_WB, NULL },
    { -1, -1, NULL, NULL }
};

struct STARSCREAM_DATAREGION M68K_Write_Word[] =
{
    { 0xFF0000, 0xFFFFFF, NULL, &Ram_68k[0] },
    { 0x000000, 0xFEFFFF, (void *)M68K_WW, NULL },
    { -1, -1, NULL, NULL }
};

void M68K_Reset_Handler()
{
    //	Init_Memory_M68K(GENESIS);
}

void S68K_Reset_Handler()
{
    //	Init_Memory_M68K(SEGACD);
}

/*** M68K_Init - initialise the main 68K ***/

int M68K_Init(void)
{
    memset(&Context_68K, 0, sizeof(Context_68K));

    Context_68K.s_fetch = Context_68K.u_fetch = Context_68K.fetch = M68K_Fetch;
    Context_68K.s_readbyte = Context_68K.u_readbyte = Context_68K.readbyte = M68K_Read_Byte;
    Context_68K.s_readword = Context_68K.u_readword = Context_68K.readword = M68K_Read_Word;
    Context_68K.s_writebyte = Context_68K.u_writebyte = Context_68K.writebyte = M68K_Write_Byte;
    Context_68K.s_writeword = Context_68K.u_writeword = Context_68K.writeword = M68K_Write_Word;
    Context_68K.resethandler = (void *)M68K_Reset_Handler;

    main68k_SetContext(&Context_68K);
    main68k_init();

    return 1;
}

/*** M68K_Reset - general reset of the main 68K CPU ***/

void M68K_Reset(int System_ID, char Hard)
{
    if (Hard)
    {
        memset(Ram_68k, 0, 64 * 1024);

        M68K_Fetch[0].lowaddr = 0x000000;
        M68K_Fetch[0].highaddr = Rom_Size - 1;
        M68K_Fetch[0].offset = (unsigned)&Rom_Data[0] - 0x000000;

        M68K_Fetch[1].lowaddr = 0xFF0000;
        M68K_Fetch[1].highaddr = 0xFFFFFF;
        M68K_Fetch[1].offset = (unsigned)&Ram_68k[0] - 0xFF0000;

        M68K_Fetch[2].lowaddr = 0xF00000;
        M68K_Fetch[2].highaddr = 0xF0FFFF;
        M68K_Fetch[2].offset = (unsigned)&Ram_68k[0] - 0xF00000;

        M68K_Fetch[3].lowaddr = 0xEF0000;
        M68K_Fetch[3].highaddr = 0xEFFFFF;
        M68K_Fetch[3].offset = (unsigned)&Ram_68k[0] - 0xEF0000;

        M68K_Fetch[4].lowaddr = -1;
        M68K_Fetch[4].highaddr = -1;
        M68K_Fetch[4].offset = (unsigned)NULL;
    }
    main68k_reset();

    Init_Memory_M68K(System_ID);
}

/*** M68K_Reset_CPU - just reset the main 68K cpu ***/

void M68K_Reset_CPU()
{
    main68k_reset();
}
