
#pragma once
#include <cstdio>
#include "pico/stdlib.h"
#include "jack_wave1_runtime.h"
#ifdef JACK_MEGADUCK_OPT047
#include "megaduck_opt047.h"
#endif
#ifdef JACK_MEGADUCK_PACE036
#include "megaduck_pace036.h"
#endif
#ifdef JACK_MEGADUCK_TIMA037
#include "megaduck_halt017.h"
#endif

#ifndef JACK_WAVE1_PERF_INTERVAL_US
#define JACK_WAVE1_PERF_INTERVAL_US 1000000u
#endif

struct JackWave1Perf {
#ifdef JACK_MEGADUCK_OPT047
    uint32_t previous047 = 0;
    bool previous_valid047 = false;
#endif
    uint32_t start = 0, video = 0, frames = 0;
    uint64_t core = 0, draw = 0, wait = 0;
#ifdef JACK_MEGADUCK_TIMA037
    uint64_t run = 0, audio = 0;
#endif
    void begin() { start = time_us_32(); video = jack_wave1_frame_counter(); }
    void sample(const char *name, uint32_t core_us, uint32_t draw_us,
                uint32_t wait_us, uint16_t buttons, const char *state,
                void (*sink)(const char *)
#ifdef JACK_MEGADUCK_TIMA037
                , uint32_t run_us, uint32_t audio_us
#endif
                ) {
        ++frames; core += core_us; draw += draw_us; wait += wait_us;
#ifdef JACK_MEGADUCK_TIMA037
        run += run_us; audio += audio_us;
#endif
        const uint32_t now = time_us_32(), elapsed = now - start;
#ifdef JACK_MEGADUCK_OPT047
        megaduck_metrics047_sample(run_us, previous_valid047 ? now - previous047 : 0u);
        previous047 = now;
        previous_valid047 = true;
#endif
        if (elapsed < JACK_WAVE1_PERF_INTERVAL_US) return;
        const uint32_t current_video = jack_wave1_frame_counter();
        const uint32_t fps100 = static_cast<uint32_t>(uint64_t(frames) * 100000000u / elapsed);
        const uint32_t video_fps100 = static_cast<uint32_t>(uint64_t(current_video - video) * 100000000u / elapsed);
#ifdef JACK_MEGADUCK_PACE036
        const megaduck_pace036_stats_t pace = megaduck_pace036_take_stats();
#ifdef JACK_MEGADUCK_TIMA037
        jack_megaduck_halt017_stats_t halt{};
        jack_megaduck_halt017_take(&halt);
        char line[576];
#else
        char line[448];
#endif
#else
        char line[320];
#endif
        snprintf(line, sizeof(line),
            "%s_PERF state=%s updates=%lu elapsed_us=%lu fps=%lu.%02lu video_fps=%lu.%02lu video_frames=%lu core_mean_us=%lu draw_mean_us=%lu wait_mean_us=%lu buttons=%04x usb_reports=%lu"
#ifdef JACK_MEGADUCK_PACE036
            " pace_late=%lu pace_rebase=%lu pace_max_late_us=%lu"
#endif
#ifdef JACK_MEGADUCK_TIMA037
            " run_mean_us=%lu audio_mean_us=%lu halt_batches=%lu timer_fast=%lu"
#endif
            "\n",
            name, state, (unsigned long)frames, (unsigned long)elapsed,
            (unsigned long)(fps100 / 100), (unsigned long)(fps100 % 100),
            (unsigned long)(video_fps100 / 100), (unsigned long)(video_fps100 % 100),
            (unsigned long)(current_video - video), (unsigned long)(core / frames),
            (unsigned long)(draw / frames), (unsigned long)(wait / frames), buttons,
            (unsigned long)jack_wave1_usb_report_count()
#ifdef JACK_MEGADUCK_PACE036
            , (unsigned long)pace.late_frames, (unsigned long)pace.rebases,
            (unsigned long)pace.max_late_us
#endif
#ifdef JACK_MEGADUCK_TIMA037
            , (unsigned long)(run / frames), (unsigned long)(audio / frames),
            (unsigned long)halt.batches, (unsigned long)halt.timer_fast_calls
#endif
            );
        sink(line);
#ifdef JACK_MEGADUCK_OPT047
        megaduck_metrics047_report();
#endif
        start = now; video = current_video; frames = 0; core = draw = wait = 0;
#ifdef JACK_MEGADUCK_TIMA037
        run = audio = 0;
#endif
    }
};
inline void jack_wave1_perf_stdout(const char *line) {
#ifdef JACK_MEGADUCK_OPT047
    megaduck_log047_write(line);
#else
    printf("%s", line);
#endif
}
