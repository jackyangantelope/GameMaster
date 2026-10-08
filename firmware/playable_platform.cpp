
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ff.h"
#include "pico/stdlib.h"

#include "gmaster_board.h"
#include "gmaster_upd7810.h"
#include "gmaster_log.h"
#include "jack_wave1_audio.h"
#include "jack_wave1_perf.h"
#include "jack_wave1_runtime.h"

#ifndef GMASTER_BUILD_VERSION
#if defined(GMASTER_NO_AUDIO_PROBE)
#define GMASTER_BUILD_VERSION "0.2.15-timer-fast-test"
#elif defined(GMASTER_AUDIO_DIAG)
#define GMASTER_BUILD_VERSION "0.2.14-audio-diag"
#elif defined(GMASTER_LCD_TEST_64_COLUMNS)
#define GMASTER_BUILD_VERSION "0.2.12-lcd64-test"
#elif defined(GMASTER_LCD_TEST_62_COLUMNS)
#define GMASTER_BUILD_VERSION "0.2.13-lcd62-test"
#else
#define GMASTER_BUILD_VERSION "0.2.11-audio-log"
#endif
#endif

namespace {
constexpr size_t kBiosBytes = 4096;
constexpr size_t kMaximumCartBytes = 32768;
constexpr size_t kMinimumCartBytes = 8192;
constexpr uint32_t kCpuClockHz = 12000000;
constexpr uint32_t kCyclesPerFrame = 200000;
constexpr uint32_t kAudioSampleRate = 44100;
constexpr int16_t kSpeakerAmplitude = 4096;
constexpr int16_t kProbeAmplitude = 8192;
constexpr uint32_t kProbeFrequency = 1000;
#ifdef GMASTER_NO_AUDIO_PROBE
constexpr uint32_t kProbeFrames = 0;
#else
constexpr uint32_t kProbeFrames = 180;
#endif
#ifdef GMASTER_AUDIO_DIAG
constexpr uint32_t kAudioDiagPhaseUs = 15000000u;
constexpr uint8_t kAudioDiagPhaseCount = 5;
#endif
constexpr size_t kMaximumAudioFrames = 768;
constexpr size_t kMaximumSpeakerTransitions = 1024;
constexpr unsigned kPortA = 0;
constexpr unsigned kPortC = 2;

static_assert(kCpuClockHz / kCyclesPerFrame == 60,
              "Game Master timing must remain 60 Hz");

uint8_t g_bios[kBiosBytes];
uint8_t g_cart[kMaximumCartBytes];
size_t g_cart_size;
uint16_t g_cart_mask;
uint8_t g_internal_ram[256];
uint8_t g_frame[GMASTER_LCD_HEIGHT][GMASTER_LCD_WIDTH];
uint8_t g_previous[GMASTER_LCD_HEIGHT][GMASTER_LCD_WIDTH];
gmaster_board_t g_board;
uint16_t g_buttons;
char g_bios_path[JACK_WAVE1_PATH_MAX];
char g_cart_path[JACK_WAVE1_PATH_MAX];
JackWave1Perf g_perf;
uint32_t g_audio_phase;
uint32_t g_probe_phase;
int16_t g_previous_pcm_sample;
uint32_t g_step_us;
uint32_t g_synth_us;
int16_t g_audio_samples[kMaximumAudioFrames * 2];
uint32_t g_step_cycle_cursor;
uint32_t g_speaker_transition_cycle[kMaximumSpeakerTransitions];
uint8_t g_speaker_transition_high[kMaximumSpeakerTransitions];
size_t g_speaker_transition_count;
uint32_t g_speaker_transition_dropped;
bool g_record_speaker_transitions;
#ifdef GMASTER_AUDIO_DIAG
uint8_t g_audio_diag_phase;
uint32_t g_audio_diag_phase_start;
uint32_t g_audio_diag_frames;
uint32_t g_audio_diag_edge_frames;
uint32_t g_audio_diag_edges;
uint32_t g_audio_diag_flips;
uint32_t g_audio_diag_max_edges;
uint32_t g_audio_diag_core_us;
uint32_t g_audio_diag_draw_us;
uint32_t g_audio_diag_wait_us;
uint32_t g_audio_diag_late_frames;
#endif

bool load_exact(const char *path, uint8_t *destination, size_t size)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK || f_size(&file) != size) return false;
    UINT count = 0;
    const bool ok = f_read(&file, destination, static_cast<UINT>(size), &count) == FR_OK &&
                    count == size;
    f_close(&file);
    return ok;
}

