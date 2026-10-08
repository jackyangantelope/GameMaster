
#include "gmaster_board.h"

#include <string.h>

enum {
    GMASTER_RAM_CS = 1u << 0,
    GMASTER_LCD0_CS = 1u << 1,
    GMASTER_LCD1_CS = 1u << 2,
    GMASTER_SPEAKER = 1u << 4,
};

static void sed_command(gmaster_sed1520_t *lcd, uint8_t value)
{
    if ((value & 0xFEu) == 0xAEu) {
        lcd->display_on = (value & 1u) != 0u;
    } else if ((value & 0xE0u) == 0xC0u) {
        lcd->start_line = value & 0x1Fu;
    } else if ((value & 0xFCu) == 0xB8u) {
        lcd->page = value & 0x03u;
    } else if ((value & 0x80u) == 0u) {
        lcd->column = value & 0x7Fu;
    } else if ((value & 0xFEu) == 0xA0u) {
        lcd->adc = value & 1u;
    } else if ((value & 0xFEu) == 0xA4u) {
        lcd->static_drive = (value & 1u) != 0u;
    } else if ((value & 0xFEu) == 0xA8u) {
        lcd->duty = value & 1u;
    } else if (value == 0xE0u) {
        lcd->modify_write = true;
        lcd->old_column = lcd->column;
    } else if (value == 0xEEu) {
        lcd->modify_write = false;
        lcd->column = lcd->old_column;
    } else if (value == 0xE2u) {
        lcd->start_line = 0u;
        lcd->column = 0u;
        lcd->page = 3u;
    }
}

static void sed_write(gmaster_sed1520_t *lcd, bool data_register, uint8_t value)
{
    if (!data_register) {
        sed_command(lcd, value);
        return;
    }

    lcd->data_latch = value;
    if (lcd->column < 80u) {
        const unsigned int index = (unsigned int)lcd->page * 80u + lcd->column;
        lcd->dram[index] = value;
        ++lcd->column;
    }
}

static uint8_t sed_read(gmaster_sed1520_t *lcd, bool data_register)
{
    if (!data_register) {
        return (uint8_t)((lcd->adc << 6u) | (lcd->display_on ? 0x20u : 0u));
    }

    const uint8_t value = lcd->data_latch;
    if (lcd->column < 80u) {
        const unsigned int index = (unsigned int)lcd->page * 80u + lcd->column;
        lcd->data_latch = lcd->dram[index];
        if (!lcd->modify_write) {
            ++lcd->column;
        }
    } else {
        lcd->data_latch = 0u;
    }
    return value;
}

void gmaster_board_reset(gmaster_board_t *board)
{
    memset(board, 0, sizeof(*board));
    for (unsigned int chip = 0; chip < 2u; ++chip) {
        board->lcd[chip].page = 3u;
        board->lcd[chip].adc = 1u;
    }
}

void gmaster_board_write_port_c(gmaster_board_t *board, uint8_t value)
{
    board->port_c = value;
    board->speaker_high = (value & GMASTER_SPEAKER) != 0u;
}

void gmaster_board_write_external(gmaster_board_t *board, uint16_t address, uint8_t value)
{
    const uint16_t offset = address & 0x07FFu;
    const bool data_register = (offset & 1u) != 0u;

    if ((board->port_c & GMASTER_RAM_CS) != 0u) {
        board->ram[offset] = value;
    }
    if ((board->port_c & GMASTER_LCD0_CS) != 0u) {
        sed_write(&board->lcd[0], data_register, value);
    }
    if ((board->port_c & GMASTER_LCD1_CS) != 0u) {
        sed_write(&board->lcd[1], data_register, value);
    }
}

uint8_t gmaster_board_read_external(gmaster_board_t *board, uint16_t address)
{
    const uint16_t offset = address & 0x07FFu;
    const bool data_register = (offset & 1u) != 0u;
    uint8_t value = 0u;

    if ((board->port_c & GMASTER_RAM_CS) != 0u) {
        value |= board->ram[offset];
    }
    if ((board->port_c & GMASTER_LCD0_CS) != 0u) {
        value |= sed_read(&board->lcd[0], data_register);
    }
    if ((board->port_c & GMASTER_LCD1_CS) != 0u) {
        value |= sed_read(&board->lcd[1], data_register);
    }
    return value;
}

void gmaster_board_render(const gmaster_board_t *board,
                          uint8_t framebuffer[GMASTER_LCD_HEIGHT][GMASTER_LCD_WIDTH])
{
    memset(framebuffer, 0, GMASTER_LCD_HEIGHT * GMASTER_LCD_WIDTH);

    for (unsigned int chip = 0; chip < 2u; ++chip) {
        if (!board->lcd[chip].display_on) {
            continue;
        }
        for (unsigned int cell = 0; cell < GMASTER_SED_DRAM_BYTES; ++cell) {
            const unsigned int x = cell % 80u;
            if (x >= GMASTER_LCD_WIDTH) {
                continue;
            }
            const unsigned int y0 = chip * 32u + (cell / 80u) * 8u;
            const uint8_t bits = board->lcd[chip].dram[cell];
            for (unsigned int bit = 0; bit < 8u; ++bit) {
                framebuffer[y0 + bit][x] = (bits >> bit) & 1u;
            }
        }
    }
}
