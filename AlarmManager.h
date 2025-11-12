#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include "Config.h"
#include <time.h>

// Note: This is a functional-style implementation, not a class,
// to simplify integration with the global state objects.
// All functions that modify state MUST be called within a mutex lock.

// Forward declaration for g_epoch_time_s
extern volatile unsigned long g_epoch_time_s;

static void add_log_entry(AlarmState& alarms, const char* code, bool cleared) {
    if (alarms.log_count < MAX_ALARM_LOGS) {
        alarms.log_count++;
    }
    
    time_t now = g_epoch_time_s;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo); // Use thread-safe version

    alarms.log[alarms.log_head].hour = timeinfo.tm_hour;
    alarms.log[alarms.log_head].minute = timeinfo.tm_min;
    alarms.log[alarms.log_head].second = timeinfo.tm_sec;
    alarms.log[alarms.log_head].cleared = cleared;
    strncpy(alarms.log[alarms.log_head].code, code, ALARM_CODE_MAX_LEN - 1);
    alarms.log[alarms.log_head].code[ALARM_CODE_MAX_LEN - 1] = '\0';

    alarms.log_head = (alarms.log_head + 1) % MAX_ALARM_LOGS;
}


// Raise an alarm. If it already exists, does nothing.
static void raise_alarm(AlarmState& alarms, LEDState& leds, const char* code, const char* msg, bool sticky = true) {
    if (alarms.count >= MAX_ACTIVE_ALARMS) {
        return; // Cannot add more alarms
    }

    for (int i = 0; i < alarms.count; i++) {
        if (strcmp(alarms.active_alarms[i].code, code) == 0) {
            return; // Already active
        }
    }

    // Add new alarm
    Alarm& new_alarm = alarms.active_alarms[alarms.count];
    strncpy(new_alarm.code, code, ALARM_CODE_MAX_LEN - 1);
    new_alarm.code[ALARM_CODE_MAX_LEN - 1] = '\0';
    strncpy(new_alarm.msg, msg, ALARM_MSG_MAX_LEN - 1);
    new_alarm.msg[ALARM_MSG_MAX_LEN - 1] = '\0';
    new_alarm.sticky = sticky;
    new_alarm.raised_at_ms = millis();
    
    alarms.count++;
    leds.red = true;

    add_log_entry(alarms, code, false);
}

// Clear an alarm by its code.
static void clear_alarm(AlarmState& alarms, LEDState& leds, const char* code) {
    int found_index = -1;
    for (int i = 0; i < alarms.count; i++) {
        if (strcmp(alarms.active_alarms[i].code, code) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index != -1) {
        add_log_entry(alarms, code, true);
        for (int i = found_index; i < alarms.count - 1; i++) {
            alarms.active_alarms[i] = alarms.active_alarms[i + 1];
        }
        alarms.count--;
    }

    if (alarms.count == 0) {
        leds.red = false;
    }
}

#endif // ALARM_MANAGER_H