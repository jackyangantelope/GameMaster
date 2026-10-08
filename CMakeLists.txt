cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD adafruit_fruit_jam CACHE STRING "Adafruit Fruit Jam" FORCE)
set(PICO_PSRAM_CS_PIN 47 CACHE STRING "Fruit Jam PSRAM CS" FORCE)
set(PICO_PSRAM_SIZE_BYTES 0x800000 CACHE STRING "Fruit Jam PSRAM size" FORCE)
set(PICO_DEFAULT_UART 0 CACHE STRING "Diagnostic UART" FORCE)
set(PICO_DEFAULT_UART_TX_PIN 44 CACHE STRING "Diagnostic UART TX" FORCE)
set(PICO_DEFAULT_UART_RX_PIN 45 CACHE STRING "Diagnostic UART RX" FORCE)

if(NOT DEFINED PICO_SDK_PATH OR NOT DEFINED PICO_PIO_USB_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH and PICO_PIO_USB_PATH")
endif()
set(PICO_SHARED_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/pico_shared" CACHE PATH "pico_shared checkout")
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(picoGameMaster C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(GMASTER_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(UPD_ROOT "${GMASTER_ROOT}/deps/common-src-project/src/vm/mame/emu/cpu/upd7810")
set(WAVE_ROOT "${GMASTER_ROOT}/platform/wave1")
set(GMASTER_VERSION "0.2.18-public-candidate")

add_library(gmaster_core STATIC
    "${GMASTER_ROOT}/core/gmaster_board.c"
    "${GMASTER_ROOT}/core/gmaster_upd7810.cpp")
target_include_directories(gmaster_core PUBLIC "${GMASTER_ROOT}/core" PRIVATE "${UPD_ROOT}")
target_compile_definitions(gmaster_core PUBLIC
    GMASTER_LCD_TEST_62_COLUMNS=1
    GMASTER_UPD7810_FAST_ETIMER=1
    GMASTER_UPD7810_FAST_IRQ=1
    GMASTER_UPD7810_TIMER_AUDIO=1
    GMASTER_UPD7810_FAST_IDLE_TIMER=1
    GMASTER_UPD7810_FAST_MASKED_IRQ=1)
target_compile_options(gmaster_core PRIVATE -fno-function-sections -fno-data-sections)

set(JACK_WAVE1_HDMI_AUDIO ON)
set(JACK_WAVE1_HDMI_DRIVER_ROOT "${PICO_SHARED_PATH}/drivers/pico_hdmi")
set(JACK_WAVE1_HDMI_VIDEO_SOURCE "${JACK_WAVE1_HDMI_DRIVER_ROOT}/video_output.c")
set(JACK_WAVE1_HDMI_ADAPTER "${WAVE_ROOT}/hdmi/jack_hdmi.c")
add_subdirectory("${WAVE_ROOT}" "${CMAKE_BINARY_DIR}/wave1")
target_compile_definitions(jack_wave1_runtime PRIVATE
    JACK_WAVE1_USB_NONBLOCKING=1 JACK_WAVE1_QUIET_INPUT_LOG=1)

option(BUILD_FOR_BOOTLOADER "Link to the resident loader app partition" ON)
add_executable(picoGameMaster
    "${GMASTER_ROOT}/firmware/playable_platform.cpp"
    "${GMASTER_ROOT}/firmware/gmaster_log.cpp")
pico_set_program_name(picoGameMaster "picoGameMaster")
pico_set_program_version(picoGameMaster "${GMASTER_VERSION}")
pico_enable_stdio_uart(picoGameMaster 1)
pico_enable_stdio_usb(picoGameMaster 0)
target_compile_definitions(picoGameMaster PRIVATE
    GMASTER_BUILD_VERSION="${GMASTER_VERSION}"
    GMASTER_NO_AUDIO_PROBE=1 GMASTER_WINDOW_STATS=1
    JACK_GMASTER_BOUNDED_LOG=1 JACK_MEGADUCK_AUDIO033=1
    HW_CONFIG=8 PICO_STACK_SIZE=4096
    JACK_WAVE1_PERF_INTERVAL_US=1000000u)
target_link_libraries(picoGameMaster PRIVATE
    pico_stdlib hardware_psram hardware_uart gmaster_core jack_wave1_runtime)
target_link_options(picoGameMaster PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage)
if(BUILD_FOR_BOOTLOADER)
    include("${GMASTER_ROOT}/cmake/LoaderPartition.cmake")
    gamemaster_link_for_loader(picoGameMaster)
endif()
pico_add_linker_script_override_path(picoGameMaster
    "${GMASTER_ROOT}/cmake/cpu-sram-linker")
pico_add_extra_outputs(picoGameMaster)
