# Source changes from upstream

Base interpreter: [tautologico/common-src-project](https://github.com/tautologico/common-src-project), revision `91ebd909500b7c829d9741f10ac30964cbab71f0`. The CPU source header offers GPL-2.0-or-later, selected for this port. The repository carries only the interpreter files needed to build, not the full desktop emulator framework.

| Files | Port work |
| --- | --- |
| `deps/common-src-project/src/vm/mame/emu/cpu/upd7810/upd7810.c` | 67 added lines for opt-in IRQ and event-timer fast paths and single-step execution; original code and license header retained. |
| `core/gmaster_upd7810.cpp`, `core/gmaster_upd7810.h`, `core/cstring` | Standalone CPU adapter, memory windows, I/O bridge, timer-to-speaker routing, and SRAM execution placement. |
| `core/gmaster_board.c`, `core/gmaster_board.h` | Game Master memory, port, dual SED1520 LCD, input, and 1-bit speaker model. |
| `firmware/playable_platform.cpp`, `firmware/gmaster_log.cpp`, `firmware/gmaster_log.h` | BIOS/cartridge selection, frame/audio synthesis, user input and bounded telemetry. |
| `platform/wave1/*`, `cmake/*` | Public Fruit Jam SD, USB, HDMI audio/video and resident-loader build. This adaptation uses a 252 MHz public profile and removes unsupported line-miss telemetry. |

The 0.2.18 host tests covered CPU, timers, IRQs, LCD, and audio-source equivalence. They did not validate the Fruit Jam runtime, nor does a successful firmware build prove hardware performance.
