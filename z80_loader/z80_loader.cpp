#define VERSION "1.25"
/*
*      SEGA MEGA DRIVE/GENESIS Z80 Drivers Loader
*      Author: DrMefistO [Lab 313] <newinferno@gmail.com>
*/

#define _CRT_SECURE_NO_WARNINGS

#include <ida.hpp>
#include <idp.hpp>
#include <entry.hpp>
#include <diskio.hpp>
#include <loader.hpp>
#include <auto.hpp>
#include <name.hpp>
#include <bytes.hpp>
#include <struct.hpp>
#include <enum.hpp>
#include <fixup.hpp>

#include "z80_loader.h"

struct reg {
  asize_t size;
  ea_t addr;
  char *name;
};

static const reg spec_regs[] = {
  { 1, 0x4000, "YM2612_A0" },
  { 1, 0x4001, "YM2612_D0" },
  { 1, 0x4002, "YM2612_A1" },
  { 1, 0x4003, "YM2612_D1" },

  { 1, 0x6000, "Z80_BANK" },

  { 1, 0x7F11, "SN76489_PSG" },
};

static const char Z80[] = "Z80";
static const char ROM[] = "ROM";
static const char YM2612_REGS[] = "YM2612_REGS";
static const char BANK_REG[] = "BANK_REG";
static const char PSG_REG[] = "PSG_REG";
static const char RESV1[] = "RESV1";
static const char RESV2[] = "RESV2";
static const char RESV3[] = "RESV3";
static const char RESV4[] = "RESV4";
static const char BANK[] = "BANK";

static const char CODE[] = "CODE";
static const char DATA[] = "DATA";
static const char XTRN[] = "XTRN";

static inline unsigned int SWAP_BYTES_32(unsigned int a) {
  return ((a >> 24) & 0x000000FF) | ((a >> 8) & 0x0000FF00) | ((a << 8) & 0x00FF0000) | ((a << 24) & 0xFF000000); // Swap dword LE to BE
}

static inline unsigned short READ_BE_WORD(unsigned char *addr) {
  return (addr[0] << 8) | addr[1]; // Read BE word
}

static inline unsigned int READ_BE_UINT(unsigned char *addr) {
  return (READ_BE_WORD(&addr[0]) << 16) | READ_BE_WORD(&addr[2]); // Read BE unsigned int by pointer
}

static void add_sub(unsigned int addr, const char *name) {
  ea_t e_addr = to_ea(sel2para(0), addr);

  auto_make_proc(e_addr);
  set_name(e_addr, name);
}

static void add_offset_field(struc_t *st, segment_t *code_segm, const char *name) {
  opinfo_t info = { 0 };
  info.ri.init(REF_OFF32, BADADDR);
  add_struc_member(st, name, BADADDR, dword_flag() | off_flag(), &info, 4);
}

static void add_dword_field(struc_t *st, const char *name) {
  add_struc_member(st, name, BADADDR, dword_flag() | hex_flag(), NULL, 4);
}

static void add_string_field(struc_t *st, const char *name, asize_t size) {
  opinfo_t info = { 0 };
  info.strtype = STRTYPE_C;
  add_struc_member(st, name, BADADDR, strlit_flag(), &info, size);
}

static void add_byte_field(struc_t *st, const char *name, const char *cmt = NULL) {
  add_struc_member(st, name, BADADDR, byte_flag() | hex_flag(), NULL, 1);
  member_t* mm = get_member_by_name(st, name);
  set_member_cmt(mm, cmt, true);
}

static void add_short_field(struc_t *st, const char *name, const char *cmt = NULL) {
  add_struc_member(st, name, BADADDR, word_flag() | hex_flag(), NULL, 2);
  member_t *mm = get_member_by_name(st, name);
  set_member_cmt(mm, cmt, true);
}

static void add_dword_array(struc_t *st, const char *name, asize_t length) {
  add_struc_member(st, name, BADADDR, dword_flag() | hex_flag(), NULL, 4 * length);
}

