#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include "Config.h"

// Note: This is a functional-style implementation, not a class,
// to simplify integration with the global state objects.

// Alarms are managed directly on the g_alarmState struct.
// All functions that modify g_alarmState MUST be called within a mutex lock.

// Raise an alarm. If it already exists, does nothing.
static void raise_alarm(const char* code, const char* msg, bool sticky = true) {
    if (g_alarmState.count >= MAX_ACTIVE_ALARMS) {
        // Cannot add more alarms
        return;
    }

    // Check if alarm already exists
    for (int i = 0; i < g_alarmState.count; i++) {
        if (strcmp(g_alarmState.active_alarms[i].code, code) == 0) {
            return; // Already active
        }
    }

    // Add new alarm
    Alarm& new_alarm = g_alarmState.active_alarms[g_alarmState.count];
    strncpy(new_alarm.code, code, ALARM_CODE_MAX_LEN - 1);
    new_alarm.code[ALARM_CODE_MAX_LEN - 1] = '\0';
    strncpy(new_alarm.msg, msg, ALARM_MSG_MAX_LEN - 1);
    new_alarm.msg[ALARM_MSG_MAX_LEN - 1] = '\0';
    new_alarm.sticky = sticky;
    new_alarm.raised_at_ms = millis();
    
    g_alarmState.count++;

    // Update Red LED state
    g_ledState.red = true;
}

// Clear an alarm by its code.
static void clear_alarm(const char* code) {
    int found_index = -1;
    for (int i = 0; i < g_alarmState.count; i++) {
        if (strcmp(g_alarmState.active_alarms[i].code, code) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index != -1) {
        // Shift remaining alarms down
        for (int i = found_index; i < g_alarmState.count - 1; i++) {
            g_alarmState.active_alarms[i] = g_alarmState.active_alarms[i + 1];
        }
        g_alarmState.count--;
    }

    // Update Red LED state
    if (g_alarmState.count == 0) {
        g_ledState.red = false;
    }
}

#endif // ALARM_MANAGER_H
