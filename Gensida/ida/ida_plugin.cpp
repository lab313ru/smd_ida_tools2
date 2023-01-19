// Copyright (C) 2021 DrMefistO
//
// This program is free software : you can redistribute it and / or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License 2.0 for more details.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http ://www.gnu.org/licenses/

#include <Windows.h>
#include <Psapi.h>
#include <ida.hpp>
#include <dbg.hpp>
#include <idd.hpp>
#include <loader.hpp>
#include <idp.hpp>
#include <offset.hpp>
#include <struct.hpp>
#include <enum.hpp>
#include <expr.hpp>
#include <regex>

#include "ida_plugin.h"

#include "ida_debmod.h"
#include "ida_registers.h"
#include <mutex>

extern debugger_t debugger;

static bool plugin_inited;
static bool dbg_started;
static bool my_dbg;

#ifdef DEBUG_68K
static ssize_t idaapi hook_dbg(void* user_data, int notification_code, va_list va)
{
  switch (notification_code)
  {
  case dbg_notification_t::dbg_process_start:
    dbg_started = true;
    break;

  case dbg_notification_t::dbg_process_exit:
    dbg_started = false;
    break;
  }
  return 0;
}

static int idaapi idp_to_dbg_reg(int idp_reg)
{
  int reg_idx = idp_reg;
  if (idp_reg >= 0 && idp_reg <= 7)
    reg_idx = R_D0 + idp_reg;
  else if (idp_reg >= 8 && idp_reg <= 39)
    reg_idx = R_A0 + (idp_reg % 8);
  else if (idp_reg == 91)
    reg_idx = R_PC;
  else if (idp_reg == 92 || idp_reg == 93)
    reg_idx = R_SR;
  else if (idp_reg == 94)
    reg_idx = R_A7;
  else
  {
    char buf[MAXSTR];
    ::qsnprintf(buf, MAXSTR, "reg: %d\n", idp_reg);
    warning("SEND THIS MESSAGE TO newinferno@gmail.com:\n%s\n", buf);
    return 0;
  }
  return reg_idx;
}
#endif

#ifdef _DEBUG
static const char* const optype_names[] =
{
    "o_void",
    "o_reg",
    "o_mem",
    "o_phrase",
    "o_displ",
    "o_imm",
    "o_far",
    "o_near",
    "o_idpspec0",
    "o_idpspec1",
    "o_idpspec2",
    "o_idpspec3",
    "o_idpspec4",
    "o_idpspec5",
};

static const char* const dtyp_names[] =
{
    "dt_byte",
    "dt_word",
    "dt_dword",
    "dt_float",
    "dt_double",
    "dt_tbyte",
    "dt_packreal",
    "dt_qword",
    "dt_byte16",
    "dt_code",
    "dt_void",
    "dt_fword",
    "dt_bitfild",
    "dt_string",
    "dt_unicode",
    "dt_3byte",
    "dt_ldbl",
    "dt_byte32",
    "dt_byte64",
};

static void print_insn(insn_t* insn)
{
  if (my_dbg)
  {
    msg("cs=%x, ", insn->cs);
    msg("ip=%x, ", insn->ip);
    msg("ea=%x, ", insn->ea);
    msg("itype=%x, ", insn->itype);
    msg("size=%x, ", insn->size);
    msg("auxpref=%x, ", insn->auxpref);
    msg("segpref=%x, ", insn->segpref);
    msg("insnpref=%x, ", insn->insnpref);
    msg("insnpref=%x, ", insn->insnpref);

    msg("flags[");
    if (insn->flags & INSN_MACRO)
      msg("INSN_MACRO|");
    if (insn->flags & INSN_MODMAC)
      msg("OF_OUTER_DISP");
    msg("]\n");
  }
}

static void print_op(ea_t ea, op_t* op)
{
  if (my_dbg)
  {
    msg("type[%s], ", optype_names[op->type]);

    msg("flags[");
    if (op->flags & OF_NO_BASE_DISP)
      msg("OF_NO_BASE_DISP|");
    if (op->flags & OF_OUTER_DISP)
      msg("OF_OUTER_DISP|");
    if (op->flags & PACK_FORM_DEF)
      msg("PACK_FORM_DEF|");
    if (op->flags & OF_NUMBER)
      msg("OF_NUMBER|");
    if (op->flags & OF_SHOW)
      msg("OF_SHOW");
    msg("], ");

    msg("dtyp[%s], ", dtyp_names[op->dtype]);

    if (op->type == o_reg)
      msg("reg=%x, ", op->reg);
    else if (op->type == o_displ || op->type == o_phrase)
      msg("phrase=%x, ", op->phrase);
    else
      msg("reg_phrase=%x, ", op->phrase);

    msg("addr=%x, ", op->addr);

    msg("value=%x, ", op->value);

    msg("specval=%x, ", op->specval);

    msg("specflag1=%x, ", op->specflag1);
    msg("specflag2=%x, ", op->specflag2);
    msg("specflag3=%x, ", op->specflag3);
    msg("specflag4=%x, ", op->specflag4);

    msg("refinfo[");

    opinfo_t buf;

    if (get_opinfo(&buf, ea, op->n, op->flags))
    {
      msg("target=%x, ", buf.ri.target);
      msg("base=%x, ", buf.ri.base);
      msg("tdelta=%x, ", buf.ri.tdelta);

      msg("flags[");
      if (buf.ri.flags & REFINFO_TYPE)
        msg("REFINFO_TYPE|");
      if (buf.ri.flags & REFINFO_RVAOFF)
        msg("REFINFO_RVAOFF|");
      if (buf.ri.flags & REFINFO_PASTEND)
        msg("REFINFO_PASTEND|");
      if (buf.ri.flags & REFINFO_CUSTOM)
        msg("REFINFO_CUSTOM|");
      if (buf.ri.flags & REFINFO_NOBASE)
        msg("REFINFO_NOBASE|");
      if (buf.ri.flags & REFINFO_SUBTRACT)
        msg("REFINFO_SUBTRACT|");
      if (buf.ri.flags & REFINFO_SIGNEDOP)
        msg("REFINFO_SIGNEDOP");
      msg("]");
    }
    msg("]\n");
  }
}

#endif

#ifdef DEBUG_68K
/// <summary>
/// this thing patches IDA to output structures in a full form, not like s1 <0>, but s1 <0, 0, 0>
/// </summary>
static uint8_t* dirty_win_hack() {
  // if not found, look for seq "01 1F 30 02 1F 00 00 00" in ida.dll
  static const uint8_t find_pat[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x8B, 0xD9, 0x8B, 0xF1 };

  MODULEINFO modinfo = { 0 };
  HMODULE hModule = GetModuleHandle(TEXT("ida.dll"));

  if (hModule == 0) {
    return NULL;
  }

  GetModuleInformation(GetCurrentProcess(), hModule, &modinfo, sizeof(MODULEINFO));

  uint8_t* p = (uint8_t*)modinfo.lpBaseOfDll;
  auto base = 0;

  for (; base < modinfo.SizeOfImage; ++base) {
    if (!memcmp(&p[base], find_pat, sizeof(find_pat))) {
      break;
    }
  }

  if (base == modinfo.SizeOfImage) {
    return NULL;
  }

  p = &p[base + 0x3E]; // lea rax, empty_struc

  DWORD oldProtection;
  VirtualProtect(p, 7, PAGE_EXECUTE_READWRITE, &oldProtection);
  p[0] = 0x33; // xor eax, eax
  p[1] = 0xC0;
  p[2] = 0x90; // nop
  p[3] = 0x90;
  p[4] = 0x90;
  p[5] = 0x90;
  p[6] = 0x90;

  VirtualProtect(p, 7, oldProtection, NULL);

  return p;
}