static void add_enum_member_with_mask(enum_t id, const char* name, unsigned int value, unsigned int mask = DEFMASK, const char* cmt = NULL) {
  int res = add_enum_member(id, name, value, mask);
  if (cmt != NULL) set_enum_member_cmt(get_enum_member_by_name(name), cmt, false);
}

static void add_segment(ea_t start, ea_t end, const char *name, const char *class_name, const char *cmnt, uchar perm) {
  segment_t s;
  s.sel = 0;
  s.start_ea = start;
  s.end_ea = end;
  s.align = saRelByte;
  s.comb = scPub;
  s.bitness = 1; // 32-bit
  s.perm = perm;

  int flags = ADDSEG_NOSREG | ADDSEG_NOTRUNC | ADDSEG_QUIET;

  if (!add_segm_ex(&s, name, class_name, flags)) loader_failure();
  segment_t *segm = getseg(start);
  set_segment_cmt(segm, cmnt, false);
  create_byte(start, 1);
  segm->update();
}

static void set_spec_register_names() {
  for (int i = 0; i < _countof(spec_regs); i++) {
    if (spec_regs[i].size == 2) {
      create_word(spec_regs[i].addr, 2);
    }
    else if (spec_regs[i].size == 4) {
      create_dword(spec_regs[i].addr, 4);
    }
    else {
      create_byte(spec_regs[i].addr, spec_regs[i].size);
    }
    set_name(spec_regs[i].addr, spec_regs[i].name);
  }
}

static void make_segments() {
  add_segment(0x0000, 0x001FFF + 1, ROM, CODE, "Main segment", SEGPERM_EXEC | SEGPERM_READ);
  add_segment(0x2000, 0x003FFF + 1, RESV1, DATA, "Reserved", SEGPERM_READ | SEGPERM_WRITE);

  add_segment(0x4000, 0x004003 + 1, YM2612_REGS, XTRN, "YM2612 Regs", SEGPERM_WRITE);

  add_segment(0x4004, 0x005FFF + 1, RESV2, DATA, "Reserved", SEGPERM_READ | SEGPERM_WRITE);

  add_segment(0x6000, 0x006000 + 1, BANK_REG, XTRN, "Z80 Bank Reg", SEGPERM_WRITE);

  add_segment(0x6001, 0x007F10 + 1, RESV3, DATA, "Reserved", SEGPERM_READ | SEGPERM_WRITE);

  add_segment(0x7F11, 0x007F11 + 1, PSG_REG, XTRN, "SN76489 PSG", SEGPERM_WRITE);

  add_segment(0x7F12, 0x007FFF + 1, RESV4, DATA, "Reserved", SEGPERM_READ | SEGPERM_WRITE);
  add_segment(0x8000, 0x00FFFF + 1, BANK, DATA, "Reserved", SEGPERM_READ | SEGPERM_WRITE);
}

