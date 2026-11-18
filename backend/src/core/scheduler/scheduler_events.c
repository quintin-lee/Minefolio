#include "core/scheduler/scheduler_events.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COOLDOWN_ENTRIES 256
#define MAX_SUBSCRIBERS 16

typedef struct {
    char   key[128];
    time_t last_triggered;
} cooldown_entry_t;

static cooldown_entry_t           s_cooldowns[MAX_COOLDOWN_ENTRIES];
static int                        s_num_cooldowns = 0;
static scheduler_event_listener_t s_subscribers[MAX_SUBSCRIBERS];
static void*                      s_sub_data[MAX_SUBSCRIBERS];
static int                        s_num_subscribers = 0;
static pthread_mutex_t            s_events_lock = PTHREAD_MUTEX_INITIALIZER;

bool
scheduler_event_check_and_update_cooldown(const char* key, int cooldown_sec)
{
    if (!key || !*key) {
        return false;
    }
    if (cooldown_sec <= 0) {
        cooldown_sec = SCHEDULER_DEFAULT_COOLDOWN_SEC;
    }

    time_t now = time(NULL);

    pthread_mutex_lock(&s_events_lock);

    /* Look for existing key */
    for (int i = 0; i < s_num_cooldowns; i++) {
        if (strcmp(s_cooldowns[i].key, key) == 0) {
            if (now - s_cooldowns[i].last_triggered < cooldown_sec) {
                pthread_mutex_unlock(&s_events_lock);
                return false; /* Within cooldown period */
            }
            s_cooldowns[i].last_triggered = now;
            pthread_mutex_unlock(&s_events_lock);
            return true;
        }
    }

    /* Key not found; insert */
    if (s_num_cooldowns < MAX_COOLDOWN_ENTRIES) {
        snprintf(s_cooldowns[s_num_cooldowns].key, sizeof(s_cooldowns[0].key), "%s", key);
        s_cooldowns[s_num_cooldowns].last_triggered = now;
        s_num_cooldowns++;
    } else {
        /* Evict oldest or overwrite slot 0 */
        snprintf(s_cooldowns[0].key, sizeof(s_cooldowns[0].key), "%s", key);
        s_cooldowns[0].last_triggered = now;
    }

    pthread_mutex_unlock(&s_events_lock);
    return true;
}

void
scheduler_event_subscribe(scheduler_event_listener_t listener, void* user_data)
{
    if (!listener) {
        return;
    }

    pthread_mutex_lock(&s_events_lock);
    for (int i = 0; i < s_num_subscribers; i++) {
        if (s_subscribers[i] == listener && s_sub_data[i] == user_data) {
            pthread_mutex_unlock(&s_events_lock);
            return;
        }
    }

    if (s_num_subscribers < MAX_SUBSCRIBERS) {
        s_subscribers[s_num_subscribers] = listener;
        s_sub_data[s_num_subscribers] = user_data;
        s_num_subscribers++;
    }
    pthread_mutex_unlock(&s_events_lock);
}

void
scheduler_event_publish_price_spike(int64_t asset_id, const char* symbol, double change_pct)
{
    char key[128];
    snprintf(key, sizeof(key), "spike_%s_%lld", symbol ? symbol : "unknown", (long long)asset_id);

    if (!scheduler_event_check_and_update_cooldown(key, SCHEDULER_DEFAULT_COOLDOWN_SEC)) {
        return; /* In cooldown */
    }

    pthread_mutex_lock(&s_events_lock);
    int                        count = s_num_subscribers;
    scheduler_event_listener_t subs[MAX_SUBSCRIBERS];
    void*                      datas[MAX_SUBSCRIBERS];
    for (int i = 0; i < count; i++) {
        subs[i] = s_subscribers[i];
        datas[i] = s_sub_data[i];
    }
    pthread_mutex_unlock(&s_events_lock);

    for (int i = 0; i < count; i++) {
        if (subs[i]) {
            subs[i](SCHEDULER_EVENT_PRICE_SPIKE, symbol ? symbol : "", change_pct, datas[i]);
        }
    }
}

void
scheduler_event_publish_market_closed(const char* market_code)
{
    char key[128];
    snprintf(key, sizeof(key), "market_closed_%s", market_code ? market_code : "ALL");

    if (!scheduler_event_check_and_update_cooldown(key, SCHEDULER_DEFAULT_COOLDOWN_SEC)) {
        return; /* In cooldown */
    }

    pthread_mutex_lock(&s_events_lock);
    int                        count = s_num_subscribers;
    scheduler_event_listener_t subs[MAX_SUBSCRIBERS];
    void*                      datas[MAX_SUBSCRIBERS];
    for (int i = 0; i < count; i++) {
        subs[i] = s_subscribers[i];
        datas[i] = s_sub_data[i];
    }
    pthread_mutex_unlock(&s_events_lock);

    for (int i = 0; i < count; i++) {
        if (subs[i]) {
            subs[i](SCHEDULER_EVENT_MARKET_CLOSED, market_code ? market_code : "", 0.0, datas[i]);
        }
    }
}

void
scheduler_event_reset(void)
{
    pthread_mutex_lock(&s_events_lock);
    s_num_cooldowns = 0;
    s_num_subscribers = 0;
    pthread_mutex_unlock(&s_events_lock);
}
