#ifndef LLMMON_PLATFORM_H
#define LLMMON_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { LINK_OK, LINK_MQTT_DOWN, LINK_WIFI_DOWN } link_state_t;
uint32_t platform_now_ms(void);      /* monotonic ms, for LVGL tick + timers */
int64_t  platform_epoch_utc(void);   /* wall-clock UTC seconds, for ages/staleness */
/* Yield to the platform for approximately ms milliseconds. On a
   cooperative/RTOS target this must actually yield so watchdogs and
   background tasks are serviced; on the desktop it is a plain sleep. */
void platform_delay_ms(uint32_t ms);
/* Publish to the broker. Returns false if the link is down or the payload
   was rejected. Implemented per-platform in sim/ and device/; tests link a stub. */
bool platform_mqtt_publish(const char* topic, const char* payload,
                           size_t len, bool retain);
/* Large, short-lived buffers (JSON parse pools for /history). On the device
   this is PSRAM, keeping internal SRAM for Wi-Fi and TLS; elsewhere malloc. */
void* platform_big_malloc(size_t n);
void* platform_big_realloc(void* p, size_t n);
void  platform_big_free(void* p);
#ifdef __cplusplus
}
#endif
#endif
