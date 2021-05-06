enum GpRegsEnum {
  AF,
  AF2,
  BC,
  BC2,
  DE,
  DE2,
  HL,
  HL2,

  IX,
  IY,
  I,
  R,
  PC,

  SP,
  IP,

  BANK,
}

struct GpRegister {
  1:GpRegsEnum index,
  2:i32 value,
}

struct GpRegisters {
  1:i32 AF,
  2:i32 AF2,
  3:i32 BC,
  4:i32 BC2,
  5:i32 DE,
  6:i32 DE2,
  7:i32 HL,
  8:i32 HL2,

  9:i32 IX,
  10:i32 IY,
  11:i32 I,
  12:i32 R,

  13:i32 SP,
  14:i32 PC,
  15:i32 IP,

  16:i32 BANK,
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
  5:bool is_forbid,
}

service DbgServer {
  i32 get_gp_reg(1:GpRegsEnum index),
  GpRegisters get_gp_regs(),
  void set_gp_reg(1:GpRegister reg),

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
