#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_BUF_SIZE 50

typedef struct {
    int64_t  timestamp_ms;   // uptime ms (esp_timer_get_time / 1000)
    float    magnitude;      // accel deviation from 1g (in g)
    char     type[16];       // "door_event"
} vibration_event_t;

// Initialize the event buffer.
void event_buffer_init(void);

// Push an event (thread-safe). Oldest event is discarded if buffer is full.
void event_buffer_push(const vibration_event_t *evt);

// Copy up to max_out events into out[], ordered oldest-first.
// Returns the number of events copied.
int event_buffer_get(vibration_event_t *out, int max_out);

// Copy events from the last since_ms milliseconds.
int event_buffer_get_since(vibration_event_t *out, int max_out, int64_t since_ms);

// Number of events currently stored.
int event_buffer_count(void);

// Timestamp of the most recent event, or 0 if empty.
int64_t event_buffer_last_ts(void);

#ifdef __cplusplus
}
#endif
