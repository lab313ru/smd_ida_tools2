#pragma once

#define RC_GENERAL 1
#ifdef DEBUG_68K
#define RC_VDP 2
#endif

enum register_t
{
#ifdef DEBUG_68K
    R_D0,
    R_D1,
    R_D2,
    R_D3,
    R_D4,
    R_D5,
    R_D6,
    R_D7,

    R_A0,
    R_A0_ADDR,
    R_A1,
    R_A1_ADDR,
    R_A2,
    R_A2_ADDR,
    R_A3,
    R_A3_ADDR,
    R_A4,
    R_A4_ADDR,
    R_A5,
    R_A5_ADDR,
    R_A6,
    R_A6_ADDR,
    R_A7,
    R_A7_ADDR,

    R_PC,
    R_SP,
    R_SR,

    R_VDP_DMA_LEN,
    R_VDP_DMA_SRC,
    R_VDP_WRITE_ADDR,

    R_V00,
    R_V01,
    R_V02,
    R_V03,
    R_V04,
    R_V05,
    R_V06,
    R_V07,
    R_V08,
    R_V09,
    R_V10,
    R_V11,
    R_V12,
    R_V13,
    R_V14,
    R_V15,
    R_V16,
    R_V17,
    R_V18,
    R_V19,
    R_V20,
    R_V21,
    R_V22,
    R_V23,
#else
  R_A,
  R_AF,
  R_AF2,
  R_B,
  R_C,
  R_BC,
  R_BC2,
  R_DE,
  R_DE2,
  R_HL,
  R_HL2,
  R_IXH,
  R_IXL,
  R_IX,
  R_IYH,
  R_IYL,
  R_IY,
  R_I,
  R_R,
  R_PC,
  R_SP,
  R_IP,
  R_BANK,
#endif
};

enum m68k_insn_type_t
{
    M68K_linea = CUSTOM_INSN_ITYPE,
    M68K_linef,
};