bool load_cart(const char *path)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    const FSIZE_t file_size = f_size(&file);
    if (file_size != kMinimumCartBytes && file_size != 16384u &&
        file_size != kMaximumCartBytes) {
        f_close(&file);
        return false;
    }
    UINT count = 0;
    const bool ok = f_read(&file, g_cart, static_cast<UINT>(file_size), &count) == FR_OK &&
                    count == file_size;
    f_close(&file);
    if (!ok) return false;
    g_cart_size = static_cast<size_t>(file_size);
    g_cart_mask = static_cast<uint16_t>(g_cart_size - 1u);
    return true;
}

class MachineBus final : public GMasterUpd7810Bus {
public:
    uint8_t read_memory(uint16_t address) override
    {
        if (address < 0x1000) return g_bios[address];
        if (address >= 0xff00) return g_internal_ram[address & 0xff];
        if (address >= 0x8000) return g_cart[(address - 0x8000) & g_cart_mask];
        if (address >= 0x4000) return gmaster_board_read_external(&g_board, address);
        return 0xff;
    }

    void write_memory(uint16_t address, uint8_t value) override
    {
        if (address >= 0xff00) g_internal_ram[address & 0xff] = value;
        else if (address >= 0x4000 && address < 0x8000)
            gmaster_board_write_external(&g_board, address, value);
    }

    uint8_t read_port(unsigned int port) override
    {
        if (port != kPortA) return 0xff;
        uint8_t value = 0xff;
        if (g_buttons & JACK_BUTTON_RIGHT) value &= ~0x01;
        if (g_buttons & JACK_BUTTON_LEFT) value &= ~0x02;
        if (g_buttons & JACK_BUTTON_DOWN) value &= ~0x04;
        if (g_buttons & JACK_BUTTON_UP) value &= ~0x08;
        if (g_buttons & JACK_BUTTON_B) value &= ~0x10;
        if (g_buttons & (JACK_BUTTON_A | JACK_BUTTON_X)) value &= ~0x20;
        if (g_buttons & JACK_BUTTON_SELECT) value &= ~0x40;
        if (g_buttons & JACK_BUTTON_START) value &= ~0x80;
        return value;
    }

    void write_port(unsigned int port, uint8_t value) override
    {
        if (port != kPortC) return;
        const bool high = (value & 0x10u) != 0;
        if (g_record_speaker_transitions && high != g_board.speaker_high) {
            if (g_speaker_transition_count < kMaximumSpeakerTransitions) {
                g_speaker_transition_cycle[g_speaker_transition_count] =
                    g_step_cycle_cursor;
                g_speaker_transition_high[g_speaker_transition_count] = high ? 1u : 0u;
                ++g_speaker_transition_count;
            } else {
                ++g_speaker_transition_dropped;
            }
        }
        gmaster_board_write_port_c(&g_board, value);
    }
};

void emit_audio_span(uint32_t cycles, bool speaker_high, size_t &frames)
{
    const uint64_t total = static_cast<uint64_t>(g_audio_phase) +
                           static_cast<uint64_t>(cycles) * kAudioSampleRate;
    const size_t generated = static_cast<size_t>(total / kCpuClockHz);
    g_audio_phase = static_cast<uint32_t>(total % kCpuClockHz);
    const size_t writable = std::min(generated, kMaximumAudioFrames - frames);
    const int16_t sample = speaker_high ? kSpeakerAmplitude : -kSpeakerAmplitude;
    for (size_t i = 0; i < writable; ++i) {
        g_audio_samples[(frames + i) * 2] = sample;
        g_audio_samples[(frames + i) * 2 + 1] = sample;
    }
    frames += writable;
}

