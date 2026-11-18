#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHEDULER_EVENT_PRICE_SPIKE 1
#define SCHEDULER_EVENT_MARKET_CLOSED 2

#define SCHEDULER_DEFAULT_COOLDOWN_SEC 14400 /* 4 hours (240 min) */

typedef void (*scheduler_event_listener_t)(int         event_type,
                                           const char* key,
                                           double      val,
                                           void*       user_data);

/**
 * Check if the given event key is currently within its cooldown window.
 * If not in cooldown, updates the last triggered timestamp to now and returns true.
 * If in cooldown, returns false.
 */
bool scheduler_event_check_and_update_cooldown(const char* key, int cooldown_sec);

/**
 * Subscribe to market events.
 */
void scheduler_event_subscribe(scheduler_event_listener_t listener, void* user_data);

/**
 * Publish a price spike event for an asset / symbol.
 */
void scheduler_event_publish_price_spike(int64_t asset_id, const char* symbol, double change_pct);

/**
 * Publish a market closed event (e.g. market_code = "CN", "US", "HK").
 */
void scheduler_event_publish_market_closed(const char* market_code);

/**
 * Reset all event subscribers and cooldown state (for unit testing and shutdown).
 */
void scheduler_event_reset(void);

#ifdef __cplusplus
}
#endif
