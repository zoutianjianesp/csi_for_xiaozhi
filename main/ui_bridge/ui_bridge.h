#ifndef LVGL_DISPLAY_BRIDGE_H
#define LVGL_DISPLAY_BRIDGE_H

#include "lvgl.h"

/**
 * @file ui_bridge.h
 * @brief Bridge between LVGL and xiaozhi display module
 *
 */

/* Page name constants */
#define UI_BRIDGE_PAGE_HOME                      "DUMMY"  /* Base emote display page */

#ifdef __cplusplus
namespace emote {
    class EmoteDisplay;
}
extern "C" {
#endif

/**
 * @brief Page switch callback function type
 *
 * When a gesture navigation occurs, this callback is called with the target page ID.
 * If callback returns true, the default page switching is skipped.
 *
 * @param target_page Target page ID to switch to
 * @param user_data User data passed when registering the callback
 * @return true if handled (skip default switch), false to use default switch
 */
typedef bool (*ui_bridge_page_switch_cb_t)(const char *target_page, void *user_data);

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief Initialize LVGL display bridge
 *
 * This function initializes the bridge between LVGL and xiaozhi display module.
 * It creates the base emote container and sets up page management.
 *
 * @param display Pointer to the EmoteDisplay instance created by the board
 */
#ifdef __cplusplus
void ui_bridge_init(emote::EmoteDisplay *display);
#else
void ui_bridge_init(void *display);
#endif

/**
 * @brief Attach gesture event handler to input device
 *
 * Attaches gesture detection and page navigation handler to the LVGL input device.
 *
 * @param indev Pointer to the LVGL input device
 */
void ui_bridge_attach_gesture_handler(lv_indev_t *indev);

/**
 * @brief Register a page container for page switching with cycle control
 *
 * Registers a page container that can be switched to via gesture navigation.
 *
 * @param page_id Unique page identifier (e.g., "DUMMY", "POMODORO", "SLEEP", "PAGE_TIME_UP")
 * @param container Pointer to pointer of the container object
 * @param in_cycle Whether this page should be included in cycle navigation (true) or skipped (false)
 * @return true if registration successful, false otherwise
 */
bool ui_bridge_register_page(const char *page_id, lv_obj_t **container, bool in_cycle);

/**
 * @brief Switch to specified page
 *
 * This function handles all page visibility and state management in one call.
 *
 * @param page_id Target page ID to switch to
 */
void ui_bridge_switch_page(const char *page_id);

/**
 * @brief Get current page ID
 *
 * @return Current page ID, or NULL if no page is active
 */
const char *ui_bridge_get_current_page(void);

/**
 * @brief Set page switch callback for custom handling
 *
 * When a gesture navigation occurs, this callback is called with the target page ID.
 * If callback returns true, the default page switching is skipped.
 *
 * @param cb Callback function (NULL to disable)
 * @param user_data User data to pass to callback
 */
void ui_bridge_set_page_switch_callback(ui_bridge_page_switch_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // LVGL_DISPLAY_BRIDGE_H
