#include "ui_bridge.h"
#include "ui_bridge_priv.h"
#include "config.h"
#include "board.h"
#include "wifi_board.h"
#include "display/emote_display.h"
#include "application.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <lvgl.h>
#include <esp_lv_adapter.h>

#define TAG "ui_bridge"

/* Define center coordinates based on display dimensions */
#define UI_BRIDGE_CENTER_X                       (DISPLAY_WIDTH / 2)   /* Center X coordinate */
#define UI_BRIDGE_CENTER_Y                       (DISPLAY_HEIGHT / 2)  /* Center Y coordinate */

/* Internal state */
static lv_obj_t *s_base_container = NULL;
static lv_obj_t *s_center_icon = NULL;

static void ui_bridge_base_container_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        auto &app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateStarting &&
                !WifiStation::GetInstance().IsConnected()) {
            static_cast<WifiBoard &>(Board::GetInstance()).ResetWifiConfiguration();
        } else {
            app.ToggleChatState();
        }
    }
}

void ui_bridge_init(emote::EmoteDisplay *display)
{
    if (display) {
        ui_bridge_set_emote_display(display);
        ESP_LOGI(TAG, "Cached emote display pointer: %p", (void*)display);
    }

    /* Create base emote UI container */
    s_base_container = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_base_container);
    lv_obj_set_size(s_base_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_align(s_base_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_base_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_base_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_base_container, LV_OBJ_FLAG_CLICKABLE);  /* Disable click on full screen */

    /* Register base container as default page */
    ui_bridge_register_page(UI_BRIDGE_PAGE_HOME, &s_base_container, true);
    ui_bridge_switch_page(UI_BRIDGE_PAGE_HOME);  /* Set as default page */

    s_center_icon = lv_obj_create(s_base_container);
    lv_obj_remove_style_all(s_center_icon);
    lv_obj_set_size(s_center_icon, 150, 150);
    lv_obj_align(s_center_icon, LV_ALIGN_CENTER, 0, 0);  /* Center the icon */
    lv_obj_set_style_bg_opa(s_center_icon, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_center_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_center_icon, LV_OBJ_FLAG_CLICKABLE);
    /* Only monitor events on the center icon */
    lv_obj_add_event_cb(s_center_icon, ui_bridge_base_container_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "UI bridge initialized for %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}
