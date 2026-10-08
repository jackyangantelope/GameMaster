
#include "gmaster_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "hardware/uart.h"

namespace {
constexpr uint32_t kCapacity = 2048u;
char queue[kCapacity];
uint32_t head, tail, dropped;
bool active;

bool enqueue(const char *line, uint32_t size)
{
    if (size > kCapacity - (head - tail)) {
        ++dropped;
        return false;
    }
    for (uint32_t i = 0; i < size; ++i) queue[head++ % kCapacity] = line[i];
    return true;
}
}

extern "C" void gmaster_log_start(void)
{
    head = tail = dropped = 0;
    active = true;
}

extern "C" uint32_t gmaster_log_dropped(void) { return dropped; }

extern "C" void gmaster_log_write(const char *line)
{
    if (!active) {
        std::printf("%s", line);
        return;
    }
    enqueue(line, static_cast<uint32_t>(std::strlen(line)));
}

extern "C" int gmaster_log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (!active) {
        const int result = std::vprintf(format, args);
        va_end(args);
        return result;
    }
    char line[384];
    const int result = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (result < 0 || static_cast<size_t>(result) >= sizeof(line) ||
        !enqueue(line, static_cast<uint32_t>(result))) return -1;
    return result;
}

extern "C" void gmaster_log_pump(void)
{
    if (!active) return;

    for (unsigned i = 0; i < 32u && tail != head; ++i) {
        if (!uart_is_writable(uart_default)) break;
        uart_get_hw(uart_default)->dr = static_cast<uint8_t>(queue[tail++ % kCapacity]);
    }
}