static void undo_dirty_win_hack(uint8_t* p) {
  if (p == NULL) {
    return;
  }

  DWORD oldProtection;
  VirtualProtect(p, 7, PAGE_EXECUTE_READWRITE, &oldProtection);
  p[0] = 0x48;
  p[1] = 0x8D;
  p[2] = 0x05;
  p[3] = 0x17;
  p[4] = 0xD4;
  p[5] = 0x2E;
  p[6] = 0x00;

  VirtualProtect(p, 7, oldProtection, NULL);
}

static enum asm_out_state {
  asm_out_none,
  asm_out_del_start,
  asm_out_del_end,
  asm_out_bin_start,
  asm_out_bin_end,
  asm_out_inc_start,
  asm_out_inc_end,
  asm_out_struct_start,
  asm_out_rom_end,
} asm_o_state;

static uint8_t* patch_offset = NULL;
static ea_t bin_inc_start = BADADDR;
static qstring bin_inc_path;
static qstrvec_t inc_listing;
static bool warn_shown = false;

static const std::regex re_check_code_repl("^CODE_REPL$");
static const std::regex re_check_code_repl_final("^CODE_END$");
static const std::regex re_check_ats("^((?:\\w*@+\\w*)+):");
static const std::regex re_check_global("^[ \\t]+global[ \\t]+\\w+$");
static const std::regex re_check_binclude("^BIN_START \"(.+)\"");
static const std::regex re_check_include("^INC_START \"(.+)\"");
static const std::regex re_check_subroutine("^; =+ S U B R O U T I N E =+$");
static const std::regex re_check_licensed_to("^; \\|[ \\t]+Licensed to:.+$");
static const std::regex re_check_org("^ORG[ \\t]+(\\$[0-9a-fA-F]+)$");
static const std::regex re_check_align("^[ \\t]+align[ \\t]+(\\d+)");
static const std::regex re_check_dcb_0("^[ \\t]+dcb\\.b[ \\t]+(\\$?\\w+),(?:[ \\t]+)?0");
static const std::regex re_check_dcb_xx("^[ \\t]+dcb\\.b[ \\t]+(\\$?\\w+),(?:[ \\t]+)?(\\$?\\w+)");
//static const std::regex re_check_quotates("([^#'])'(.+?)'");
static const std::regex re_s_parse_name("^(\\w+)[ \\t]+struc .+$");
static const std::regex re_s_parse_field("^(\\w+):[ \\t]+(.+)$");
static const std::regex re_s_parse_end("^\\w+[ \\t]+ends(?:.+)?$");
static const std::regex re_equ_parse("^(\\w+):[ \\t]+equ (.+)$");
static const std::regex re_remove_offset("^\\w+:([0-9A-F]+) (.+)$");

static void fix_arrows(qstring& line) {
  line.replace("\xe2\x86\x93", "   "); // arrow down
  line.replace("\xe2\x86\x91", "   "); // arrow up
}

static ea_t remove_offset(qstring& line) {
  ea_t addr = BADADDR;

  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_remove_offset) != true) {
    line.clear();
    return addr;
  }

  addr = strtoul(match.str(1).c_str(), nullptr, 16);
  line = match.str(2).c_str();

  return addr;
}

static asm_out_state check_delete(qstring& line) {
  if (line.find("DEL_START") != qstring::npos) {
    return asm_out_del_start;
  }
  else if ((line.find("DEL_END") != qstring::npos) && (asm_o_state == asm_out_del_start)) {
    line.clear();
    return asm_out_del_end;
  }

  return asm_o_state;
}

static int path_create_dir(const qstring& path) {
  size_t start = 0;

  while (1) {
    size_t p = path.find('/', start);

    if (p == qstring::npos) {
      break;
    }

    qstring sub = path.substr(0, p);
    qmkdir(sub.c_str(), 0777);

    start = p + 1;
  }

  return 1;
}

static asm_out_state check_binclude(qstring& line, ea_t addr) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_binclude) == true) {
    bin_inc_start = addr;

    bin_inc_path.clear();
    bin_inc_path.append(match.str(1).c_str());

    return asm_out_bin_start;
  }
  else if ((line.find("BIN_END") != qstring::npos) && (asm_o_state == asm_out_bin_start)) {
    addr = next_head(addr, BADADDR);
    size_t size = addr - bin_inc_start;
    uint8_t* tmp = (uint8_t*)malloc(size);
    get_bytes(tmp, size, bin_inc_start, 0);

    if (!path_create_dir(bin_inc_path)) {
      free(tmp);
      return asm_o_state;
    }

    FILE* f = qfopen(bin_inc_path.c_str(), "wb");

    if (f == nullptr) {
      free(tmp);
      return asm_o_state;
    }

    qfwrite(f, tmp, size);
    qfclose(f);

    free(tmp);

    qstring name = get_name(bin_inc_start);
    line.clear();

    if (!name.empty()) {
      line.append(name);
      line.append(":\n");
    }

    line.cat_sprnt("    binclude \"%s\"\n    align 2, 0", bin_inc_path.c_str());

    return asm_out_bin_end;
  }

  return asm_o_state;
}

static asm_out_state check_include(qstring& line, ea_t addr) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_include) == true) {
    bin_inc_start = addr;

    bin_inc_path.clear();
    bin_inc_path.append(match.str(1).c_str());

    inc_listing.clear();

    return asm_out_inc_start;
  }
  else if ((line.find("INC_END") != qstring::npos) && (asm_o_state == asm_out_inc_start)) {
    if (!path_create_dir(bin_inc_path)) {
      return asm_o_state;
    }

    FILE* f = qfopen(bin_inc_path.c_str(), "wb");

    if (f == nullptr) {
      return asm_o_state;
    }

    for each (const auto var in inc_listing) {
      qfwrite(f, var.c_str(), var.length());
      qfwrite(f, "\n", 1);
    }

    qfclose(f);

    qstring name = get_name(bin_inc_start);
    line.clear();

    line.cat_sprnt("    include \"%s\"", bin_inc_path.c_str());

    return asm_out_inc_end;
  }
  else if (asm_o_state == asm_out_inc_start) {
    inc_listing.add(line);
  }

  return asm_o_state;
}

static int check_subroutine(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_subroutine) != true) {
    return 1;
  }

  return 0;
}

static int check_licensed_to(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_licensed_to) != true) {
    return 1;
  }

  return 0;
}

static int check_global(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_global) != true) {
    return 1;
  }

  return 0;
}

static void check_org(qstring& line) {
  std::string rep = std::regex_replace(line.c_str(), re_check_org, "    org $1");

  line = rep.c_str();
}

static void check_align(qstring& line) {
  std::string rep = std::regex_replace(line.c_str(), re_check_align, "    align $1, 0");

  line = rep.c_str();
}

static void check_dcb(qstring& line) {
  std::string rep = std::regex_replace(line.c_str(), re_check_dcb_0, "    rorg $1");
  rep = std::regex_replace(rep.c_str(), re_check_dcb_xx, "    dc.b [$1]$2");

  line = rep.c_str();
}

