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

#include <dbg.hpp>
#include <expr.hpp>
#include <ida.hpp>
#include <idd.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <name.hpp>
#include <offset.hpp>

#include "ida_plugin.h"
#include "ida_registers.h"

#ifdef DEBUG_68K
#include "paintform.h"
#endif

#include <regex>

extern debugger_t debugger;

static bool plugin_inited;
static bool dbg_started;
static bool my_dbg;

#ifdef DEBUG_68K
static ssize_t idaapi hook_dbg(void* user_data, int notification_code, va_list va)
{
  switch (notification_code)
  {
  case dbg_notification_t::dbg_process_start: {
    dbg_started = true;
  } break;

  case dbg_notification_t::dbg_process_exit: {
    dbg_started = false;
  } break;
  }
  return 0;
}

static int idaapi idp_to_dbg_reg(int idp_reg)
{
  int reg_idx = idp_reg;
  if (idp_reg >= 0 && idp_reg <= 7)
    reg_idx = R_D0 + idp_reg;
  else if (idp_reg >= 8 && idp_reg <= 39)
    reg_idx = R_A0 + (idp_reg % 8) * 2;
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
static enum asm_out_state {
  asm_out_none,
  asm_out_del_start,
  asm_out_del_end,
  asm_out_bin,
  asm_out_inc_start,
  asm_out_inc_end,
  asm_out_structs,
} asm_o_state;

static enum out_asm_t {
  asm_as,
  asm_vasm,
  asm_asm68k,
} assembler;

#define ASM_SPACE "    "
#define ASM_AS_BIN "binclude"
#define ASM_VASM_BIN "incbin"
#define ASM_ASM68K_BIN ASM_VASM_BIN
#define ASM_AS_ALIGN2 "align 2,0"
#define ASM_VASM_ALIGN2 "align 1"
#define ASM_ASM68K_ALIGN2 "even"

#define ASM_AS_LINK "http://john.ccac.rwth-aachen.de:8000/as/index.html"
#define ASM_VASM_LINK "http://sun.hasenbraten.de/vasm/index.php?view=main"
#define ASM_ASM68K_LINK "https://info.sonicretro.org/File:ASM68k.7z"

static ea_t bin_end = BADADDR;
static ea_t inc_start = BADADDR;
static qstring inc_path;
static qstrvec_t inc_listing;
static ea_t last_not_unkn = BADADDR;
static bool skip_unused = false;
static bool dont_delete = false;
static ea_t rom_end = BADADDR;

static const std::regex re_check_ats("^((?:\\w*@+\\w*)+):");
static const std::regex re_check_global("^[ \\t]+global[ \\t]+\\w+$");
static const std::regex re_check_binclude("^BIN \"(.+)\"");
static const std::regex re_check_include("^INC_START \"(.+)\"");
static const std::regex re_check_subroutine("^; =+ S U B R O U T I N E =+$");
static const std::regex re_check_licensed_to("^; \\|[ \\t]+License.+$");
static const std::regex re_check_org("^ORG[ \\t]+(\\$[0-9a-fA-F]+)$");
static const std::regex re_check_align("^[ \\t]+align[ \\t]+2$");
static const std::regex re_check_dcb_xx("^[ \\t]+dcb\\.b[ \\t]+(\\$?\\w+),(?:[ \\t]+)?(\\$?\\w+)");
//static const std::regex re_check_quotates("([^#'])'(.+?)'");
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

static asm_out_state check_delete(qstring& line, bool& skip) {
  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none && asm_o_state != asm_out_del_start) {
    return asm_o_state;
  }

  if (line.find("DEL_START") != qstring::npos) {
    line.clear();
    skip = dont_delete;
    return dont_delete ? asm_o_state : asm_out_del_start;
  }
  else if (line.find("DEL_END") != qstring::npos) {
    line.clear();
    skip = dont_delete;
    return dont_delete ? asm_o_state : asm_out_del_end;
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

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return asm_o_state;
  }

  if (std::regex_match(line.c_str(), match, re_check_binclude) == true) {
    asize_t size = get_item_size(addr);

    bin_end = addr + size;

    uint8_t* tmp = (uint8_t*)malloc(size);
    get_bytes(tmp, size, addr, 0);

    qstring path = match.str(1).c_str();

    if (!path_create_dir(path)) {
      free(tmp);
      return asm_o_state;
    }

    FILE* f = qfopen(path.c_str(), "wb");

    if (f == nullptr) {
      free(tmp);
      return asm_o_state;
    }

    qfwrite(f, tmp, size);
    qfclose(f);

    free(tmp);

    qstring name = get_name(addr);
    line.clear();

    if (!name.empty()) {
      line.append(name);
      line.append(":\n");
    }

    switch (assembler) {
    case asm_as: {
      line.cat_sprnt(ASM_SPACE ASM_AS_BIN);
    } break;
    case asm_vasm: {
      line.cat_sprnt(ASM_SPACE ASM_VASM_BIN);
    } break;
    case asm_asm68k: {
      line.cat_sprnt(ASM_SPACE ASM_ASM68K_BIN);
    } break;
    }

    line.cat_sprnt(" \"%s\"\n", path.c_str());

    switch (assembler) {
    case asm_as: {
      line.cat_sprnt(ASM_SPACE ASM_AS_ALIGN2);
    } break;
    case asm_vasm: {
      line.cat_sprnt(ASM_SPACE ASM_VASM_ALIGN2);
    } break;
    case asm_asm68k: {
      line.cat_sprnt(ASM_SPACE ASM_ASM68K_ALIGN2);
    } break;
    }

    return asm_out_bin;
  }

  return asm_o_state;
}