static void make_ym2612_regs_enum() {
  enum_t ym_regs = add_enum(BADADDR, "ym_registers", hex_flag());
  add_enum_member_with_mask(ym_regs, "REG_LFO", 0x22);
  add_enum_member_with_mask(ym_regs, "REG_TIM_A_FREQ_0", 0x24);
  add_enum_member_with_mask(ym_regs, "REG_TIM_A_FREQ_1", 0x25);
  add_enum_member_with_mask(ym_regs, "REG_TIM_B_FREQ", 0x26);
  add_enum_member_with_mask(ym_regs, "REG_CH3_MODE_TIM_CTRL", 0x27);
  add_enum_member_with_mask(ym_regs, "REG_KEY_ON_KEY_OFF", 0x28);
  add_enum_member_with_mask(ym_regs, "REG_DAC_OUTPUT", 0x2A);
  add_enum_member_with_mask(ym_regs, "REG_DAC_ENABLE", 0x2B);

  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH1_CH4_OP1", 0x30);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH1_CH4_OP2", 0x38);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH1_CH4_OP3", 0x34);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH1_CH4_OP4", 0x3C);

  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH2_CH5_OP1", 0x31);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH2_CH5_OP2", 0x39);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH2_CH5_OP3", 0x35);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH2_CH5_OP4", 0x3D);

  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH3_CH6_OP1", 0x32);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH3_CH6_OP2", 0x3A);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH3_CH6_OP3", 0x36);
  add_enum_member_with_mask(ym_regs, "REG_MULTIPLY_DETUNE_CH3_CH6_OP4", 0x3E);

  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH1_CH4_OP1", 0x40);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH1_CH4_OP2", 0x48);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH1_CH4_OP3", 0x44);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH1_CH4_OP4", 0x4C);

  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH2_CH5_OP1", 0x41);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH2_CH5_OP2", 0x49);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH2_CH5_OP3", 0x45);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH2_CH5_OP4", 0x4D);

  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH3_CH6_OP1", 0x42);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH3_CH6_OP2", 0x4A);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH3_CH6_OP3", 0x46);
  add_enum_member_with_mask(ym_regs, "REG_TOTAL_LEVEL_CH3_CH6_OP4", 0x4E);

  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH1_CH4_OP1", 0x50);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH1_CH4_OP2", 0x58);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH1_CH4_OP3", 0x54);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH1_CH4_OP4", 0x5C);

  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH2_CH5_OP1", 0x51);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH2_CH5_OP2", 0x59);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH2_CH5_OP3", 0x55);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH2_CH5_OP4", 0x5D);

  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH3_CH6_OP1", 0x52);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH3_CH6_OP2", 0x5A);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH3_CH6_OP3", 0x56);
  add_enum_member_with_mask(ym_regs, "REG_ATTACK_RATE_RATE_SCALING_CH3_CH6_OP4", 0x5E);

  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH1_CH4_OP1", 0x60);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH1_CH4_OP2", 0x68);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH1_CH4_OP3", 0x64);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH1_CH4_OP4", 0x6C);

  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH2_CH5_OP1", 0x61);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH2_CH5_OP2", 0x69);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH2_CH5_OP3", 0x65);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH2_CH5_OP4", 0x6D);

  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH3_CH6_OP1", 0x62);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH3_CH6_OP2", 0x6A);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH3_CH6_OP3", 0x66);
  add_enum_member_with_mask(ym_regs, "REG_DECAY_RATE_AM_ENABLE_CH3_CH6_OP4", 0x6E);

  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH1_CH4_OP1", 0x70);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH1_CH4_OP2", 0x78);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH1_CH4_OP3", 0x74);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH1_CH4_OP4", 0x7C);

  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH2_CH5_OP1", 0x71);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH2_CH5_OP2", 0x79);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH2_CH5_OP3", 0x75);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH2_CH5_OP4", 0x7D);

  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH3_CH6_OP1", 0x72);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH3_CH6_OP2", 0x7A);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH3_CH6_OP3", 0x76);
  add_enum_member_with_mask(ym_regs, "REG_SUSTAIN_RATE_CH3_CH6_OP4", 0x7E);

  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH1_CH4_OP1", 0x80);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH1_CH4_OP2", 0x88);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH1_CH4_OP3", 0x84);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH1_CH4_OP4", 0x8C);

  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH2_CH5_OP1", 0x81);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH2_CH5_OP2", 0x89);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH2_CH5_OP3", 0x85);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH2_CH5_OP4", 0x8D);

  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH3_CH6_OP1", 0x82);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH3_CH6_OP2", 0x8A);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH3_CH6_OP3", 0x86);
  add_enum_member_with_mask(ym_regs, "REG_RELEASE_RATE_SUSTAIN_LEVEL_CH3_CH6_OP4", 0x8E);

  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH1_CH4_OP1", 0x90);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH1_CH4_OP2", 0x98);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH1_CH4_OP3", 0x94);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH1_CH4_OP4", 0x9C);

  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH2_CH5_OP1", 0x91);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH2_CH5_OP2", 0x99);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH2_CH5_OP3", 0x95);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH2_CH5_OP4", 0x9D);

  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH3_CH6_OP1", 0x92);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH3_CH6_OP2", 0x9A);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH3_CH6_OP3", 0x96);
  add_enum_member_with_mask(ym_regs, "REG_SSG_EG_CH3_CH6_OP4", 0x9E);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH1_CH4_HIGH_HALF", 0xA4);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH1_CH4_LOW_HALF", 0xA0);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH2_CH5_HIGH_HALF", 0xA5);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH2_CH5_LOW_HALF", 0xA1);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_CH6_HIGH_HALF", 0xA6);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_CH6_LOW_HALF", 0xA2);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S1_HIGH_HALF", 0xAD);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S1_LOW_HALF", 0xA9);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S2_HIGH_HALF", 0xAE);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S2_LOW_HALF", 0xAA);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S3_HIGH_HALF", 0xAC);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S3_LOW_HALF", 0xA8);

  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S4_HIGH_HALF", 0xA6);
  add_enum_member_with_mask(ym_regs, "REG_FREQUENCY_CH3_SPECIAL_S4_LOW_HALF", 0xA2);

  add_enum_member_with_mask(ym_regs, "REG_ALGORITHM_FEEDBACK_CH1_CH4", 0xB0);
  add_enum_member_with_mask(ym_regs, "REG_ALGORITHM_FEEDBACK_CH2_CH5", 0xB1);
  add_enum_member_with_mask(ym_regs, "REG_ALGORITHM_FEEDBACK_CH3_CH6", 0xB2);

  add_enum_member_with_mask(ym_regs, "REG_PANNING_PMS_AMS_CH1_CH4", 0xB4);
  add_enum_member_with_mask(ym_regs, "REG_PANNING_PMS_AMS_CH2_CH5", 0xB5);
  add_enum_member_with_mask(ym_regs, "REG_PANNING_PMS_AMS_CH3_CH6", 0xB6);
}