//static void check_quotates(qstring& line) {
//  std::string rep = std::regex_replace(line.c_str(), re_check_quotates, "$1\"$2\"");
//
//  line = rep.c_str();
//}

static void check_ats(qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_check_ats) != true) {
    return;
  }

  line.replace("@", "_");
}

static asm_out_state check_rom_end(ea_t addr) {
  return (addr == 0xA00000) ? asm_out_rom_end : asm_o_state;
}

static void print_line(FILE* fp, const qstring& line) {
  qfwrite(fp, line.c_str(), line.length());
  qfwrite(fp, "\n", 1);
}

static int idaapi line_output(FILE* fp, const qstring& line, bgcolor_t prefix_color, bgcolor_t bg_color) {
  qstring qbuf;
  tag_remove(&qbuf, line);

  fix_arrows(qbuf);

  ea_t addr = remove_offset(qbuf);

  if (addr == BADADDR || is_unknown(get_flags(addr))) {
    return 1;
  }

  if (!check_subroutine(qbuf)) {
    print_line(fp, "\n");
  }

  if (!check_licensed_to(qbuf)) {
    return 1;
  }

  if (!check_global(qbuf)) {
    return 1;
  }

  check_org(qbuf);
  check_align(qbuf);
  check_dcb(qbuf);
  //check_quotates(qbuf);
  check_ats(qbuf);

  asm_o_state = check_delete(qbuf);
  asm_o_state = check_binclude(qbuf, addr);
  asm_o_state = check_include(qbuf, addr);
  asm_o_state = check_rom_end(addr);

  switch (asm_o_state) {
  case asm_out_inc_start: {
    return 1;
  } break;
  case asm_out_del_start: {
    return 1;
  } break;
  case asm_out_bin_start: {
    return 1;
  } break;
  case asm_out_del_end:
  case asm_out_bin_end:
  case asm_out_inc_end: {
    asm_o_state = asm_out_none;
  } break;
  case asm_out_struct_start: {
    gen_file(ofile_type_t::OFILE_ASM, fp, BADADDR, BADADDR, GENFLG_ASMTYPE | GENFLG_ASMINC);
    asm_o_state = asm_out_none;
  } break;
  case asm_out_rom_end: {
    return 1;
  } break;
  }

  print_line(fp, qbuf);

  return 1;
}

typedef struct {
  qstring name;
  qstrvec_t fields;
  qstrvec_t types;
} struct_prep_t;

typedef struct {
  qstring name;
  qstring val;
} equ_t;

static struct_prep_t curr_struct;
static qvector<struct_prep_t> structs;
static qvector<equ_t> equs;

static enum struct_parse_t {
  struct_parse_name,
  struct_parse_fields,
  struct_parse_none,
} s_parse_state;

static struct_parse_t s_parse_name(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_s_parse_name) != true) {
    return s_parse_state;
  }

  curr_struct.name = match.str(1).c_str();

  return struct_parse_fields;
}

static struct_parse_t s_parse_end(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_s_parse_end) != true) {
    return s_parse_state;
  }

  return struct_parse_none;
}

static struct_parse_t s_parse_field(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_s_parse_field) != true) {
    return s_parse_end(line);
  }

  curr_struct.fields.add(match.str(1).c_str());
  curr_struct.types.add(match.str(2).c_str());

  return s_parse_state;
}

static void equ_parse(const qstring& line) {
  std::cmatch match;

  if (std::regex_match(line.c_str(), match, re_equ_parse) != true) {
    return;
  }

  equ_t equ;
  equ.name = match.str(1).c_str();
  equ.val = match.str(2).c_str();
  equs.add(equ);
}

static int idaapi struct_equ_output(FILE* fp, const qstring& line, bgcolor_t prefix_color, bgcolor_t bg_color) {
  qstring qbuf;
  tag_remove(&qbuf, line);

  fix_arrows(qbuf);

  equ_parse(qbuf);

  switch (s_parse_state) {
  case struct_parse_name: {
    s_parse_state = s_parse_name(qbuf);
  } break;
  case struct_parse_fields: {
    s_parse_state = s_parse_field(qbuf);
  } break;
  case struct_parse_none: {
    structs.add(curr_struct);

    curr_struct.name.clear();
    curr_struct.fields.clear();
    curr_struct.types.clear();

    s_parse_state = struct_parse_name;
  } break;
  }

  return 1;
}

static void asm_add_header(FILE* fp) {
  print_line(fp, "    cpu 68000");
  print_line(fp, "    supmode on");
  print_line(fp, "    padding off\n\n");
}

static void asm_add_externs(FILE* fp) {
  print_line(fp, "; ports");
  print_line(fp, "IO_CT1_CTRL equ $A10008");
  print_line(fp, "IO_CT1_DATA equ $A10002");
  print_line(fp, "IO_CT1_RX equ $A1000E");
  print_line(fp, "IO_CT1_SMODE equ $A10012");
  print_line(fp, "IO_CT1_TX equ $A10010");
  print_line(fp, "IO_CT2_CTRL equ $A1000A");
  print_line(fp, "IO_CT2_DATA equ $A10004");
  print_line(fp, "IO_CT2_RX equ $A10014");
  print_line(fp, "IO_CT2_SMODE equ $A10018");
  print_line(fp, "IO_CT2_TX equ $A10016");
  print_line(fp, "IO_EXT_CTRL equ $A1000C");
  print_line(fp, "IO_EXT_DATA equ $A10006");
  print_line(fp, "IO_EXT_RX equ $A1001A");
  print_line(fp, "IO_EXT_SMODE equ $A1001E");
  print_line(fp, "IO_EXT_TX equ $A1001C");
  print_line(fp, "IO_FDC equ $A12000");
  print_line(fp, "IO_PCBVER equ $A10000");
  print_line(fp, "IO_RAMMODE equ $A11000");
  print_line(fp, "IO_TIME equ $A13000");
  print_line(fp, "IO_TMSS equ $A14000");
  print_line(fp, "IO_Z80BUS equ $A11100");
  print_line(fp, "IO_Z80RES equ $A11200");
  print_line(fp, "VDP_CNTR equ $C00008");
  print_line(fp, "VDP_CTRL equ $C00004");
  print_line(fp, "VDP_DATA equ $C00000");
  print_line(fp, "VDP_PSG equ $C00011");
  print_line(fp, "VDP__CNTR equ $C0000A");
  print_line(fp, "VDP__CTRL equ $C00006");
  print_line(fp, "VDP__DATA equ $C00002");
  print_line(fp, "VDP___CNTR equ $C0000C");
  print_line(fp, "VDP____CNTR equ $C0000E");
  print_line(fp, "; ports\n\n\n");
}

static void dump_structures(FILE* fp) {
  if (structs.size() > 0) {
    print_line(fp, "; ------------- structures -------------");
  }

  for each (const auto s in structs) {
    qstring tmp;
    tmp.cat_sprnt("%s struc\n", s.name.c_str());

    for (auto i = 0; i < s.fields.size(); ++i) {
      tmp.cat_sprnt("%s %s\n", s.fields[i].c_str(), s.types[i].c_str());
    }

    tmp.cat_sprnt("%s ends\n", s.name.c_str());

    print_line(fp, tmp);
  }

  if (structs.size() > 0) {
    print_line(fp, "; --------------------------------------\n\n\n");
  }

  structs.clear();
}

