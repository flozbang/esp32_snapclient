#ifndef SYSTEM_DIAGNOSTICS_H
#define SYSTEM_DIAGNOSTICS_H

#include <stdint.h>

typedef struct {
    uint32_t wifi_reconnects;
    uint32_t snapcast_reconnects;
    uint32_t rb_underflows;
    uint32_t rb_overflows;
    uint32_t stream_errors;
} system_diagnostics_t;

void system_diagnostics_init(void);
void system_diagnostics_log(void);

void system_diagnostics_wifi_reconnect(void);
void system_diagnostics_snapcast_reconnect(void);
void system_diagnostics_rb_underflow(void);
void system_diagnostics_rb_overflow(void);
void system_diagnostics_stream_error(void);

const system_diagnostics_t *system_diagnostics_get(void);

#endif