size_t run_cpu_audio_frame(GMasterUpd7810 &cpu)
{
    const uint32_t step_start = time_us_32();
    const bool initial_speaker_high = g_board.speaker_high;
    g_speaker_transition_count = 0;
    g_record_speaker_transitions = true;
    const uint32_t cycles = cpu.run_cycles(kCyclesPerFrame, &g_step_cycle_cursor);
    g_record_speaker_transitions = false;
    const uint32_t synth_start = time_us_32();
    g_step_us = synth_start - step_start;

    size_t frames = 0;
    uint32_t cursor = 0;
    bool speaker_high = initial_speaker_high;
    for (size_t i = 0; i < g_speaker_transition_count; ++i) {
        const uint32_t transition = std::min(g_speaker_transition_cycle[i], cycles);
        emit_audio_span(transition - cursor, speaker_high, frames);
        speaker_high = g_speaker_transition_high[i] != 0;
        cursor = transition;
    }
    emit_audio_span(cycles - cursor, speaker_high, frames);
    g_synth_us = time_us_32() - synth_start;
    return frames;
}

uint32_t bios_fingerprint()
{
    uint32_t hash = 2166136261u;
    for (uint8_t byte : g_bios) hash = (hash ^ byte) * 16777619u;
    return hash;
}

uint32_t count_pcm_flips(size_t frames)
{
    uint32_t flips = 0;
    for (size_t i = 0; i < frames; ++i) {
        const int16_t sample = g_audio_samples[i * 2];
        flips += sample != g_previous_pcm_sample;
        g_previous_pcm_sample = sample;
    }
    return flips;
}

void replace_with_probe_tone(size_t frames)
{
    for (size_t i = 0; i < frames; ++i) {
        g_probe_phase += kProbeFrequency;
        if (g_probe_phase >= kAudioSampleRate) g_probe_phase -= kAudioSampleRate;
        const int16_t sample = g_probe_phase < kAudioSampleRate / 2 ?
            kProbeAmplitude : -kProbeAmplitude;
        g_audio_samples[i * 2] = sample;
        g_audio_samples[i * 2 + 1] = sample;
    }
}

#ifdef GMASTER_AUDIO_DIAG
const char *audio_diag_phase_name(uint8_t phase)
{
    static const char *const names[kAudioDiagPhaseCount] = {
        "GAME LOG ON", "ZERO LOG ON", "DC LOG ON",
        "ZERO LOG OFF", "GAME LOG OFF"
    };
    return names[phase];
}

void draw_audio_diag_header()
{
    const uint16_t dark = jack_wave1_rgb555(1, 4, 2);
    const uint16_t green = jack_wave1_rgb555(18, 27, 12);
    jack_wave1_fill_rect(0, 0, JACK_WAVE1_SCREEN_WIDTH, 25, dark);
    jack_wave1_draw_text(38, 10, audio_diag_phase_name(g_audio_diag_phase),
                         green, dark);
}

void audio_diag_advance(uint32_t now)
{
    const uint32_t elapsed = now - g_audio_diag_phase_start;
    const uint32_t fps100 = elapsed ?
        static_cast<uint32_t>(static_cast<uint64_t>(g_audio_diag_frames) * 100000000u / elapsed) : 0;
    const uint32_t frames = g_audio_diag_frames ? g_audio_diag_frames : 1u;
    gmaster_log_printf("GMASTER_AUDIO_DIAG_END phase=%u name=%s elapsed_us=%lu frames=%lu fps=%lu.%02lu core_mean_us=%lu draw_mean_us=%lu wait_mean_us=%lu late=%lu edges=%lu edge_frames=%lu pcm_flips=%lu max_edges_frame=%lu\n",
                       g_audio_diag_phase, audio_diag_phase_name(g_audio_diag_phase),
                       static_cast<unsigned long>(elapsed),
                       static_cast<unsigned long>(g_audio_diag_frames),
                       static_cast<unsigned long>(fps100 / 100),
                       static_cast<unsigned long>(fps100 % 100),
                       static_cast<unsigned long>(g_audio_diag_core_us / frames),
                       static_cast<unsigned long>(g_audio_diag_draw_us / frames),
                       static_cast<unsigned long>(g_audio_diag_wait_us / frames),
                       static_cast<unsigned long>(g_audio_diag_late_frames),
                       static_cast<unsigned long>(g_audio_diag_edges),
                       static_cast<unsigned long>(g_audio_diag_edge_frames),
                       static_cast<unsigned long>(g_audio_diag_flips),
                       static_cast<unsigned long>(g_audio_diag_max_edges));
    g_audio_diag_phase = (g_audio_diag_phase + 1u) % kAudioDiagPhaseCount;
    g_audio_diag_phase_start = now;
    g_audio_diag_frames = g_audio_diag_edge_frames = g_audio_diag_edges = 0;
    g_audio_diag_flips = g_audio_diag_max_edges = 0;
    g_audio_diag_core_us = g_audio_diag_draw_us = g_audio_diag_wait_us = 0;
    g_audio_diag_late_frames = 0;
    draw_audio_diag_header();
    gmaster_log_printf("GMASTER_AUDIO_DIAG_BEGIN phase=%u name=%s duration_s=15\n",
                       g_audio_diag_phase, audio_diag_phase_name(g_audio_diag_phase));
}