static void dump_equals(FILE* fp) {
  if (equs.size() > 0) {
    print_line(fp, "; ------------- equals -------------");
  }

  for each (const auto e in equs) {
    qstring tmp;
    tmp.cat_sprnt("%s equ %s", e.name.c_str(), e.val.c_str());

    print_line(fp, tmp.c_str());
  }

  if (equs.size() > 0) {
    print_line(fp, "; ----------------------------------\n\n\n");
  }

  equs.clear();
}

static void dump_name(FILE* fp, ea_t addr) {
  qstring name = get_name(addr);

  if (!name.empty()) {
    name.cat_sprnt(" equ $%a", addr);
    print_line(fp, name);
  }
}

static void dump_ram_names(FILE* fp) {
  print_line(fp, "; ---------- ram names -------------");

  ea_t ea = 0xFF0000;

  while (ea != BADADDR && ea < 0x1000000) {
    dump_name(fp, ea);
    ea = next_not_tail(ea);
  }

  ea = 0xA00000;

  while (ea != BADADDR && ea < 0xA10000) {
    dump_name(fp, ea);
    ea = next_not_tail(ea);
  }

  print_line(fp, "; ----------------------------------\n\n\n");
}

static ssize_t idaapi process_asm_output(void* user_data, int notification_code, va_list va) {
  switch (notification_code) {
  case processor_t::ev_gen_asm_or_lst: {
    bool starting = va_arg(va, bool);
    FILE* fp = va_arg(va, FILE*);
    bool is_asm = va_arg(va, bool);
    int flags = va_arg(va, int);
    html_line_cb_t** outline = va_arg(va, html_line_cb_t**);

    if (is_asm) {
      if (starting) {
        *outline = struct_equ_output;
        s_parse_state = struct_parse_name;
      }
      else {
        dump_structures(fp);
        dump_equals(fp);
        dump_ram_names(fp);
      }
      break;
    }

    if (starting) {
      patch_offset = dirty_win_hack();
      *outline = line_output;

      qstring tmp;
      idc_value_t rv;
      tmp.sprnt("process_config_line(\"%s\")", "OPCODE_BYTES=0");
      eval_idc_expr(&rv, BADADDR, tmp.c_str());

      asm_add_header(fp);
      asm_add_externs(fp);
      asm_o_state = asm_out_struct_start;
    }
    else {
      undo_dirty_win_hack(patch_offset);
      patch_offset = NULL;
    }
  } break;
  }

  return 0;
}

static ssize_t idaapi hook_disasm(void* user_data, int notification_code, va_list va)
{
  switch (notification_code)
  {
  case processor_t::ev_ana_insn:
  {
    insn_t* out = va_arg(va, insn_t*);

    uchar b = get_byte(out->ea);

    if (b == 0xA0 || b == 0xF0)
    {
      switch (b)
      {
      case 0xA0:
        out->itype = M68K_linea;
        out->Op1.addr = get_dword(0x0A * sizeof(uint32));
        break;
      case 0xF0:
        out->itype = M68K_linef;
        out->Op1.addr = get_dword(0x0B * sizeof(uint32));
        break;
      }

      out->size = 2;

      out->Op1.type = o_near;
      out->Op1.offb = 1;
      out->Op1.dtype = dt_dword;
      out->Op1.phrase = 0x0A;
      out->Op1.specflag1 = 2;

      out->Op2.type = o_imm;
      out->Op2.offb = 1;
      out->Op2.dtype = dt_byte;
      out->Op2.value = get_byte(out->ea + 1);

      return out->size;
    }
  } break;
  case processor_t::ev_emu_insn:
  {
    insn_t* insn = va_arg(va, insn_t*);

    if (insn->itype == M68K_linea || insn->itype == M68K_linef)
    {
      insn->add_cref(insn->Op1.addr, 0, fl_CN);
      insn->add_cref(insn->ea + insn->size, insn->Op1.offb, fl_F);
      return 1;
    }
  } break;
  case processor_t::ev_out_mnem:
  {
    outctx_t* outctx = va_arg(va, outctx_t*);
    if (outctx->insn.itype == M68K_linea || outctx->insn.itype == M68K_linef)
    {
      outctx->out_custom_mnem((outctx->insn.itype == M68K_linef) ? "line_f" : "line_a");
      return 1;
    }
  } break;
  default:
    notification_code = notification_code;
  }

  return 0;
}

