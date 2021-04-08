#ifndef corehooks_h
#define corehooks_h
#define uint32 unsigned int

extern "C" {
    extern uint32 hook_address;
    extern uint32 hook_value;
    extern uint32 hook_pc;

    void hook_read_byte();
    void hook_read_word();
    void hook_read_dword();
    void hook_write_byte();
    void hook_write_word();
    void hook_write_dword();

    void hook_write_vram_byte();
    void hook_write_vram_word();
    void hook_read_vram_byte();
    void hook_read_vram_word();

    void hook_dma();
    void hook_vdp_reg();

    void hook_exec();
};
#endif