static void make_lfo_frequency_enum() {
  enum_t en = add_enum(BADADDR, "lfo_frequency", bin_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "LFO_FREQ_3_82_Hz",  0b000, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_5_33_Hz",  0b001, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_5_77_Hz",  0b010, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_6_11_Hz",  0b011, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_6_60_Hz",  0b100, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_9_23_Hz",  0b101, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_46_11_Hz", 0b110, 0b111);
  add_enum_member_with_mask(en, "LFO_FREQ_69_22_Hz", 0b111, 0b111);

  add_enum_member_with_mask(en, "LFO_ENABLE", 0b1000, 0b1000);
}

static void make_ch3_mode_timer_ctrl() {
  enum_t en = add_enum(BADADDR, "ch3_mode_tmr_ctrl", bin_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "TIMER_CTRL_LOAD_A_FROZEN",  0b00000000, 0b00000001);
  add_enum_member_with_mask(en, "TIMER_CTRL_LOAD_A_RUNNING", 0b00000001, 0b00000001);

  add_enum_member_with_mask(en, "TIMER_CTRL_LOAD_B_FROZEN",  0b00000000, 0b00000010);
  add_enum_member_with_mask(en, "TIMER_CTRL_LOAD_B_RUNNING", 0b00000010, 0b00000010);

  add_enum_member_with_mask(en, "TIMER_CTRL_ENBL_A",         0b00000100, 0b00000100);
  add_enum_member_with_mask(en, "TIMER_CTRL_ENBL_B",         0b00001000, 0b00001000);

  add_enum_member_with_mask(en, "TIMER_CTRL_RST_A",          0b00010000, 0b00010000);
  add_enum_member_with_mask(en, "TIMER_CTRL_RST_B",          0b00100000, 0b00100000);

  add_enum_member_with_mask(en, "CH3_MODE_NORMAL",           0b00000000, 0b11000000);
  add_enum_member_with_mask(en, "CH3_MODE_SPECIAL",          0b01000000, 0b11000000);
  add_enum_member_with_mask(en, "CH3_MODE_CSM",              0b10000000, 0b11000000);
}