struct m68k_events_visitor_t : public post_event_visitor_t
{
  ssize_t idaapi handle_post_event(ssize_t code, int notification_code, va_list va) override
  {
    switch (notification_code)
    {
    case processor_t::ev_ana_insn:
    {
      insn_t* out = va_arg(va, insn_t*);

#ifdef _DEBUG
      print_insn(out);
#endif

      for (int i = 0; i < UA_MAXOP; ++i)
      {
        op_t& op = out->ops[i];

#ifdef _DEBUG
        print_op(out->ea, &op);
#endif

        switch (op.type)
        {
        case o_near:
        case o_mem:
        {
          if (op.addr >= 0xFFFF0000 && op.addr <= 0xFFFFFFFF) {
            op.addr &= 0xFFFFFF;
          }

          if ((op.addr & 0xFFE00000) == 0xE00000) { // RAM mirrors
            op.addr |= 0x1F0000;
          }

          if ((op.addr >= 0xC00000 && op.addr <= 0xC0001F) ||
            (op.addr >= 0xC00020 && op.addr <= 0xC0003F)) { // VDP mirrors
            op.addr &= 0xC000FF;
          }

          if (out->itype == 0x75 && op.n == 0 && op.phrase == 9 && out->size == 6) { // jsr (label).l
            op.type = o_mem;
          }
          else if ((out->itype == 0x76 || out->itype == 0x75 || out->itype == 0x74) && op.n == 0 &&
            (op.phrase == 0x09 || op.phrase == 0x0A) &&
            (op.addr != 0 && op.addr <= 0xA00000) &&
            (op.specflag1 == 2 || op.specflag1 == 3)) { // lea table(pc),Ax; jsr func(pc); jmp label(pc)
            short diff = op.addr - out->ea;
            if (diff >= SHRT_MIN && diff <= SHRT_MAX)
            {
              out->Op1.type = o_displ;
              out->Op1.offb = 2;
              out->Op1.dtype = dt_dword;
              out->Op1.phrase = 0x5B;
              out->Op1.specflag1 = 0x10;
            }
          }
        } break;
        }
      }

      return out->size;
    } break;
    case processor_t::ev_emu_insn:
    {
      insn_t* insn = va_arg(va, insn_t*);
      if (insn->itype == 0xB6) // trap #X
      {
        qstring name;
        ea_t trap_addr = get_dword((0x20 + (insn->Op1.value & 0xF)) * sizeof(uint32));
        get_func_name(&name, trap_addr);
        set_cmt(insn->ea, name.c_str(), false);
        insn->add_cref(trap_addr, insn->Op1.offb, fl_CN);

        if (func_does_return(trap_addr)) {
          func_t* trap_func = get_func(trap_addr);
          int argsize = (trap_func != nullptr) ? trap_func->argsize : 0;
          insn->add_cref(insn->ea + 2 + argsize, 0, fl_F); // calc next insn
        }

        return 1;
      }

      if ((insn->itype == 0x76 || insn->itype == 0x75 || insn->itype == 0x74) &&
        insn->Op1.phrase == 0x5B && insn->Op1.specflag1 == 0x10) // lea table(pc),Ax; jsr func(pc); jmp label(pc)
      {
        short diff = insn->Op1.addr - insn->ea;
        if (diff >= SHRT_MIN && diff <= SHRT_MAX)
        {
          insn->add_dref(insn->Op1.addr, insn->Op1.offb, dr_O);

          if (insn->itype != 0x74) {
            insn->add_cref(insn->ea + insn->size, 0, fl_F);
          }

          return 1;
        }
      }

      if (insn->itype == 0x7F && insn->Op1.type == o_imm && insn->Op1.value & 0xFF0000 && insn->Op1.dtype == dt_word) { // movea
        insn->Op1.value &= 0xFFFF;

        op_offset(insn->ea, insn->Op1.n, REF_OFF32, BADADDR, 0xFF0000);
        insn->add_cref(insn->ea + insn->size, insn->Op1.offb, fl_F);
        return 1;
      }
    } break;
    case processor_t::ev_get_idd_opinfo:
    {
      idd_opinfo_t* opinf = va_arg(va, idd_opinfo_t*);
      ea_t ea = va_arg(va, ea_t);
      int n = va_arg(va, int);
      int thread_id = va_arg(va, int);
      processor_t::regval_getter_t* getreg = va_arg(va, processor_t::regval_getter_t*);
      const regval_t* regvalues = va_arg(va, const regval_t*);

      opinf->ea = BADADDR;
      opinf->debregidx = 0;
      opinf->modified = false;
      opinf->value.ival = 0;
      opinf->value_size = 4;

      insn_t out;
      if (decode_insn(&out, ea))
      {
        op_t op = out.ops[n];

#ifdef _DEBUG
        print_insn(&out);
#endif

        int size = 0;
        switch (op.dtype)
        {
        case dt_byte:
          size = 1;
          break;
        case dt_word:
          size = 2;
          break;
        default:
          size = 4;
          break;
        }

        opinf->value_size = size;

        switch (op.type)
        {
        case o_mem:
        case o_near:
        case o_imm:
        {
          flags_t flags;

          switch (n)
          {
          case 0: flags = get_optype_flags0(get_flags(ea)); break;
          case 1: flags = get_optype_flags1(get_flags(ea)); break;
          default: flags = 0; break;
          }

          switch (op.type)
          {
          case o_mem:
          case o_near: opinf->ea = op.addr; break;
          case o_imm: opinf->ea = op.value; break;
          }

          opinfo_t info;
          if (get_opinfo(&info, ea, n, flags) != NULL)
          {
            opinf->ea += info.ri.base;
          }
        } break;
        case o_phrase:
        case o_reg:
        {
          int reg_idx = idp_to_dbg_reg(op.reg);
          regval_t reg = getreg(dbg->regs(reg_idx).name, regvalues);

          if (op.phrase >= 0x10 && op.phrase <= 0x1F || // (A0)..(A7), (A0)+..(A7)+
            op.phrase >= 0x20 && op.phrase <= 0x27) // -(A0)..-(A7)
          {
            if (op.phrase >= 0x20 && op.phrase <= 0x27)
              reg.ival -= size;

            opinf->ea = (ea_t)reg.ival;
            size_t read_size = 0;

            switch (size)
            {
            case 1:
            {
              uint8_t b = 0;
              dbg->read_memory(&read_size, (ea_t)reg.ival, &b, 1);
              opinf->value.ival = b;
            } break;
            case 2:
            {
              uint16_t w = 0;
              dbg->read_memory(&read_size, (ea_t)reg.ival, &w, 2);
              w = swap16(w);
              opinf->value.ival = w;
            } break;
            default:
            {
              uint32_t l = 0;
              dbg->read_memory(&read_size, (ea_t)reg.ival, &l, 4);
              l = swap32(l);
              opinf->value.ival = l;
            } break;
            }
          }
          else
            opinf->value = reg;

          opinf->debregidx = reg_idx;
        } break;
        case o_displ:
        {
          regval_t main_reg, add_reg;
          int main_reg_idx = idp_to_dbg_reg(op.reg);
          int add_reg_idx = idp_to_dbg_reg(op.specflag1 & 0xF);

          main_reg.ival = 0;
          add_reg.ival = 0;
          if (op.specflag2 & 0x10)
          {
            add_reg = getreg(dbg->regs(add_reg_idx).name, regvalues);
            if (op.specflag1 & 0x10)
            {
              add_reg.ival &= 0xFFFF;
              add_reg.ival = (uint64)((int16_t)add_reg.ival);
            }
          }

          if (main_reg_idx != 16)
            main_reg = getreg(dbg->regs(main_reg_idx).name, regvalues);

          ea_t addr = (ea_t)main_reg.ival + op.addr + (ea_t)add_reg.ival;
          opinf->ea = addr;
          size_t read_size = 0;

          switch (size)
          {
          case 1:
          {
            uint8_t b = 0;
            dbg->read_memory(&read_size, addr, &b, 1);
            opinf->value.ival = b;
          } break;
          case 2:
          {
            uint16_t w = 0;
            dbg->read_memory(&read_size, addr, &w, 2);
            w = swap16(w);
            opinf->value.ival = w;
          } break;
          default:
          {
            uint32_t l = 0;
            dbg->read_memory(&read_size, addr, &l, 4);
            l = swap32(l);
            opinf->value.ival = l;
          } break;
          }
        } break;
        }

        if (opinf->ea >= 0xFFFF0000 && opinf->ea <= 0xFFFFFFFF) {
          opinf->ea &= 0xFFFFFF;
        }

        return 1;
      }
    } break;
      case processor_t::ev_out_operand: {
        outctx_t* outctx = va_arg(va, outctx_t*);
        const op_t* op = va_arg(va, const op_t*);

        if (
          op->type == o_displ &&
          (((op->phrase == 0x0A) || (op->phrase == 0x0B) || (op->phrase == 0x08) || (op->phrase == 0x09)) && op->addr == 0)
          ) {
          qstring tmp;

          tmp.append(COLSTR("0", SCOLOR_NUMBER));
          tmp.append(COLSTR("(", SCOLOR_SYMBOL) SCOLOR_ON SCOLOR_REG "a");
          tmp.append(0x28 + op->reg);
          tmp.append(SCOLOR_OFF SCOLOR_REG);

          size_t pos = outctx->outbuf.find(tmp.c_str());

          if (pos != qstring::npos) {
            outctx->outbuf.insert(pos + 5, COLSTR(".w", SCOLOR_SYMBOL));
          }
        }
      } break;
    default:
    {
#ifdef _DEBUG
      if (my_dbg)
      {
        qstring p;
        p.sprnt("%d\n", notification_code);
        OutputDebugStringA(p.c_str());
      }
#endif
    } break;
    }
    return code;
  }
} ctx;

//--------------------------------------------------------------------------
static unsigned int mask(unsigned char bit_idx, unsigned char bits_cnt = 1)
{
  return (((1 << bits_cnt) - 1) << bit_idx);
}

//--------------------------------------------------------------------------
static bool is_vdp_send_cmd(uint32 val)
{
  if (val & 0xFFFF0000)
  {
    return ((val & 0x9F000000) >= 0x80000000) && ((val & 0x9F000000) <= 0x97000000);
  }
  else
  {
    return ((val & 0x9F00) >= 0x8000) && ((val & 0x9F00) <= 0x9700);
  }
}

