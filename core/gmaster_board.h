
#ifndef JACK_GMASTER_BOARD_H
#define JACK_GMASTER_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define GMASTER_RAM_BYTES 2048u

// The 62-column mode exposes one normally hidden LCD RAM column.

#if defined(GMASTER_LCD_TEST_62_COLUMNS) && defined(GMASTER_LCD_TEST_64_COLUMNS)
#error Select only one Game Master LCD diagnostic width
#endif
#if defined(GMASTER_LCD_TEST_64_COLUMNS)
#define GMASTER_LCD_WIDTH 64u
#elif defined(GMASTER_LCD_TEST_62_COLUMNS)
#define GMASTER_LCD_WIDTH 62u
#else
#define GMASTER_LCD_WIDTH 61u
#endif
#define GMASTER_LCD_HEIGHT 64u
#define GMASTER_SED_DRAM_BYTES 320u

typedef struct gmaster_sed1520 {
    uint8_t dram[GMASTER_SED_DRAM_BYTES];
    uint8_t page;
    uint8_t column;
    uint8_t old_column;
    uint8_t start_line;
    uint8_t data_latch;
    uint8_t adc;
    uint8_t duty;
    bool display_on;
    bool static_drive;
    bool modify_write;
} gmaster_sed1520_t;

typedef struct gmaster_board {
    uint8_t ram[GMASTER_RAM_BYTES];
    gmaster_sed1520_t lcd[2];
    uint8_t port_c;
    bool speaker_high;
} gmaster_board_t;

#ifdef __cplusplus
extern "C" {
#endif

void gmaster_board_reset(gmaster_board_t *board);
void gmaster_board_write_port_c(gmaster_board_t *board, uint8_t value);
void gmaster_board_write_external(gmaster_board_t *board, uint16_t address, uint8_t value);
uint8_t gmaster_board_read_external(gmaster_board_t *board, uint16_t address);
void gmaster_board_render(const gmaster_board_t *board,
                          uint8_t framebuffer[GMASTER_LCD_HEIGHT][GMASTER_LCD_WIDTH]);

#ifdef __cplusplus
}
#endif

#endif