static void make_key_on_key_off_enum() {
  enum_t en = add_enum(BADADDR, "key_on_key_off", bin_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "CH1",          0b00000000, 0b00000111);
  add_enum_member_with_mask(en, "CH2",          0b00000001, 0b00000111);
  add_enum_member_with_mask(en, "CH3",          0b00000010, 0b00000111);
  add_enum_member_with_mask(en, "CH4",          0b00000100, 0b00000111);
  add_enum_member_with_mask(en, "CH5",          0b00000101, 0b00000111);
  add_enum_member_with_mask(en, "CH6",          0b00000110, 0b00000111);

  add_enum_member_with_mask(en, "S1_KEY_OFF",   0b00000000, 0b00010000);
  add_enum_member_with_mask(en, "S1_KEY_ON",    0b00010000, 0b00010000);

  add_enum_member_with_mask(en, "S2_KEY_OFF",   0b00000000, 0b00100000);
  add_enum_member_with_mask(en, "S2_KEY_ON",    0b00100000, 0b00100000);

  add_enum_member_with_mask(en, "S3_KEY_OFF",   0b00000000, 0b01000000);
  add_enum_member_with_mask(en, "S3_KEY_ON",    0b01000000, 0b01000000);

  add_enum_member_with_mask(en, "S4_KEY_OFF",   0b00000000, 0b10000000);
  add_enum_member_with_mask(en, "S4_KEY_ON",    0b10000000, 0b10000000);
}

static void make_dac_enable_enum() {
  enum_t en = add_enum(BADADDR, "dac_enable_disable", bin_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "DAC_OUTPUT_FM",  0b00000000, 0b10000000);
  add_enum_member_with_mask(en, "DAC_OUTPUT_DAC", 0b10000000, 0b10000000);
}

static void make_mul_dt_enum() {
  enum_t en = add_enum(BADADDR, "multiply_detune", hex_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "FREQ_MUL_HALF",   0, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_1",      1, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_2",      2, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_3",      3, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_4",      4, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_5",      5, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_6",      6, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_7",      7, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_8",      8, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_9",      9, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_10",    10, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_11",    11, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_12",    12, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_13",    13, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_14",    14, 0x0F);
  add_enum_member_with_mask(en, "FREQ_MUL_15",    15, 0x0F);

  add_enum_member_with_mask(en, "FREQ_NO_DETUNE",   0b00000000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_PLUS_1xE",    0b00010000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_PLUS_2xE",    0b00100000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_PLUS_3xE",    0b00110000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_NO_DETUNE",   0b01000000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_MINUS_1xE",   0b01010000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_MINUS_2xE",   0b01100000, 0b01110000);
  add_enum_member_with_mask(en, "FREQ_MINUS_3xE",   0b01110000, 0b01110000);
}

static void make_ar_rs_enum() {
  enum_t en = add_enum(BADADDR, "attack_rate_scaling", hex_flag());
  set_enum_bf(en, true);

  char tmp[16];

  for (auto i = 0; i <= 0x1F; ++i) {
    qsnprintf(tmp, 16, "ATTACK_RATE_%d", i);

    add_enum_member_with_mask(en, tmp, i, 0x1F);
  }

  add_enum_member_with_mask(en, "RATE_SCALING_1", 0b01000000, 0b11000000);
  add_enum_member_with_mask(en, "RATE_SCALING_2", 0b10000000, 0b11000000);
  add_enum_member_with_mask(en, "RATE_SCALING_3", 0b11000000, 0b11000000);
}

static void make_dr_am_enable_enum() {
  enum_t en = add_enum(BADADDR, "dr_am_enable", hex_flag());
  set_enum_bf(en, true);

  char tmp[16];

  for (auto i = 0; i <= 0x1F; ++i) {
    qsnprintf(tmp, 16, "DECAY_RATE_%d", i);

    add_enum_member_with_mask(en, tmp, i, 0x1F);
  }

  add_enum_member_with_mask(en, "AMS_ENABLED", 0b10000000, 0b10000000);
}