static asm_out_state check_include(qstring& line, ea_t addr) {
  std::cmatch match;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return asm_o_state;
  }

  if (std::regex_match(line.c_str(), match, re_check_include) == true) {
    inc_start = addr;

    inc_path = match.str(1).c_str();

    inc_listing.clear();

    return asm_out_inc_start;
  }
  else if (line.find("INC_END") != qstring::npos) {
    if (!path_create_dir(inc_path)) {
      return asm_o_state;
    }

    FILE* f = qfopen(inc_path.c_str(), "wb");

    if (f == nullptr) {
      return asm_o_state;
    }

    for (const auto vvar : inc_listing) {
      qfwrite(f, vvar.c_str(), vvar.length());
      qfwrite(f, "\n", 1);
    }

    qfclose(f);

    line.sprnt(ASM_SPACE "include \"%s\"", inc_path.c_str());

    return asm_out_inc_end;
  }
  else if (asm_o_state == asm_out_inc_start) {
    inc_listing.add(line);
  }

  return asm_o_state;
}

static int check_subroutine(const qstring& line) {
  std::cmatch match;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return 1;
  }

  if (std::regex_match(line.c_str(), match, re_check_subroutine) != true) {
    return 1;
  }

  return 0;
}

static int check_licensed_to(const qstring& line) {
  std::cmatch match;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return 1;
  }

  if (std::regex_match(line.c_str(), match, re_check_licensed_to) != true) {
    return 1;
  }

  return 0;
}

static int check_global(const qstring& line) {
  std::cmatch match;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return 1;
  }

  if (std::regex_match(line.c_str(), match, re_check_global) != true) {
    return 1;
  }

  return 0;
}

static void check_org(qstring& line) {
  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return;
  }

  std::string rep = std::regex_replace(line.c_str(), re_check_org, ASM_SPACE "org $1");

  line = rep.c_str();
}

static void check_align(qstring& line) {
  std::string rep;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return;
  }
  
  switch (assembler) {
  case asm_as: {
    rep = std::regex_replace(line.c_str(), re_check_align, ASM_SPACE ASM_AS_ALIGN2);
  } break;
  case asm_vasm: {
    rep = std::regex_replace(line.c_str(), re_check_align, ASM_SPACE ASM_VASM_ALIGN2);
  } break;
  case asm_asm68k: {
    rep = std::regex_replace(line.c_str(), re_check_align, ASM_SPACE ASM_ASM68K_ALIGN2);
  } break;
  default:
    return;
  }
  
  line = rep.c_str();
}

static void check_dcb(qstring& line) {
  std::string rep;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return;
  }

  switch (assembler) {
  case asm_as: {
    rep = std::regex_replace(line.c_str(), re_check_dcb_xx, ASM_SPACE "dc.b [$1]$2");
  } break;
  case asm_vasm: {
    rep = std::regex_replace(line.c_str(), re_check_dcb_xx, ASM_SPACE "dcb.b $1,$2");
  } break;
  case asm_asm68k: {
    rep = std::regex_replace(line.c_str(), re_check_dcb_xx, ASM_SPACE "dcb.b $1,$2");
  } break;
  default:
    return;
  }

  line = rep.c_str();
}