//-------------------------------------------------------------------------
static bool is_call16_const_cmd(uint32 val)
{
  return (val & 0xFFFF0000) == 0x4EB80000;
}

//--------------------------------------------
static void do_call16_const(ea_t ea, uint32 val)
{
  insn_t insn;
  decode_insn(&insn, ea);

  insn_add_dref(insn, val, 2, dr_O);

  char buf[MAXSTR];
  ::qsnprintf(buf, MAXSTR, "jsr 0x%.4X", val);
  append_cmt(ea, buf, false);
}

//--------------------------------------------------------------------------
static bool is_vdp_rw_cmd(uint32 val)
{
  if (val & 0xFFFF0000) // command was sended by one dword
  {
    switch ((val >> 24) & mask(6, 2))
    {
    case 0 /*00*/ << 6:
    case 1 /*01*/ << 6:
    case 3 /*11*/ << 6:
    {
      switch ((val & 0xFF) & mask(4, 2))
      {
      case 0 /*00*/ << 4:
      case 1 /*01*/ << 4:
      case 2 /*10*/ << 4:
      {
        return true;
      }
      }
      return false;
    }
    }
    return false;
  }
  else // command was sended by halfs (this is high word of it)
  {
    switch ((val >> 8) & mask(6, 2))
    {
    case 0 /*00*/ << 6:
    case 1 /*01*/ << 6:
    case 3 /*11*/ << 6:
    {
      return true;
    }
    }
    return false;
  }
}

static const char wrong_vdp_cmd[] = "Wrong command to send to VDP_CTRL!\n";