static void make_rr_sl_enum() {
  enum_t en = add_enum(BADADDR, "release_rate_sustain_level", hex_flag());
  set_enum_bf(en, true);

  char tmp[16];

  for (auto i = 0; i <= 0x0F; ++i) {
    qsnprintf(tmp, 16, "RELEASE_RATE_%d", i);

    add_enum_member_with_mask(en, tmp, i, 0x0F);
  }

  for (auto i = 0; i <= 0x0F; ++i) {
    qsnprintf(tmp, 16, "SUSTAIN_LEVEL_%d", i);

    add_enum_member_with_mask(en, tmp, i << 4, 0xF0);
  }
}

static void make_frequency_enum() {
  enum_t en = add_enum(BADADDR, "frequency_octave", hex_flag());
  set_enum_bf(en, true);

  char tmp[32];

  for (auto i = 0; i <= 7; ++i) {
    qsnprintf(tmp, 32, "FREQ_BYTE_HIGH_%02X", i);

    add_enum_member_with_mask(en, tmp, i, 7);
  }

  for (auto i = 0; i <= 7; ++i) {
    qsnprintf(tmp, 32, "BLK_OCTAVE_%d", i);

    add_enum_member_with_mask(en, tmp, i << 3, 7 << 3);
  }
}

static void make_algo_feedback_enum() {
  enum_t en = add_enum(BADADDR, "algo_feedback", hex_flag());
  set_enum_bf(en, true);

  char tmp[16];

  for (auto i = 0; i <= 7; ++i) {
    qsnprintf(tmp, 32, "ALGO_%d", i);

    add_enum_member_with_mask(en, tmp, i, 7);
  }

  for (auto i = 0; i <= 7; ++i) {
    qsnprintf(tmp, 32, "FEED_%d", i);

    add_enum_member_with_mask(en, tmp, i << 3, 7 << 3);
  }
}

static void make_panning_pms_ams_enum() {
  enum_t en = add_enum(BADADDR, "panning_pms_ams", hex_flag());
  set_enum_bf(en, true);

  add_enum_member_with_mask(en, "PMS_DISABLED", 0, 7);
  add_enum_member_with_mask(en, "PMS_0_034_SEMITONES", 1, 7);
  add_enum_member_with_mask(en, "PMS_0_067_SEMITONES", 2, 7);
  add_enum_member_with_mask(en, "PMS_0_10_SEMITONES", 3, 7);
  add_enum_member_with_mask(en, "PMS_0_14_SEMITONES", 4, 7);
  add_enum_member_with_mask(en, "PMS_0_20_SEMITONES", 5, 7);
  add_enum_member_with_mask(en, "PMS_0_40_SEMITONES", 6, 7);
  add_enum_member_with_mask(en, "PMS_0_80_SEMITONES", 7, 7);

  add_enum_member_with_mask(en, "AMS_DISABLED", 0, 3 << 4);
  add_enum_member_with_mask(en, "AMS_1_4_dB", 1 << 4, 3 << 4);
  add_enum_member_with_mask(en, "AMS_5_9_dB", 2 << 4, 3 << 4);
  add_enum_member_with_mask(en, "AMS_11_8_dB", 3 << 4, 3 << 4);
}

static void print_version() {
  static const char format[] = "Sega Genesis/Megadrive Z80 drivers loader v%s;\nAuthor: DrMefistO [Lab 313] <newinferno@gmail.com>.";
  info(format, VERSION);
  msg(format, VERSION);
}

static int idaapi accept_file(qstring* fileformatname, qstring* processor, linput_t* li, const char* filename) {
  qlseek(li, 0, SEEK_SET);

  if (qlsize(li) > 0x2000) return 0;

  fileformatname->sprnt("%s", "Z80 drivers loader v1");

  return 1;
}

