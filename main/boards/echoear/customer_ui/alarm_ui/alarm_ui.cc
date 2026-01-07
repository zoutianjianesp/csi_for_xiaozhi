#include <string.h>
#include <esp_log.h>
#include "alarm_api.h"
#include "alarm_pomodoro.h"
#include "alarm_sleep_24h.h"
#include "alarm_end.h"
#include "ui_bridge.h"

#define TAG "alarm_ui_operations"

void alarm_start_pomodoro(int32_t minutes)
{
    if (minutes <= 0) {
        minutes = 5;
    }

    ESP_LOGI(TAG, "Show pomodoro page with %ld minutes", (long)minutes);

    /* Configure pomodoro timer first, then switch page */
    alarm_pomodoro_reset_to_zero();
    alarm_pomodoro_adjust_end_point(minutes);
    ui_bridge_switch_page(PAGE_POMODORO);
}

bool alarm_resume_pomodoro(void)
{
    ESP_LOGI(TAG, "Start pomodoro timer");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0) {
        ESP_LOGI(TAG, "Not on pomodoro page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_pomodoro_start();
    }
    return true;
}

bool alarm_pause_pomodoro(void)
{
    ESP_LOGI(TAG, "Pause pomodoro timer");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0) {
        ESP_LOGI(TAG, "Not on pomodoro page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_pomodoro_pause();
    }
    return true;
}

void alarm_start_sleep(int32_t end_hour, int32_t end_min)
{
    /* Validate and clamp values */
    const int32_t DEFAULT_END_HOUR = 8;

    if (end_hour < 0 || end_hour >= 24) {
        ESP_LOGW(TAG, "Invalid end_hour: %ld, using default %ld", (long)end_hour, (long)DEFAULT_END_HOUR);
        end_hour = DEFAULT_END_HOUR;
    }
    if (end_min < 0 || end_min >= 60) {
        ESP_LOGW(TAG, "Invalid end_min: %ld, using 0", (long)end_min);
        end_min = 0;
    }

    ESP_LOGI(TAG, "Show sleep page with end time: %02ld:%02ld (start time will be current time)",
             (long)end_hour, (long)end_min);

    /* Configure sleep timer first (start time is current time), then switch page */
    alarm_sleep_24h_set_end_time(end_hour, end_min);
    ui_bridge_switch_page(PAGE_SLEEP);
}

void alarm_set_sleep_end_time(int32_t end_hour, int32_t end_min)
{
    /* Validate and clamp values */
    const int32_t DEFAULT_END_HOUR = 8;

    if (end_hour < 0 || end_hour >= 24) {
        ESP_LOGW(TAG, "Invalid end_hour: %ld, using default %ld", (long)end_hour, (long)DEFAULT_END_HOUR);
        end_hour = DEFAULT_END_HOUR;
    }
    if (end_min < 0 || end_min >= 60) {
        ESP_LOGW(TAG, "Invalid end_min: %ld, using 0", (long)end_min);
        end_min = 0;
    }

    /* Delegate to sleep_24h module */
    alarm_sleep_24h_set_end_time(end_hour, end_min);
}

bool alarm_get_sleep_end_time(int32_t *end_hour, int32_t *end_min)
{
    /* Delegate to sleep_24h module */
    return alarm_sleep_24h_get_end_time(end_hour, end_min);
}

bool alarm_toggle_sleep_duration_display(void)
{
    ESP_LOGI(TAG, "Toggle sleep duration display");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_SLEEP) != 0) {
        ESP_LOGI(TAG, "Not on sleep page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_sleep_24h_trigger_center_btn();
    }
    return true;
}

bool alarm_time_up_snooze(void)
{
    ESP_LOGI(TAG, "Time up snooze");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_TIME_UP) != 0) {
        ESP_LOGI(TAG, "Not on time up page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_time_up_snooze_impl();
    }
    return true;
}

