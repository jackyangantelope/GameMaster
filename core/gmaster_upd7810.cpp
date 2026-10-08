

// Adapter for the GPL-2.0-or-later uPD7810 core.
#include "gmaster_upd7810.h"

#include <cstdlib>
#include <cstring>

// Interpreter text and tables must be copied to SRAM.
#pragma GCC section text=".time_critical.gmaster_cpu_text" rodata=".time_critical.gmaster_cpu_rodata"

using INT8 = int8_t;
using INT16 = int16_t;
using INT32 = int32_t;
using UINT8 = uint8_t;
using UINT16 = uint16_t;
using UINT32 = uint32_t;

union pair32_t {
    struct { uint8_t l, h, h2, h3; } b;
    struct { uint16_t l, h; } w;
    uint32_t d;
};

struct upd7810_state;
class DEVICE {
public:
    struct outputs_t {};
    explicit DEVICE(GMasterUpd7810Bus &bus) : bus_(bus) {}
    void set_rom_windows(const uint8_t *bios, const uint8_t *cart, uint16_t cart_mask)
    {
        bios_ = bios;
        cart_ = cart;
        cart_mask_ = cart_mask;
    }
    uint8_t read_data8(uint32_t address)
    {
        const auto mapped = static_cast<uint16_t>(address);
        if (bios_ && mapped < 0x1000) return bios_[mapped];
        if (cart_ && mapped >= 0x8000 && mapped < 0xff00)
            return cart_[(mapped - 0x8000) & cart_mask_];
        return bus_.read_memory(mapped);
    }
    void write_data8(uint32_t address, uint32_t value)
    {
        bus_.write_memory(static_cast<uint16_t>(address), static_cast<uint8_t>(value));
    }
    uint8_t read_io8(uint32_t address)
    {
        return bus_.read_port(static_cast<unsigned int>(address));
    }
    void write_io8(uint32_t address, uint32_t value)
    {
        bus_.write_port(static_cast<unsigned int>(address), static_cast<uint8_t>(value));
    }
    uint16_t read_io16(uint32_t address)
    {
        return static_cast<uint16_t>(read_io8(address) | (read_io8(address + 1) << 8));
    }
    void write_io16(uint32_t address, uint32_t value)
    {
        write_io8(address, value & 0xff);
        write_io8(address + 1, (value >> 8) & 0xff);
    }
    void write_signals(outputs_t *, uint32_t);
#ifdef GMASTER_UPD7810_TIMER_AUDIO
    void set_cpu_state(upd7810_state *state) { cpu_state_ = state; }
#endif
private:
#ifdef GMASTER_UPD7810_TIMER_AUDIO
    upd7810_state *cpu_state_ = nullptr;
#endif
    GMasterUpd7810Bus &bus_;
    const uint8_t *bios_ = nullptr;
    const uint8_t *cart_ = nullptr;
    uint16_t cart_mask_ = 0;
};

class GMasterUpd7810::Adapter final : public DEVICE {
public:
    explicit Adapter(GMasterUpd7810Bus &bus) : DEVICE(bus) {}
    outputs_t outputs;
    outputs_t serial_outputs;
};

#define PAIR pair32_t
#define offs_t UINT16
#define CPU_INIT_NAME(name) cpu_init_##name
#define CPU_INIT(name) void *CPU_INIT_NAME(name)()
#define CPU_RESET_NAME(name) cpu_reset_##name
#define CPU_RESET(name) void CPU_RESET_NAME(name)(upd7810_state *cpustate)
#define CPU_EXECUTE_NAME(name) cpu_execute_##name
#define CPU_EXECUTE(name) int CPU_EXECUTE_NAME(name)(upd7810_state *cpustate)
#define CPU_INIT_CALL(name) CPU_INIT_NAME(name)()
#define CPU_RESET_CALL(name) CPU_RESET_NAME(name)(cpustate)
#define CPU_EXECUTE_CALL(name) CPU_EXECUTE_NAME(name)(cpustate)