//--------------------------------------------------------------------------
static bool do_cmt_vdp_reg_const(ea_t ea, uint32 val)
{
  if (!val) return false;

  char name[250];
  unsigned int addr = 0;
  switch (val & 0x9F00)
  {
  case 0x8000:
  {
    if (val & mask(0))  append_cmt(ea, "DISPLAY_OFF", false);
    else append_cmt(ea, "DISPLAY_ON", false);

    if (val & mask(1))  append_cmt(ea, "PAUSE_HV_WHEN_EXT_INT", false);
    else append_cmt(ea, "NORMAL_HV_COUNTER", false);

    if (val & mask(2))  append_cmt(ea, "EIGHT_COLORS_MODE", false);
    else append_cmt(ea, "FULL_COLORS_MODE", false);

    if (val & mask(4))  append_cmt(ea, "ENABLE_HBLANK", false);
    else append_cmt(ea, "DISABLE_HBLANK", false);

    return true;
  }
  case 0x8100:
  {
    if (val & mask(2))  append_cmt(ea, "GENESIS_DISPLAY_MODE_BIT2", false);
    else append_cmt(ea, "SMS_DISPLAY_MODE_BIT2", false);

    if (val & mask(3))  append_cmt(ea, "SET_PAL_MODE", false);
    else append_cmt(ea, "SET_NTSC_MODE", false);

    if (val & mask(4))  append_cmt(ea, "ENABLE_DMA", false);
    else append_cmt(ea, "DISABLE_DMA", false);

    if (val & mask(5))  append_cmt(ea, "ENABLE_VBLANK", false);
    else append_cmt(ea, "DISABLE_VBLANK", false);

    if (val & mask(6))  append_cmt(ea, "ENABLE_DISPLAY", false);
    else append_cmt(ea, "DISABLE_DISPLAY", false);

    if (val & mask(7))  append_cmt(ea, "TMS9918_DISPLAY_MODE_BIT7", false);
    else append_cmt(ea, "GENESIS_DISPLAY_MODE_BIT7", false);

    return true;
  }
  case 0x8200:
  {
    addr = (val & mask(3, 3));
    ::qsnprintf(name, sizeof(name), "SET_PLANE_A_ADDR_$%.4X", addr * 0x400);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x8300:
  {
    addr = (val & mask(1, 5));
    ::qsnprintf(name, sizeof(name), "SET_WINDOW_PLANE_ADDR_$%.4X", addr * 0x400);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x8400:
  {
    addr = (val & mask(0, 3));
    ::qsnprintf(name, sizeof(name), "SET_PLANE_B_ADDR_$%.4X", addr * 0x2000);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x8500:
  {
    addr = (val & mask(0, 7));
    ::qsnprintf(name, sizeof(name), "SET_SPRITE_TBL_ADDR_$%.4X", addr * 0x200);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x8600:
  {
    if (val & mask(5))  append_cmt(ea, "ENABLE_SPRITES_REBASE", false);
    else append_cmt(ea, "DISABLE_SPRITES_REBASE", false);

    return true;
  }
  case 0x8700:
  {
    unsigned int xx = (val & mask(4, 2));
    unsigned int yyyy = (val & mask(0, 4));

    ::qsnprintf(name, sizeof(name), "SET_BG_AS_%dPAL_%dTH_COLOR", xx + 1, yyyy + 1);
    append_cmt(ea, name, false);

    return true;
  }
  case 0x8A00:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_HBLANK_COUNTER_VALUE_$%.4X", addr);
    append_cmt(ea, name, false);
    return true;
  } break;
  case 0x8B00:
  {
    switch (val & mask(0, 2))
    {
    case 0 /*00*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_FULLSCREEN", false); break;
    case 1 /*01*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_LINE_SCROLL", false); break;
    case 2 /*10*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_CELL_SCROLL", false); break;
    case 3 /*11*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_LINE__SCROLL", false); break;
    }

    if (val & mask(2))  append_cmt(ea, "_2CELLS_COLUMN_VSCROLL_MODE", false);
    else append_cmt(ea, "FULLSCREEN_VSCROLL_MODE", false);

    if (val & mask(3))  append_cmt(ea, "ENABLE_EXT_INTERRUPT", false);
    else append_cmt(ea, "DISABLE_EXT_INTERRUPT", false);

    return true;
  }
  case 0x8C00:
  {
    switch (val & 0x81)
    {
    case 0 /*0XXXXXX0*/: append_cmt(ea, "SET_40_TILES_WIDTH_MODE", false); break;
    case 0x81 /*1XXXXXX1*/: append_cmt(ea, "SET_32_TILES_WIDTH_MODE", false); break;
    }

    if (val & mask(3)) append_cmt(ea, "ENABLE_SHADOW_HIGHLIGHT_MODE", false);
    else append_cmt(ea, "DISABLE_SHADOW_HIGHLIGHT_MODE", false);

    switch (val & mask(1, 2))
    {
    case 0 /*00*/ << 1: append_cmt(ea, "NO_INTERLACE_MODE", false); break;
    case 1 /*01*/ << 1: append_cmt(ea, "ENABLE_SIMPLE_INTERLACE_MODE", false); break;
    case 3 /*11*/ << 1: append_cmt(ea, "ENABLE_DOUBLE_INTERLACE_MODE", false); break;
    }

    if (val & mask(4)) append_cmt(ea, "ENABLE_EXTERNAL_PIXEL_BUS", false);
    else append_cmt(ea, "DISABLE_EXTERNAL_PIXEL_BUS", false);

    if (val & mask(6)) append_cmt(ea, "DO_PIXEL_CLOCK_INSTEAD_OF_VSYNC", false);
    else append_cmt(ea, "DO_VSYNC_INSTEAD_OF_PIXEL_CLOCK", false);

    return true;
  }
  case 0x8D00:
  {
    addr = (val & mask(0, 6));
    ::qsnprintf(name, sizeof(name), "SET_HSCROLL_DATA_ADDR_$%.4X", addr * 0x400);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x8E00:
  {
    if (val & mask(0))  append_cmt(ea, "ENABLE_PLANE_A_REBASE", false);
    else append_cmt(ea, "DISABLE_PLANE_A_REBASE", false);

    if (val & mask(4))  append_cmt(ea, "ENABLE_PLANE_B_REBASE", false);
    else append_cmt(ea, "DISABLE_PLANE_B_REBASE", false);

    return true;
  }
  case 0x8F00:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_VDP_AUTO_INC_VALUE_$%.4X", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9000:
  {
    switch (val & mask(0, 2))
    {
    case 0 /*00*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_32_TILES", false); break;
    case 1 /*01*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_64_TILES", false); break;
    case 3 /*11*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_128_TILES", false); break;
    }

    switch (val & mask(4, 2))
    {
    case 0 /*00*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_32_TILES", false); break;
    case 1 /*01*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_64_TILES", false); break;
    case 3 /*11*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_128_TILES", false); break;
    }

    return true;
  }
  case 0x9100:
  {
    if (val & mask(7)) append_cmt(ea, "MOVE_WINDOW_HORZ_RIGHT", false);
    else append_cmt(ea, "MOVE_WINDOW_HORZ_LEFT", false);

    addr = (val & mask(0, 5));
    ::qsnprintf(name, sizeof(name), "MOVE_BY_%d_CELLS", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9200:
  {
    if (val & mask(7)) append_cmt(ea, "MOVE_WINDOW_VERT_RIGHT", false);
    else append_cmt(ea, "MOVE_WINDOW_VERT_LEFT", false);

    addr = (val & mask(0, 5));
    ::qsnprintf(name, sizeof(name), "MOVE_BY_%d_CELLS", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9300:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_LOWER_BYTE_OF_DMA_LEN_TO_$%.2X", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9400:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_HIGHER_BYTE_OF_DMA_LEN_TO_$%.2X", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9500:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_LOWER_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9600:
  {
    addr = (val & mask(0, 8));
    ::qsnprintf(name, sizeof(name), "SET_MIDDLE_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
    append_cmt(ea, name, false);
    return true;
  }
  case 0x9700:
  {
    addr = (val & mask(0, 6));
    ::qsnprintf(name, sizeof(name), "SET_HIGH_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
    append_cmt(ea, name, false);

    if (val & mask(7)) append_cmt(ea, "ADD_$800000_TO_DMA_SRC_ADDR", false);
    else append_cmt(ea, "SET_COPY_M68K_TO_VRAM_DMA_MODE", false);

    switch (val & mask(6, 2))
    {
    case 2 /*10*/ << 6: append_cmt(ea, "SET_VRAM_FILL_DMA_MODE", false); break;
    case 3 /*11*/ << 6: append_cmt(ea, "SET_VRAM_COPY_DMA_MODE", false); break;
    }

    return true;
  }
  default:
  {
    msg(wrong_vdp_cmd);
    return false;
  }
  }
}

//--------------------------------------------------------------------------
static void do_cmt_sr_ccr_reg_const(ea_t ea, uint32 val)
{
  if (val & mask(4)) append_cmt(ea, "SET_X", false);
  else append_cmt(ea, "CLR_X", false);

  if (val & mask(3)) append_cmt(ea, "SET_N", false);
  else append_cmt(ea, "CLR_N", false);

  if (val & mask(2)) append_cmt(ea, "SET_Z", false);
  else append_cmt(ea, "CLR_Z", false);

  if (val & mask(1)) append_cmt(ea, "SET_V", false);
  else append_cmt(ea, "CLR_V", false);

  if (val & mask(0)) append_cmt(ea, "SET_C", false);
  else append_cmt(ea, "CLR_C", false);

  if (val & mask(15)) append_cmt(ea, "SET_T1", false);
  else append_cmt(ea, "CLR_T1", false);

  if (val & mask(14)) append_cmt(ea, "SET_T0", false);
  else append_cmt(ea, "CLR_T0", false);

  if (val & mask(13)) append_cmt(ea, "SET_SF", false);
  else append_cmt(ea, "CLR_SF", false);

  if (val & mask(12)) append_cmt(ea, "SET_MF", false);
  else append_cmt(ea, "CLR_MF", false);

  switch ((val & mask(8, 3)))
  {
  case 0x7 /*111*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS", false); break;
  case 0x6 /*110*/ << 8: append_cmt(ea, "ENABLE_NO_INTERRUPTS", false); break;

  case 0x5 /*101*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS_EXCEPT_VBLANK", false); break;
  case 0x4 /*100*/ << 8: append_cmt(ea, "ENABLE_ONLY_VBLANK_INTERRUPT", false); break;

  case 0x3 /*011*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS_EXCEPT_VBLANK_HBLANK", false); break;
  case 0x2 /*010*/ << 8: append_cmt(ea, "ENABLE_ONLY_VBLANK_HBLANK_INTERRUPTS", false); break;

  case 0x1 /*001*/ << 8: append_cmt(ea, "DISABLE_NO_INTERRUPTS", false); break;
  case 0x0 /*000*/ << 8: append_cmt(ea, "ENABLE_ALL_INTERRUPTS", false); break;
  }
}

//--------------------------------------------------------------------------
static void do_cmt_vdp_rw_command(ea_t ea, uint32 val)
{
  char name[250];

  if (val & 0xFFFF0000) // command was sended by one dword
  {
    unsigned int addr = ((val & mask(0, 2)) << 14) | ((val & mask(16, 14)) >> 16);

    switch ((val >> 24) & mask(6))
    {
    case 0 << 6: // read operation
    {
      switch (val & ((1 << 31) | (1 << 5) | (1 << 4)))
      {
      case ((0 << 31) | (0 << 5) | (0 << 4)) /*000*/: // VRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_READ_VRAM_FROM_$%.4X", addr);
        append_cmt(ea, name, false);
      } break;
      case ((0 << 31) | (0 << 5) | (1 << 4)) /*001*/: // VSRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_READ_VSRAM_FROM_$%.4X", addr);
        append_cmt(ea, name, false);
      } break;
      case ((0 << 31) | (1 << 5) | (0 << 4)) /*010*/: // CRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_READ_CRAM_FROM_$%.4X", addr);
        append_cmt(ea, name, false);
      } break;
      default:
      {
        msg(wrong_vdp_cmd);
      } break;
      }
    } break;
    case 1 << 6: // write operation
    {
      switch (val & ((1 << 31) | (1 << 5) | (1 << 4)))
      {
      case ((0 << 31) | (0 << 5) | (0 << 4)) /*000*/: // VRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_VRAM_AT_$%.4X_ADDR", addr);
        append_cmt(ea, name, false);
      } break;
      case ((0 << 31) | (0 << 5) | (1 << 4)) /*001*/: // VSRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_VSRAM_AT_$%.4X_ADDR", addr);
        append_cmt(ea, name, false);
      } break;
      case ((1 << 31) | (0 << 5) | (0 << 4)) /*100*/: // CRAM
      {
        ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_CRAM_AT_$%.4X_ADDR", addr);
        append_cmt(ea, name, false);
      } break;
      default:
      {
        msg(wrong_vdp_cmd);
      } break;
      }
    } break;
    default:
    {
      msg(wrong_vdp_cmd);
    } break;
    }
  }
  else // command was sended by halfs (this is high word of it)
  {
    switch ((val >> 8) & mask(6, 2))
    {
    case 0 /*00*/ << 6: append_cmt(ea, "VRAM_OR_VSRAM_OR_CRAM_READ_MODE", false); break;
    case 1 /*01*/ << 6: append_cmt(ea, "VRAM_OR_VSRAM_WRITE_MODE", false); break;
    case 3 /*11*/ << 6: append_cmt(ea, "CRAM_WRITE_MODE", false); break;
    }
  }

  if (val & mask(6)) append_cmt(ea, "VRAM_COPY_DMA_MODE", false);

  if (val & mask(7)) append_cmt(ea, "DO_OPERATION_USING_DMA", false);
  else append_cmt(ea, "DO_OPERATION_WITHOUT_DMA", false);
}

