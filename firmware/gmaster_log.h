
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gmaster_log_start(void);
void gmaster_log_pump(void);
uint32_t gmaster_log_dropped(void);
int gmaster_log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void gmaster_log_write(const char *line);

#ifdef __cplusplus
}
#endif