enum line_state { CLEAR_LINE = 0, ASSERT_LINE };
enum {
    UPD7810_INTF1 = 0,
    UPD7810_INTF2 = 1,
    UPD7810_INTF0 = 2,
    UPD7810_INTFE1 = 4,
    INPUT_LINE_NMI
};

#define logerror(...)
#define fatalerror(...)
#undef IN
#undef OUT

#define GMASTER_UPD7810_FAST_SINGLE_STEP 1
#include "upd7810.c"

void DEVICE::write_signals(outputs_t *output, uint32_t)
{
#ifdef GMASTER_UPD7810_TIMER_AUDIO

    if (cpu_state_ && output == cpu_state_->outputs_to &&
        (cpu_state_->mcc & 0x10u))
        WP(cpu_state_, UPD7810_PORTC, cpu_state_->pc_out);
#endif
}

GMasterUpd7810::GMasterUpd7810(GMasterUpd7810Bus &bus)
    : adapter_(new Adapter(bus)), state_(CPU_INIT_CALL(upd7810))
{
    auto *cpustate = static_cast<upd7810_state *>(state_);
    cpustate->config.type = TYPE_7810;
    reset();
}

GMasterUpd7810::~GMasterUpd7810()
{
    std::free(state_);
    delete adapter_;
}

void GMasterUpd7810::reset()
{
    auto *cpustate = static_cast<upd7810_state *>(state_);
    CPU_RESET_CALL(upd7810);
    cpustate->program = adapter_;
    cpustate->io = adapter_;
    cpustate->outputs_to = &adapter_->outputs;
    cpustate->outputs_txd = &adapter_->serial_outputs;
#ifdef GMASTER_UPD7810_TIMER_AUDIO
    adapter_->set_cpu_state(cpustate);
#endif
}

int GMasterUpd7810::step()
{
    auto *cpustate = static_cast<upd7810_state *>(state_);
    return CPU_EXECUTE_CALL(upd7810);
}

uint32_t GMasterUpd7810::run_cycles(uint32_t target_cycles, uint32_t *cycle_cursor)
{
    auto *cpustate = static_cast<upd7810_state *>(state_);
    uint32_t cycles = 0;
    while (cycles < target_cycles) {

        *cycle_cursor = cycles;
        cycles += static_cast<uint32_t>(CPU_EXECUTE_CALL(upd7810));
    }
    return cycles;
}

void GMasterUpd7810::set_rom_windows(const uint8_t *bios, const uint8_t *cart,
                                      uint16_t cart_mask)
{
    adapter_->set_rom_windows(bios, cart, cart_mask);
}

uint16_t GMasterUpd7810::pc() const
{
    const auto *cpustate = static_cast<const upd7810_state *>(state_);
    return cpustate->pc.w.l;
}

#ifdef GMASTER_UPD7810_PROFILE
uint32_t GMasterUpd7810::timer_modes() const
{
    const auto *cpustate = static_cast<const upd7810_state *>(state_);
    return static_cast<uint32_t>(cpustate->tmm) |
           (static_cast<uint32_t>(cpustate->etmm) << 8) |
           (static_cast<uint32_t>(cpustate->smh) << 16) |
           (static_cast<uint32_t>(cpustate->iff) << 24);
}

uint64_t GMasterUpd7810::timer_details() const
{
    const auto *cpustate = static_cast<const upd7810_state *>(state_);
    return static_cast<uint64_t>(cpustate->etm.w.l) |
           (static_cast<uint64_t>(cpustate->etm.w.h) << 16) |
           (static_cast<uint64_t>(cpustate->eom) << 32) |
           (static_cast<uint64_t>(cpustate->ecnt.w.l) << 40);
}

uint32_t GMasterUpd7810::irq_state() const
{
    const auto *cpustate = static_cast<const upd7810_state *>(state_);
    return static_cast<uint32_t>(cpustate->irr) |
           (static_cast<uint32_t>(cpustate->mkl) << 16) |
           (static_cast<uint32_t>(cpustate->mkh) << 24);
}
#endif

#pragma GCC section text="" rodata=""