void audio_diag_discard_log(const char *) {}
#endif

void draw_static()
{
    const uint16_t dark = jack_wave1_rgb555(1, 4, 2);
    const uint16_t green = jack_wave1_rgb555(18, 27, 12);
    jack_wave1_clear(dark);
#if defined(GMASTER_NO_AUDIO_PROBE)
    jack_wave1_draw_text(80, 10, "GAME MASTER CPU TEST", green, dark);
#elif defined(GMASTER_AUDIO_DIAG)
    jack_wave1_draw_text(38, 10, "AUDIO DIAG: PROBE 1KHZ", green, dark);
#elif defined(GMASTER_LCD_TEST_64_COLUMNS)
    jack_wave1_draw_text(80, 10, "GAME MASTER 64COL", green, dark);
#elif defined(GMASTER_LCD_TEST_62_COLUMNS)
    jack_wave1_draw_text(80, 10, "GAME MASTER 62COL", green, dark);
#else
    jack_wave1_draw_text(92, 10, "GAME MASTER", green, dark);
#endif
    jack_wave1_draw_text(40, 222, "D-PAD A/B SELECT START", green, dark);
    std::memset(g_previous, 0xff, sizeof(g_previous));
}

void draw_lcd()
{
    gmaster_board_render(&g_board, g_frame);
    constexpr unsigned scale = 3;
    constexpr unsigned left = (JACK_WAVE1_SCREEN_WIDTH - GMASTER_LCD_WIDTH * scale) / 2;
    constexpr unsigned top = 27;
    const uint16_t off = jack_wave1_rgb555(4, 8, 3);
    const uint16_t on = jack_wave1_rgb555(23, 31, 13);
    for (unsigned y = 0; y < GMASTER_LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < GMASTER_LCD_WIDTH; ++x) {
            if (g_previous[y][x] == g_frame[y][x]) continue;
            jack_wave1_fill_rect(left + x * scale, top + y * scale, scale, scale,
                                 g_frame[y][x] ? on : off);
            g_previous[y][x] = g_frame[y][x];
        }
    }
}
}