//--------------------------------------------------------------------------
static void do_cmt_z80_bus_command(ea_t ea, ea_t addr, uint32 val)
{
  switch (addr)
  {
  case 0xA11100: // IO_Z80BUS
  {
    switch (val)
    {
    case 0x0: append_cmt(ea, "Give the Z80 the bus back", false); break;
    case 0x100: append_cmt(ea, "Send the Z80 a bus request", false); break;
    }
  } break;
  case 0xA11200: // IO_Z80RES
  {
    switch (val)
    {
    case 0x0: append_cmt(ea, "Disable the Z80 reset", false); break;
    case 0x100: append_cmt(ea, "Reset the Z80", false); break;
    }
  } break;
  }
}
#endif

//--------------------------------------------------------------------------
static void print_version()
{
  static const char format[] = NAME " debugger plugin v%s;\nAuthor: DrMefistO [Lab 313] <newinferno@gmail.com>.";
  info(format, VERSION);
  msg(format, VERSION);
}

//--------------------------------------------------------------------------
// Initialize debugger plugin
static bool init_plugin(void)
{
#ifdef DEBUG_68K
  if (ph.id != PLFM_68K)
#else
  if (ph.id != PLFM_Z80)
#endif
    return false;

  return true;
}

#ifdef DEBUG_68K
struct smd_constant_action_t : public action_handler_t
{
  virtual int idaapi activate(action_activation_ctx_t* ctx)
  {
    qstring name;
    ea_t ea = get_screen_ea();
    if (is_mapped(ea)) // address belongs to disassembly
    {
      if (get_cmt(&name, ea, false) != -1) // remove previous comment and exit
      {
        set_cmt(ea, "", false);
        return 1;
      }

      insn_t out;
      decode_insn(&out, ea);
      print_operand(&name, ea, 1);
      tag_remove(&name, name);

      uval_t val = 0;
      get_immvals(&val, ea, 0, get_flags(ea));
      uint32 value = (uint32)val;
      if (out.Op1.type == o_imm && out.Op2.type == o_reg && !::qstrcmp(name.c_str(), "sr"))
      {
        do_cmt_sr_ccr_reg_const(ea, value);
      }
      else if (out.Op1.type == o_imm && out.Op2.type == o_mem &&
        (out.Op2.addr == 0xA11200 || out.Op2.addr == 0xA11100))
      {
        do_cmt_z80_bus_command(ea, out.Op2.addr, value);
      }
      else if (is_call16_const_cmd(value))
      {
        do_call16_const(ea, value & 0xFFFF);
      }
      else if (is_vdp_rw_cmd(value))
      {
        do_cmt_vdp_rw_command(ea, value);
      }
      else if (is_vdp_send_cmd(value)) // comment set vdp reg cmd
      {
        do_cmt_vdp_reg_const(ea, value);
        do_cmt_vdp_reg_const(ea, value >> 16);
      }
    }
    return 1;
  }

  virtual action_state_t idaapi update(action_update_ctx_t* ctx)
  {
    return AST_ENABLE_ALWAYS;
  }
};

static const char smd_constant_name[] = "gensida:smd_constant";
static smd_constant_action_t smd_constant;
static action_desc_t smd_constant_action = ACTION_DESC_LITERAL(smd_constant_name, "Identify SMD constant", &smd_constant, "J", NULL, -1);

//--------------------------------------------------------------------------
static ssize_t idaapi hook_ui(void* user_data, int notification_code, va_list va)
{
  if (notification_code == ui_populating_widget_popup)
  {
    TWidget* widget = va_arg(va, TWidget*);

    if (get_widget_type(widget) == BWN_DISASM)
    {
      TPopupMenu* p = va_arg(va, TPopupMenu*);
      attach_action_to_popup(widget, p, smd_constant_name);
    }
  }

  return 0;
}
#endif

//--------------------------------------------------------------------------
// Initialize debugger plugin
static plugmod_t* idaapi init(void)
{
  if (init_plugin())
  {
    plugin_inited = true;
    dbg_started = false;
    my_dbg = false;

#ifdef DEBUG_68K
    bool res = register_action(smd_constant_action);

    hook_to_notification_point(HT_UI, hook_ui, NULL);
    hook_to_notification_point(HT_IDP, hook_disasm, nullptr);
    hook_to_notification_point(HT_IDP, process_asm_output, nullptr);
    register_post_event_visitor(HT_IDP, &ctx, nullptr);
    hook_to_notification_point(HT_DBG, hook_dbg, NULL);
#endif

    print_version();

    dbg = &debugger; // temporary workaround for a dbg pointer absence
    return PLUGIN_KEEP;
  }
  return PLUGIN_SKIP;
}

//--------------------------------------------------------------------------
// Terminate debugger plugin
static void idaapi term(void)
{
  if (plugin_inited)
  {
#ifdef DEBUG_68K
    unhook_from_notification_point(HT_UI, hook_ui);
    unregister_post_event_visitor(HT_IDP, &ctx);
    unhook_from_notification_point(HT_IDP, process_asm_output);
    unhook_from_notification_point(HT_IDP, hook_disasm);
    unhook_from_notification_point(HT_DBG, hook_dbg);

    unregister_action(smd_constant_name);
#endif

    plugin_inited = false;
    dbg_started = false;

    if (dbg == &debugger) {
      dbg = nullptr;
    }
  }
}

//--------------------------------------------------------------------------
// The plugin method - usually is not used for debugger plugins
static bool idaapi run(size_t arg) { return false; }

//--------------------------------------------------------------------------
char comment[] = NAME " debugger plugin by DrMefistO.";

char help[] =
NAME " debugger plugin by DrMefistO.\n"
"\n"
"This module lets you debug "
#ifdef DEBUG_68K
"Genesis roms "
#else
"Z80 Sound Drivers "
#endif
"in IDA.\n";

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
    IDP_INTERFACE_VERSION,
    PLUGIN_PROC | PLUGIN_DBG | PLUGIN_MOD, // plugin flags
    init, // initialize

    term, // terminate. this pointer may be NULL.

    run, // invoke plugin

    comment, // long comment about the plugin
    // it could appear in the status line
    // or as a hint

help, // multiline help about the plugin

NAME " debugger plugin", // the preferred short name of the plugin

"" // the preferred hotkey to run the plugin
};