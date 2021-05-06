#ifndef _CPU_Z80_H
#define _CPU_Z80_H

#ifdef __cplusplus
extern "C" {
#endif

    int Z80_Init(void);
    void Z80_Reset(void);

#ifdef __cplusplus
};
#endif

#include <map>

typedef struct bank_min_t {
  unsigned short bank_min;
  unsigned short bank_max;
} bank_min_t;

extern std::map<int, bank_min_t> z80_banks;

#endif