//static void check_quotates(qstring& line) {
//  std::string rep = std::regex_replace(line.c_str(), re_check_quotates, "$1\"$2\"");
//
//  line = rep.c_str();
//}

static void check_ats(qstring& line) {
  std::cmatch match;

  if (asm_o_state != asm_out_inc_start && asm_o_state != asm_out_none) {
    return;
  }

  if (std::regex_match(line.c_str(), match, re_check_ats) != true) {
    return;
  }

  line.replace("@", "_");
}

static bool check_rom_end(ea_t addr) {
  return (addr >= rom_end);
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

  if (addr == BADADDR || (bin_end != BADADDR && addr < bin_end) || check_rom_end(addr)) {
    return 1;
  }

  if (is_unknown(get_flags(addr))) {
    ea_t last_head = next_head(addr, rom_end);

    if (last_not_unkn != last_head) {
      last_not_unkn = last_head;
      msg("Undefined data: %a\n", addr);
    }

    if (skip_unused) {
      return 1;
    }
  }

  if (!check_subroutine(qbuf)) {
    print_line(fp, "\n");
    return 1;
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

  bool skip = false;
  asm_o_state = check_delete(qbuf, skip);

  if (skip) {
    return 1;
  }

  asm_o_state = check_binclude(qbuf, addr);
  asm_o_state = check_include(qbuf, addr);

  switch (asm_o_state) {
  case asm_out_inc_start: {
    return 1;
  } break;
  case asm_out_del_start: {
    return 1;
  } break;
  case asm_out_bin:
  case asm_out_del_end:
  case asm_out_inc_end: {
    asm_o_state = asm_out_none;
  } break;
  case asm_out_structs: {
    gen_file(ofile_type_t::OFILE_ASM, fp, BADADDR, BADADDR, GENFLG_ASMTYPE | GENFLG_ASMINC);
    asm_o_state = asm_out_none;
  } break;
  }

  print_line(fp, qbuf);

  return 1;
}

typedef struct {
  qstring name;
  qstring val;
} equ_t;

static qvector<equ_t> equs;
static qvector<ea_t> unhidden_structs;

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

static int idaapi equ_output(FILE* fp, const qstring& line, bgcolor_t prefix_color, bgcolor_t bg_color) {
  qstring qbuf;
  tag_remove(&qbuf, line);

  fix_arrows(qbuf);
  equ_parse(qbuf);
  
  return 1;
}

static void asm_add_header(FILE* fp, ea_t first_addr) {
  qstring org_first;
  org_first.sprnt(ASM_SPACE "org $%x\n\n", first_addr);

  print_line(fp, org_first);

  switch (assembler) {
  case asm_as: {
    print_line(fp, ASM_SPACE "cpu 68000");
    print_line(fp, ASM_SPACE "page 0");
    print_line(fp, ASM_SPACE "supmode on");
    print_line(fp, ASM_SPACE "padding off\n\n");
  } break;
  }
}

static void dump_ports(FILE* fp) {
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

static void dump_equals(FILE* fp) {
  if (equs.size() > 0) {
    print_line(fp, "; ------------- equals -------------");
  }

  for (const auto e : equs) {
    qstring tmp;
    tmp.cat_sprnt("%s equ %s", e.name.c_str(), e.val.c_str());

    print_line(fp, tmp.c_str());
  }

  if (equs.size() > 0) {
    print_line(fp, "; ----------------------------------\n\n\n");
  }

  equs.clear();
}

static void dump_name(FILE* fp, ea_t addr, bool extend) {
  qstring name = get_name(addr);

  if (!name.empty()) {
    name.cat_sprnt(" equ $");

    if (extend) {
      name.append("FF");
    }

    name.cat_sprnt("%a", addr);

    print_line(fp, name);
  }
}

static void dump_ram_names_sub(FILE* fp, ea_t start, size_t end, range_t exclude, bool extend) {
    while (start != BADADDR && start < end) {
        if (!exclude.contains(start)) {
            dump_name(fp, start, extend);
        }

        start = next_not_tail(start);

    }
}

static void dump_ram_names(FILE* fp) {
  print_line(fp, "; ---------- ram names -------------");

  ea_t start_ea = get_first_seg()->start_ea;
  ea_t end_ea = get_first_seg()->end_ea;

  range_t rr(start_ea, end_ea);

  dump_ram_names_sub(fp, 0x00FF0000, 0x00FF0000ULL+0x10000ULL, rr, true);
  dump_ram_names_sub(fp, 0xFFFF0000, 0xFFFF0000ULL +0x10000ULL, rr, false);

  ea_t ea = rom_end;

  while (ea != BADADDR && is_mapped(rom_end + 0x10000) && ea < (rom_end + 0x10000)) {
    dump_name(fp, ea, false);
    ea = next_not_tail(ea);
  }

  print_line(fp, "; ----------------------------------\n\n\n");
}

static void unhide_structures() {
  unhidden_structs.clear();

  auto* s = get_first_seg();

  for (auto i = s->start_ea; i < s->end_ea; ++i) {
    if (is_terse_struc(i)) {
      unhidden_structs.add(i);
      clr_terse_struc(i);
    }
  }
}

static void hide_structures() {
  for (const auto i : unhidden_structs) {
    set_terse_struc(i);
  }

  unhidden_structs.clear();
}

static void disable_hex_view() {
  qstring tmp;
  idc_value_t rv;
  tmp.sprnt("process_config_line(\"%s\")", "OPCODE_BYTES=0");
  eval_idc_expr(&rv, BADADDR, tmp.c_str());
}

static void create_base_includes(FILE* fp) {
  path_create_dir("./src/");

  const char* ports_path = "src/ports.inc";
  const char* equals_path = "src/equals.inc";
  const char* rams_path = "src/ram_addrs.inc";

  FILE* tmp = qfopen(ports_path, "wb");
  dump_ports(tmp);
  qfclose(tmp);

  tmp = qfopen(equals_path, "wb");
  dump_equals(tmp);
  qfclose(tmp);

  tmp = qfopen(rams_path, "wb");
  dump_ram_names(tmp);
  qfclose(tmp);

  qstring tmp_line;

  tmp_line.sprnt(ASM_SPACE "include \"%s\"", ports_path);
  print_line(fp, tmp_line);

  tmp_line.sprnt(ASM_SPACE "include \"%s\"", equals_path);
  print_line(fp, tmp_line);

  tmp_line.sprnt(ASM_SPACE "include \"%s\"", rams_path);
  print_line(fp, tmp_line);
}

static int idaapi disable_links(int field_id, form_actions_t& fa) {
  fa.enable_field(2, false);
  fa.enable_field(3, false);
  fa.enable_field(4, false);
  return 1;
}

static bool ask_assembler() {
  ushort chosen_asm = 0, skip_delete = 0;

  const qstring link1 = ASM_AS_LINK;
  const qstring link2 = ASM_VASM_LINK;
  const qstring link3 = ASM_ASM68K_LINK;
  auto res = ask_form("Choose output assembler\n\n%/"
           "<~A~S assembler:R><|><~V~ASM assembler:R><|><ASM68~K~ assembler:R>1>\n"
           "<~S~kip unused code/data output:C><|><~Ignore ~D~EL_START/_END tag:C>5>\n\n"
           "<AS Website     :q2:0:::>\n"
           "<VASM Website   :q3:0:::>\n"
           "<ASM68K Website :q4:0:::>",
           &disable_links,
           &chosen_asm, &skip_delete, &link1, &link2, &link3);
  
  if (res) {
    skip_unused = skip_delete & 1;
    dont_delete = skip_delete & 2;
    assembler = (out_asm_t)chosen_asm;
  }
  else {
    skip_unused = false;
    dont_delete = false;
  }

  return res == 1;
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
        *outline = equ_output;
      }
      else {
        create_base_includes(fp);
      }
      break;
    }

    if (starting) {
      bin_end = BADADDR;
      inc_start = BADADDR;
      inc_path.clear();
      inc_listing.clear();
      last_not_unkn = BADADDR;
      skip_unused = false;
      dont_delete = false;

      ea_t start_ea = get_first_seg()->start_ea;

      rom_end = get_first_seg()->end_ea + 1;

      if (is_mapped(0xA00000)) {
        rom_end = 0xA00000;
      }

      disable_hex_view();
      unhide_structures();

      if (ask_assembler()) {
        *outline = line_output;
      }

      asm_add_header(fp, start_ea);
      asm_o_state = asm_out_structs;
    }
    else {
      hide_structures();
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

        switch (out->itype) {
        case 0x7d: { // move
          if (out->Op2.type == o_reg && out->Op2.reg == 0x5e) { // move to usp
            out->segpref = 0x3; // move.l to usp
          }
        } break;
        }

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
          else if (out->itype == 0x74 && op.n == 0 && op.phrase == 8 && out->size == 4) { // jmp (label).w
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

          if (main_reg_idx != R_PC)
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

        if (opinf->ea != BADADDR && opinf->ea >= 0xFFFF0000 && opinf->ea < 0xFFFFFFFF) {
          opinf->ea &= 0xFFFFFF;
        }

        return 1;
      }
    } break;
      case processor_t::ev_out_operand: {
        outctx_t* outctx = va_arg(va, outctx_t*);
        const op_t* op = va_arg(va, const op_t*);

        if (
          op->type == o_displ && assembler != out_asm_t::asm_asm68k &&
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
      case (unsigned int)((1 << 31) | (0 << 5) | (0 << 4)) /*100*/: // CRAM
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
static void idaapi update_tiles(bool create);

struct data_as_tiles_action_t : public action_handler_t
{
  virtual int idaapi activate(action_activation_ctx_t* ctx)
  {
    update_tiles(true);
    return 1;
  }

  virtual action_state_t idaapi update(action_update_ctx_t* ctx)
  {
    return AST_ENABLE_ALWAYS;
  }
};

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

struct smd_output_mark_action_t : public action_handler_t
{
  static int idaapi clear_current(int button_code, form_actions_t& fa)
  {
    ea_t ea1 = BADADDR, ea2 = BADADDR;
    bool good = read_range_selection(nullptr, &ea1, &ea2);

    if (!good) {
      ea1 = ea2 = get_screen_ea();
    }
    else {
      ea2 -= 1;
    }

    del_extra_cmt(ea1, E_PREV);
    del_extra_cmt(ea2, E_NEXT);

    fa.close(0);
    return 1;
  }

  virtual int idaapi activate(action_activation_ctx_t* ctx)
  {
    ea_t ea1 = BADADDR, ea2 = BADADDR;
    bool good = read_range_selection(nullptr, &ea1, &ea2);

    if (!good) {
      ea1 = ea2 = get_screen_ea();
    }
    else {
      ea2 -= 1;
    }

    qstring prev_path;
    get_extra_cmt(&prev_path, ea1, E_PREV);

    ushort bin_inc_del = 0;

    if (!prev_path.empty()) {
      if (!strncmp(prev_path.begin(), "BIN", 3)) {
        bin_inc_del = 0;
      }
      else if (!strncmp(prev_path.begin(), "INC_", 4)) {
        bin_inc_del = 1;
      }
      else if (!strncmp(prev_path.begin(), "DEL_", 4)) {
        bin_inc_del = 2;
      }
    }

    if (ask_form("Choose output method\n\n"
                 "<#binclude#Mode#~B~inary include block:R><|>"
                 "<#include#ASM ~I~nclude block:R><|>"
                 "<#delete#~D~on't output block:R>2>\n"
                 "<#Clear existing tag#~C~lear:B:0:::>\n", &bin_inc_del, &clear_current) == 1) {
      delete_extra_cmts(ea1, E_PREV);
      delete_extra_cmts(ea2, E_NEXT);
      switch (bin_inc_del) {
      case 0: {
        add_extra_line(ea1, true, "BIN \"rel/path/file.bin\"");
      } break;
      case 1: {
        add_extra_line(ea1, true, "INC_START \"rel/path/file.inc\"");
        add_extra_line(ea2, false, "INC_END");
      } break;
      case 2: {
        add_extra_line(ea1, true, "DEL_START");
        add_extra_line(ea2, false, "DEL_END");
      } break;
      }
    }
    return 1;
  }

  virtual action_state_t idaapi update(action_update_ctx_t* ctx)
  {
    return AST_ENABLE_ALWAYS;
  }
};

static const char data_as_tiles_title[] = "Tile data preview";
static const char data_as_tiles_name[] = "gensida:data_as_tiles";
static data_as_tiles_action_t data_as_tiles;
static action_desc_t data_as_tiles_action = ACTION_DESC_LITERAL(data_as_tiles_name, "Paint data as tiles", &data_as_tiles, "Shift+D", NULL, -1);

static const char smd_constant_name[] = "gensida:smd_constant";
static const char smd_output_mark_name[] = "gensida:output_mark";
static smd_constant_action_t smd_constant;
static smd_output_mark_action_t smd_output_mark;
static action_desc_t smd_constant_action = ACTION_DESC_LITERAL(smd_constant_name, "Identify SMD constant", &smd_constant, "J", NULL, -1);
static action_desc_t smd_output_mark_action = ACTION_DESC_LITERAL(smd_output_mark_name, "Mark ASM output", &smd_output_mark, "Shift+J", NULL, -1);

static TWidget* g_tiles = nullptr;

//--------------------------------------------------------------------------
static ssize_t idaapi hook_ui(void* user_data, int notification_code, va_list va)
{
  if (notification_code == ui_populating_widget_popup) {
    TWidget* widget = va_arg(va, TWidget*);

    if (get_widget_type(widget) == BWN_DISASM) {
      TPopupMenu* p = va_arg(va, TPopupMenu*);
      attach_action_to_popup(widget, p, smd_constant_name);
      attach_action_to_popup(widget, p, smd_output_mark_name);
      attach_action_to_popup(widget, p, data_as_tiles_name);
    }
  }
  else if (notification_code == ui_widget_visible) {
    TWidget* w = va_arg(va, TWidget*);

    if (w == g_tiles) {
      QLineEdit* pal_addr = new QLineEdit();
      QRegExpValidator* hexVal = new QRegExpValidator(QRegExp("[0-9a-fA-F]{1,8}"));
      pal_addr->setValidator(hexVal);
      PaintForm* pf = new PaintForm();

      QScrollBar* sb = new QScrollBar(Qt::Vertical, pf);
      pf->setScrollBar(sb);

      QGridLayout* mainLayout = new QGridLayout();
      mainLayout->setMargin(4);

      QGridLayout* lAddr = new QGridLayout();
      lAddr->addWidget(new QLabel("Palette Address:"), 0, 0);
      lAddr->addWidget(pal_addr, 0, 1);
      mainLayout->addLayout(lAddr, 0, 0, 0, 2, Qt::AlignTop);
      mainLayout->setRowStretch(0, 1);
      mainLayout->setRowStretch(1, 30);
      mainLayout->addWidget(pf, 1, 0);
      mainLayout->addWidget(sb, 1, 1);
      ((QWidget*)w)->setLayout(mainLayout);

      QObject::connect(pal_addr, &QLineEdit::textChanged, pf, &PaintForm::textChanged);
      QObject::connect(sb, &QScrollBar::valueChanged, pf, &PaintForm::scrollChanged);
    }
  }
  else if (notification_code == ui_widget_invisible) {
    TWidget* w = va_arg(va, TWidget*);

    if (w == g_tiles) {
      g_tiles = nullptr;
    }
  }

  return 0;
}

static void idaapi update_tiles(bool create) {
  QWidget* w = (QWidget*)find_widget(data_as_tiles_title);
  
  if (w != nullptr) {
    w->update();
  }
  else if (create) {
    TWidget* w = find_widget(data_as_tiles_title);

    if (w != nullptr) {
      activate_widget(w, true);
      update_tiles(false);
      return;
    }

    g_tiles = w = create_empty_widget(data_as_tiles_title);

    if (w != nullptr) {
      display_widget(w, WOPN_PERSIST | WOPN_DP_RIGHT);
      ((QWidget*)w)->show();
    }
    else {
      close_widget(w, WCLS_SAVE);
      g_tiles = nullptr;
    }
  }
}

static ssize_t idaapi hook_view(void* /*ud*/, int notification_code, va_list va) {
  switch (notification_code) {
  case view_loc_changed: {
    update_tiles(false);
  } break;
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
    res = register_action(smd_output_mark_action);
    res = register_action(data_as_tiles_action);

    hook_to_notification_point(HT_UI, hook_ui, nullptr);
    hook_to_notification_point(HT_IDP, hook_disasm, nullptr);
    hook_to_notification_point(HT_IDP, process_asm_output, nullptr);
    register_post_event_visitor(HT_IDP, &ctx, nullptr);
    hook_to_notification_point(HT_DBG, hook_dbg, nullptr);
    hook_to_notification_point(HT_VIEW, hook_view, nullptr);
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
    unhook_from_notification_point(HT_VIEW, hook_view);

    unregister_action(smd_output_mark_name);
    unregister_action(smd_constant_name);
    unregister_action(data_as_tiles_name);
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
