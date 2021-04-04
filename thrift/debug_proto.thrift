enum GpRegsEnum {
  D0,
  D1,
  D2,
  D3,
  D4,
  D5,
  D6,
  D7,

  A0,
  A1,
  A2,
  A3,
  A4,
  A5,
  A6,
  A7,

  PC,
  SR,
  SP,
}

struct GpRegister {
  1:GpRegsEnum index,
  2:i32 value,
}

struct GpRegisters {
  1:i32 D0,
  2:i32 D1,
  3:i32 D2,
  4:i32 D3,
  5:i32 D4,
  6:i32 D5,
  7:i32 D6,
  8:i32 D7,

  9:i32 A0,
  10:i32 A1,
  11:i32 A2,
  12:i32 A3,
  13:i32 A4,
  14:i32 A5,
  15:i32 A6,
  16:i32 A7,

  17:i32 PC,
  18:i32 SR,
  19:i32 SP,
}

enum VdpRegsEnum {
  V00,
  V01,
  V02,
  V03,
  V04,
  V05,
  V06,
  V07,
  V08,
  V09,
  V0A,
  V0B,
  V0C,
  V0D,
  V0E,
  V0F,
  V10,
  V11,
  V12,
  V13,
  V14,
  V15,
  V16,
  V17,
}

struct VdpRegister {
  1:VdpRegsEnum index,
  2:i16 value,
}

struct VdpRegisters {
  1:i16 V00,
  2:i16 V01,
  3:i16 V02,
  4:i16 V03,
  5:i16 V04,
  6:i16 V05,
  7:i16 V06,
  8:i16 V07,
  9:i16 V08,
  10:i16 V09,
  11:i16 V0A,
  12:i16 V0B,
  13:i16 V0C,
  14:i16 V0D,
  15:i16 V0E,
  16:i16 V0F,
  17:i16 V10,
  18:i16 V11,
  19:i16 V12,
  20:i16 V13,
  21:i16 V14,
  22:i16 V15,
  23:i16 V16,
  24:i16 V17,
}

struct DmaInfo {
  1:i16 Len,
  2:i32 Src,
  3:i32 Dst,
}

enum BpType {
  BP_PC = 1,
  BP_READ,
  BP_WRITE,
}

struct DbgBreakpoint {
  1:BpType type,
  2:i32 bstart,
  3:i32 bend,
  4:bool enabled,
  5:bool is_vdp,
  6:bool is_forbid,
}

service DbgServer {
  i32 get_gp_reg(1:GpRegsEnum index),
  GpRegisters get_gp_regs(),
  void set_gp_reg(1:GpRegister reg),
  i16 get_vdp_reg(1:VdpRegsEnum index),
  VdpRegisters get_vdp_regs(),
  void set_vdp_reg(1:VdpRegister reg),
  DmaInfo get_dma_info(),

  binary read_memory(1:i32 address, 2:i32 size),
  void write_memory(1:i32 address, 2:binary data),

  list<DbgBreakpoint> get_breakpoints(),
  void add_breakpoint(1:DbgBreakpoint bpt),
  void toggle_breakpoint(1:DbgBreakpoint bpt),
  void update_breakpoint(1:DbgBreakpoint bpt),
  void del_breakpoint(1:DbgBreakpoint bpt),
  void clear_breakpoints(),

  void pause(),
  void resume(),
  void start_emulation(),
  void exit_emulation(),

  void step_into(),
  void step_over(),

  list<i32> get_callstack(),
}

service DbgClient {
  oneway void start_event(),
  oneway void pause_event(1:i32 address, 2:map<i32,i32> changed),
  oneway void stop_event(1:map<i32,i32> changed),
}
