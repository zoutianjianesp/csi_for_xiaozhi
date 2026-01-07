#include <string.h>  /* For strcmp */
#include <esp_log.h>
#include <esp_lv_adapter.h>
#include "custom_ui.h"
#include "alarm_api.h"
#include "csi_ui/ui/ui.h"
#include "../esp_radar_csi.h"

#define TAG "alarm_controller"

// ============================================================================
// Type Definitions
// ============================================================================
using esp_brookesia::apps::RadarCSI;
// ============================================================================
// Static Variables
// ============================================================================

/* UI container objects (main_ui specific) */
static lv_obj_t *container_pomodoro = NULL;
static lv_obj_t *container_sleep = NULL;
static lv_obj_t *container_time_up = NULL;
static lv_obj_t *container_muyu = NULL;
static lv_obj_t *container_screenW = NULL;
static lv_obj_t *container_csi_behav = NULL;

// ============================================================================
// Global Variables
// ============================================================================

static bool main_ui_page_switch_callback(const char *target_page, void *user_data)
{
    const char *current_page = ui_bridge_get_current_page();
    ESP_LOGI(TAG, "Page switch: %s -> %s", current_page ? current_page : "NULL", target_page ? target_page : "NULL");

    /* Special handling for pomodoro page */
    if (target_page != NULL && strcmp(target_page, PAGE_POMODORO) == 0 &&
            (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0)) {
        alarm_start_pomodoro(5);
        return true;  /* Handled, skip default switch */
    }
    if (target_page != NULL && strcmp(target_page, "SCREEN_W") == 0 &&
            (current_page == NULL || strcmp(current_page, "SCREEN_W") != 0)) {
        ESP_LOGI(TAG, "Start Pinging!");
        RadarCSI::getInstance()->startPing();
    }
    if (current_page != NULL && strcmp(current_page, "SCREEN_W") == 0 &&
        (target_page == NULL || strcmp(target_page, "SCREEN_W") != 0)) {
        ESP_LOGI(TAG, "Stop Pinging!");
        RadarCSI::getInstance()->stopPing();
    }

    return false;  /* Use default switch */
}

void custom_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    /* Create and register pomodoro container */
    // container_pomodoro = alarm_pomodoro_create_with_parent(scr);
    // ui_bridge_register_page(PAGE_POMODORO, &container_pomodoro, true);

    // /* Create and register sleep container */
    // container_sleep = alarm_sleep_24h_create_with_parent(scr);
    // ui_bridge_register_page(PAGE_SLEEP, &container_sleep, true);

    // /* Create and register muuyu container */
    // container_muyu = alarm_muyu_create_with_parent(scr);
    // ui_bridge_register_page(PAGE_MUYU, &container_muyu, true);

    // /* Create and register time up container */
    // container_time_up = alarm_time_up_create_with_parent(scr);
    // ui_bridge_register_page(PAGE_TIME_UP, &container_time_up, false);
    /* Create and register screenW container */
    container_screenW = ui_ScreenW_init(scr);
    ui_bridge_register_page("SCREEN_W", &container_screenW, true);

    /* Create and register csi_light container */
    container_csi_behav = ui_csi_behav_init(scr);
    ui_bridge_register_page("CSI_BEHAV", &container_csi_behav, true);

    /* Register page switch callback for custom handling (e.g., pomodoro) */
    ui_bridge_set_page_switch_callback(main_ui_page_switch_callback, NULL);
}