int main()
{
    if (!jack_wave1_init("GAME MASTER")) for (;;) tight_loop_contents();
    std::printf("GMASTER_BOOT version=" GMASTER_BUILD_VERSION " cpu=upd78c11 "
                "clock=12000000 target_fps=60 audio=hdmi-44100\n");
    if (!jack_wave1_mount_sd()) {
        jack_wave1_show_error("GAME MASTER", "SD mount failed");
        for (;;) tight_loop_contents();
    }
    jack_wave1_set_picker_auto_select_single(true);
    if (!jack_wave1_select_file("SELECT 4K BIOS", "/BIOS/GMASTER", ".bin",
                                g_bios_path, sizeof(g_bios_path)) ||
        !load_exact(g_bios_path, g_bios, sizeof(g_bios))) {
        jack_wave1_show_error("GAME MASTER", "BIOS must be 4096 bytes");
        for (;;) tight_loop_contents();
    }
    jack_wave1_set_picker_auto_select_single(false);
    if (!jack_wave1_select_file("SELECT 8/16/32K CART", "/roms/GMASTER", ".bin",
                                g_cart_path, sizeof(g_cart_path)) ||
        !load_cart(g_cart_path)) {
        jack_wave1_show_error("GAME MASTER", "Cart must be 8, 16 or 32 KiB");
        for (;;) tight_loop_contents();
    }

    std::memset(g_internal_ram, 0, sizeof(g_internal_ram));
    gmaster_board_reset(&g_board);
    draw_static();
    jack_wave1_wait_buttons_released();
    MachineBus bus;
    GMasterUpd7810 cpu(bus);
    cpu.set_rom_windows(g_bios, g_cart, g_cart_mask);
    if (!jack_wave1_audio_begin(kAudioSampleRate)) {
        jack_wave1_show_error("GAME MASTER", "HDMI audio init failed");
        for (;;) tight_loop_contents();
    }
    g_perf.begin();
#if defined(GMASTER_LCD_TEST_62_COLUMNS) || defined(GMASTER_LCD_TEST_64_COLUMNS)
    std::printf("GMASTER_START bios=%s bios_fnv=%08lx cart=%s cart_bytes=%lu lcd=%ux64 "
                "audio=hdmi-44100 cpu_placement=sram\n", g_bios_path,
                static_cast<unsigned long>(bios_fingerprint()), g_cart_path,
                static_cast<unsigned long>(g_cart_size), GMASTER_LCD_WIDTH);
#else
    std::printf("GMASTER_START bios=%s bios_fnv=%08lx cart=%s cart_bytes=%lu lcd=61x64 "
                "audio=hdmi-44100 cpu_placement=sram\n", g_bios_path,
                static_cast<unsigned long>(bios_fingerprint()), g_cart_path,
                static_cast<unsigned long>(g_cart_size));
#endif
    gmaster_log_start();
#ifndef GMASTER_NO_AUDIO_PROBE
    gmaster_log_printf("GMASTER_AUDIO_PROBE source=independent-square-wave hz=1000 duration_s=3\n");
#endif

    uint32_t frame_index = 0;
#ifdef GMASTER_WINDOW_STATS
    uint32_t window_start = time_us_32();
    uint32_t window_frames = 0, window_late = 0, window_slow = 0;
    uint32_t window_step_max = 0, window_edges = 0, window_edge_frames = 0;
    uint32_t window_flips = 0, window_max_edges = 0, window_service_max = 0;
    uint16_t window_slow_end_pc = 0;
#endif
    for (;;) {
#ifdef GMASTER_AUDIO_DIAG
        if (frame_index > kProbeFrames &&
            time_us_32() - g_audio_diag_phase_start >= kAudioDiagPhaseUs)
            audio_diag_advance(time_us_32());
#endif
        const uint32_t video_frame = jack_wave1_frame_counter();
        const uint32_t core_start = time_us_32();
        g_buttons = jack_wave1_poll_buttons();
        const size_t audio_frames = run_cpu_audio_frame(cpu);
        const uint32_t source_edges = static_cast<uint32_t>(g_speaker_transition_count);
        const uint32_t source_pcm_flips = count_pcm_flips(audio_frames);
        const uint32_t core_end = time_us_32();
        const bool probe = frame_index < kProbeFrames;
        if (probe) replace_with_probe_tone(audio_frames);
#ifdef GMASTER_AUDIO_DIAG
        else {
            ++g_audio_diag_frames;
            g_audio_diag_core_us += core_end - core_start;
            g_audio_diag_edges += source_edges;
            g_audio_diag_edge_frames += source_edges != 0;
            g_audio_diag_flips += source_pcm_flips;
            g_audio_diag_max_edges = std::max(g_audio_diag_max_edges, source_edges);
            if (g_audio_diag_phase == 1u || g_audio_diag_phase == 3u) {
                std::memset(g_audio_samples, 0, audio_frames * 2 * sizeof(int16_t));
            } else if (g_audio_diag_phase == 2u) {
                for (size_t i = 0; i < audio_frames * 2; ++i)
                    g_audio_samples[i] = -kSpeakerAmplitude;
            }
        }
#endif
        jack_wave1_audio_submit(g_audio_samples, audio_frames);
#ifndef GMASTER_NO_AUDIO_PROBE
        if (++frame_index == kProbeFrames) {
            gmaster_log_printf("GMASTER_AUDIO_PROBE ended source=emulated-speaker\n");
#ifdef GMASTER_AUDIO_DIAG
            g_audio_diag_phase_start = time_us_32();
            draw_audio_diag_header();
            gmaster_log_printf("GMASTER_AUDIO_DIAG_BEGIN phase=0 name=%s duration_s=15\n",
                               audio_diag_phase_name(g_audio_diag_phase));
#endif
        }
#else
        ++frame_index;
#endif
        draw_lcd();
        const uint32_t draw_end = time_us_32();
        const bool waited = jack_wave1_wait_frame_boundary(video_frame);
        const uint32_t wait_end = time_us_32();
#ifdef GMASTER_AUDIO_DIAG
        if (!probe) {
            g_audio_diag_draw_us += draw_end - core_end;
            g_audio_diag_wait_us += wait_end - draw_end;
            g_audio_diag_late_frames += !waited;
        }
#endif
        jack_wave1_usb_service();
#ifdef GMASTER_WINDOW_STATS
        ++window_frames;
        window_late += !waited;
        window_slow += g_step_us >= 16000u;
        if (g_step_us > window_step_max) {
            window_step_max = g_step_us;
            window_slow_end_pc = cpu.pc();
        }
        window_edges += source_edges;
        window_edge_frames += source_edges != 0;
        window_flips += source_pcm_flips;
        window_max_edges = std::max(window_max_edges, source_edges);
#endif
#ifdef GMASTER_AUDIO_DIAG
        const bool quiet_log = frame_index > kProbeFrames &&
                               g_audio_diag_phase >= 3u;
        if (!quiet_log) jack_wave1_audio_report();
#else
        jack_wave1_audio_report();
#endif
        char state[128];
        std::snprintf(state, sizeof(state),
                      "pc=%04x spk=%u edges=%lu pcm_flips=%lu probe=%u step_us=%lu synth_us=%lu late=%u aud_drop=%lu log_drop=%lu", cpu.pc(),
                      g_board.speaker_high ? 1u : 0u,
                      static_cast<unsigned long>(source_edges),
                      static_cast<unsigned long>(source_pcm_flips), probe ? 1u : 0u,
                      static_cast<unsigned long>(g_step_us),
                      static_cast<unsigned long>(g_synth_us), waited ? 0u : 1u,
                      static_cast<unsigned long>(g_speaker_transition_dropped),
                      static_cast<unsigned long>(gmaster_log_dropped()));
        g_perf.sample("GMASTER", core_end - core_start, draw_end - core_end,
                      wait_end - draw_end, g_buttons, state,
#ifdef GMASTER_AUDIO_DIAG
                      quiet_log ? audio_diag_discard_log : gmaster_log_write);
#else
                      gmaster_log_write);
#endif
        gmaster_log_pump();
#ifdef GMASTER_WINDOW_STATS
        window_service_max = std::max(window_service_max, time_us_32() - wait_end);
        const uint32_t window_now = time_us_32();
        if (window_now - window_start >= 1000000u) {
            gmaster_log_printf("GMASTER_WINDOW frames=%lu elapsed_us=%lu late_frames=%lu slow_frames=%lu step_max_us=%lu slow_end_pc=%04x service_max_us=%lu edges=%lu edge_frames=%lu pcm_flips=%lu max_edges_frame=%lu\n",
                static_cast<unsigned long>(window_frames),
                static_cast<unsigned long>(window_now - window_start),
                static_cast<unsigned long>(window_late),
                static_cast<unsigned long>(window_slow),
                static_cast<unsigned long>(window_step_max), window_slow_end_pc,
                static_cast<unsigned long>(window_service_max),
                static_cast<unsigned long>(window_edges),
                static_cast<unsigned long>(window_edge_frames),
                static_cast<unsigned long>(window_flips),
                static_cast<unsigned long>(window_max_edges));
            window_start = window_now;
            window_frames = window_late = window_slow = window_step_max = 0;
            window_edges = window_edge_frames = window_flips = window_max_edges = 0;
            window_service_max = 0;
        }
#endif
    }
}
