#include "star_68k.h"

#ifndef _CPU_68K_H
#define _CPU_68K_H

#ifdef __cplusplus
extern "C" {
#endif

    extern struct S68000CONTEXT Context_68K;

    int M68K_Init();
    void M68K_Reset(int System_ID, char Hard);
    void M68K_Reset_CPU();

#ifdef __cplusplus
};
#endif

#endif