void idaapi load_file(linput_t *li, ushort neflags, const char *fileformatname) {
  if (ph.id != PLFM_Z80) {
    set_processor_type(Z80, SETPROC_LOADER); // Z80
  }

  inf.af = 0
    //| AF_FIXUP //        0x0001          // Create offsets and segments using fixup info
    //| AF_MARKCODE  //     0x0002          // Mark typical code sequences as code
    | AF_UNK //          0x0004          // Delete instructions with no xrefs
    | AF_CODE //         0x0008          // Trace execution flow
    | AF_PROC //         0x0010          // Create functions if call is present
    | AF_USED //         0x0020          // Analyze and create all xrefs
    //| AF_FLIRT //        0x0040          // Use flirt signatures
    //| AF_PROCPTR //      0x0080          // Create function if data xref data->code32 exists
    | AF_JFUNC //        0x0100          // Rename jump functions as j_...
    | AF_NULLSUB //      0x0200          // Rename empty functions as nullsub_...
    //| AF_LVAR //         0x0400          // Create stack variables
    //| AF_TRACE //        0x0800          // Trace stack pointer
    //| AF_ASCII //        0x1000          // Create ascii string if data xref exists
    | AF_IMMOFF //       0x2000          // Convert 32bit instruction operand to offset
    //AF_DREFOFF //      0x4000          // Create offset if data xref to seg32 exists
    //| AF_FINAL //       0x8000          // Final pass of analysis
    | AF_JUMPTBL  //    0x0001          // Locate and create jump tables
    | AF_STKARG  //     0x0008          // Propagate stack argument information
    | AF_REGARG  //     0x0010          // Propagate register argument information
    | AF_SIGMLT  //     0x0080          // Allow recognition of several copies of the same function
    | AF_FTAIL  //      0x0100          // Create function tails
    | AF_DATOFF  //     0x0200          // Automatically convert data to offsets
    | AF_TRFUNC  //     0x2000          // Truncate functions upon code deletion
    | AF_PURDAT  //     0x4000          // Control flow to data segment is ignored
    ;
  inf.af2 = 0
    //| AF2_DODATA  //     0x0002          // Coagulate data segs at the final pass
    //| AF2_HFLIRT  //     0x0004          // Automatically hide library functions
    //| AF2_CHKUNI  //     0x0020          // Check for unicode strings
    //| AF2_SIGCMT  //     0x0040          // Append a signature name comment for recognized anonymous library functions
    //| AF2_ANORET  //     0x0400          // Perform 'no-return' analysis
    //| AF2_VERSP  //      0x0800          // Perform full SP-analysis (ph.verify_sp)
    //| AF2_DOCODE  //     0x1000          // Coagulate code segs at the final pass
    //| AF2_MEMFUNC //    0x8000          // Try to guess member function types
    ;

  unsigned int size = qlsize(li); // size of driver

  qlseek(li, 0, SEEK_SET);
  if (size > 0x2000) loader_failure();
  file2base(li, 0, 0x0000, size, FILEREG_PATCHABLE); // load driver to database

  make_segments(); // create segments
  set_spec_register_names(); // apply names for special addresses of registers

  make_ym2612_regs_enum(); // add enum with all ym2612 registers
  make_lfo_frequency_enum();
  make_ch3_mode_timer_ctrl();
  make_key_on_key_off_enum();
  make_dac_enable_enum();
  make_mul_dt_enum();
  make_ar_rs_enum();
  make_dr_am_enable_enum();
  make_rr_sl_enum();
  make_frequency_enum();
  make_algo_feedback_enum();
  make_panning_pms_ams_enum();

  del_items(0x0000, DELIT_SIMPLE);
  add_sub(0x0000, "start");

  idainfo* _inf = &inf;
  //_inf->outflags |= OFLG_LZERO;
  _inf->baseaddr = _inf->start_cs = 0;
  _inf->start_ip = _inf->start_ea = _inf->main = 0;
  _inf->lowoff = 0;

  print_version();
}

loader_t LDSC = {
  IDP_INTERFACE_VERSION,
  0,                            // loader flags
  //
  //      check input file format. if recognized, then return 1
  //      and fill 'fileformatname'.
  //      otherwise return 0
  //
  accept_file,
  //
  //      load file into the database.
  //
  load_file,
  //
  //      create output file from the database.
  //      this function may be absent.
  //
  NULL,
  //      take care of a moved segment (fix up relocations, for example)
  NULL
};
