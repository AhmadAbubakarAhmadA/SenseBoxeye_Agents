#include "event_buffer.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static vibration_event_t s_buf[EVENT_BUF_SIZE];
static int s_head = 0;   // next write index
static int s_count = 0;
static SemaphoreHandle_t s_lock;

void event_buffer_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_head = 0;
    s_count = 0;
}

void event_buffer_push(const vibration_event_t *evt)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_buf[s_head] = *evt;
    s_head = (s_head + 1) % EVENT_BUF_SIZE;
    if (s_count < EVENT_BUF_SIZE) s_count++;
    xSemaphoreGive(s_lock);
}

// Internal: copy events oldest-first into out. Caller holds lock.
static int copy_events(vibration_event_t *out, int max_out, int64_t since_ms)
{
    int start = (s_count < EVENT_BUF_SIZE) ? 0 : s_head;
    int64_t cutoff = 0;
    if (since_ms > 0) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        cutoff = now_ms - since_ms;
    }
    int copied = 0;
    for (int i = 0; i < s_count && copied < max_out; i++) {
        int idx = (start + i) % EVENT_BUF_SIZE;
        if (since_ms > 0 && s_buf[idx].timestamp_ms < cutoff) continue;
        out[copied++] = s_buf[idx];
    }
    return copied;
}

int event_buffer_get(vibration_event_t *out, int max_out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = copy_events(out, max_out, 0);
    xSemaphoreGive(s_lock);
    return n;
}

int event_buffer_get_since(vibration_event_t *out, int max_out, int64_t since_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = copy_events(out, max_out, since_ms);
    xSemaphoreGive(s_lock);
    return n;
}

int event_buffer_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count;
    xSemaphoreGive(s_lock);
    return n;
}

int64_t event_buffer_last_ts(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t ts = 0;
    if (s_count > 0) {
        int last_idx = (s_head - 1 + EVENT_BUF_SIZE) % EVENT_BUF_SIZE;
        ts = s_buf[last_idx].timestamp_ms;
    }
    xSemaphoreGive(s_lock);
    return ts;
}
