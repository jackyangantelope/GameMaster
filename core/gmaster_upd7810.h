
#ifndef JACK_GMASTER_UPD7810_H
#define JACK_GMASTER_UPD7810_H

#include <cstdint>

class GMasterUpd7810Bus {
public:
    virtual ~GMasterUpd7810Bus() = default;
    virtual uint8_t read_memory(uint16_t address) = 0;
    virtual void write_memory(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(unsigned int port) = 0;
    virtual void write_port(unsigned int port, uint8_t value) = 0;
};

class GMasterUpd7810 {
public:
    explicit GMasterUpd7810(GMasterUpd7810Bus &bus);
    ~GMasterUpd7810();

    GMasterUpd7810(const GMasterUpd7810 &) = delete;
    GMasterUpd7810 &operator=(const GMasterUpd7810 &) = delete;

    void reset();
    int step();
    uint32_t run_cycles(uint32_t target_cycles, uint32_t *cycle_cursor);
    void set_rom_windows(const uint8_t *bios, const uint8_t *cart, uint16_t cart_mask);
    uint16_t pc() const;
#ifdef GMASTER_UPD7810_PROFILE
    uint32_t timer_modes() const;
    uint64_t timer_details() const;
    uint32_t irq_state() const;
#endif

private:
    class Adapter;
    Adapter *adapter_;
    void *state_;
};

#endif
