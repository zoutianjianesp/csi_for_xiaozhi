#include "ui_bridge.h"
#include "ui_bridge_priv.h"
#include "config.h"
#include <esp_log.h>
#include <lvgl.h>

#define TAG "ui_bridge"

/* Define center coordinates based on display dimensions */
#define UI_BRIDGE_CENTER_X                       (DISPLAY_WIDTH / 2)   /* Center X coordinate */
#define UI_BRIDGE_CENTER_Y                       (DISPLAY_HEIGHT / 2)  /* Center Y coordinate */

/* Internal gesture detection state structure */
typedef struct {
    bool active;
    bool handled;
    lv_coord_t start_x;
    lv_coord_t start_y;
    uint32_t press_start_time;
} ui_bridge_gesture_state_t;

/* Gesture detection state */
static ui_bridge_gesture_state_t s_gesture_state = {
    .active = false,
    .handled = false,
    .start_x = 0,
    .start_y = 0,
    .press_start_time = 0,
};

/* Forward declarations */
static bool ui_bridge_check_gesture_start_position(ui_bridge_gesture_type_t gesture, lv_coord_t start_x, lv_coord_t start_y);

/**
 * @brief Check if gesture start position is valid for the given gesture direction
 *
 * Valid start positions:
 * - SWIPE_UP:   Bottom edge (y > DISPLAY_HEIGHT - EDGE_THRESHOLD) and center X (±CENTER_RANGE)
 * - SWIPE_DOWN: Top edge (y < EDGE_THRESHOLD) and center X (±CENTER_RANGE)
 * - SWIPE_LEFT: Right edge (x > DISPLAY_WIDTH - EDGE_THRESHOLD) and center Y (±CENTER_RANGE)
 * - SWIPE_RIGHT: Left edge (x < EDGE_THRESHOLD) and center Y (±CENTER_RANGE)
 */
static bool ui_bridge_check_gesture_start_position(ui_bridge_gesture_type_t gesture, lv_coord_t start_x, lv_coord_t start_y)
{
    bool result = false;
    switch (gesture) {
    case UI_BRIDGE_GESTURE_SWIPE_UP:
        /* Must start from bottom edge and center X */
        result = (start_y > (DISPLAY_HEIGHT - UI_BRIDGE_EDGE_THRESHOLD)) &&
                 (LV_ABS(start_x - UI_BRIDGE_CENTER_X) <= UI_BRIDGE_CENTER_RANGE);
        break;

    case UI_BRIDGE_GESTURE_SWIPE_DOWN:
        /* Must start from top edge and center X */
        result = (start_y < UI_BRIDGE_EDGE_THRESHOLD) &&
                 (LV_ABS(start_x - UI_BRIDGE_CENTER_X) <= UI_BRIDGE_CENTER_RANGE);
        break;

    case UI_BRIDGE_GESTURE_SWIPE_LEFT:
        /* Must start from right edge and center Y */
        result = (start_x > (DISPLAY_WIDTH - UI_BRIDGE_EDGE_THRESHOLD)) &&
                 (LV_ABS(start_y - UI_BRIDGE_CENTER_Y) <= UI_BRIDGE_CENTER_RANGE);
        break;

    case UI_BRIDGE_GESTURE_SWIPE_RIGHT:
        /* Must start from left edge and center Y */
        result = (start_x < UI_BRIDGE_EDGE_THRESHOLD) &&
                 (LV_ABS(start_y - UI_BRIDGE_CENTER_Y) <= UI_BRIDGE_CENTER_RANGE);
        break;

    default:
        result = true;  /* No position requirement for other gestures */
        break;
    }
    return result;
}

/* Touch gesture event callback */
static void ui_bridge_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    ui_bridge_gesture_state_t *state = &s_gesture_state;

    switch (code) {
    case LV_EVENT_PRESSED:
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            state->active = true;
            state->handled = false;
            state->start_x = p.x;
            state->start_y = p.y;
            state->press_start_time = lv_tick_get();
            ESP_LOGD(TAG, "press at (%ld, %ld)", (long)p.x, (long)p.y);
        } else {
            ESP_LOGW(TAG, "press but indev is NULL");
        }
        break;

    case LV_EVENT_PRESSING: {
        if (!state->active || state->handled || !indev) {
            break;
        }
        break;
    }

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: {
        if (!state->active || !indev) {
            state->active = false;
            state->handled = false;
            break;
        }

        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_coord_t dx = p.x - state->start_x;
        lv_coord_t dy = p.y - state->start_y;
        uint32_t press_duration = lv_tick_elaps(state->press_start_time);

        ESP_LOGD(TAG, "release: dx=%ld, dy=%ld, duration=%lu ms, handled=%d",
                 (long)dx, (long)dy, press_duration, state->handled);

        /* Check for swipe gesture on release (if not already handled) */
        if (!state->handled) {
            /* Only recognize as swipe if one axis exceeds threshold while the other doesn't */
            /* If both exceed threshold, it's likely dragging (e.g., arc), not a swipe */
            bool dx_exceeds = LV_ABS(dx) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD;
            bool dy_exceeds = LV_ABS(dy) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD;

            if ((dx_exceeds && !dy_exceeds) || (!dx_exceeds && dy_exceeds)) {
                ui_bridge_gesture_type_t gesture = UI_BRIDGE_GESTURE_NONE;

                /* Determine swipe direction based on dominant axis */
                if (LV_ABS(dx) > LV_ABS(dy)) {
                    /* Horizontal swipe */
                    if (dx < 0) {
                        gesture = UI_BRIDGE_GESTURE_SWIPE_LEFT;
                    } else {
                        gesture = UI_BRIDGE_GESTURE_SWIPE_RIGHT;
                    }
                } else {
                    /* Vertical swipe */
                    if (dy < 0) {
                        gesture = UI_BRIDGE_GESTURE_SWIPE_UP;
                    } else {
                        gesture = UI_BRIDGE_GESTURE_SWIPE_DOWN;
                    }
                }

                if (gesture != UI_BRIDGE_GESTURE_NONE) {
                    if (ui_bridge_check_gesture_start_position(gesture, state->start_x, state->start_y)) {
                        ESP_LOGD(TAG, "swipe detected: %d (start: %ld, %ld)", gesture,
                                 (long)state->start_x, (long)state->start_y);
                        ui_bridge_handle_gesture_navigation(gesture);
                        state->handled = true;
                    } else {
                        ESP_LOGW(TAG, "swipe gesture %d rejected: invalid start position (%ld, %ld)",
                                 gesture, (long)state->start_x, (long)state->start_y);
                    }
                }
            } else if (dx_exceeds && dy_exceeds) {
                ESP_LOGW(TAG, "Both axes exceed threshold (dx=%ld, dy=%ld) - treating as drag, not swipe",
                         (long)dx, (long)dy);
            } else {
                /* It's a press (not a swipe) */
                ui_bridge_gesture_type_t gesture;
                if (press_duration >= UI_BRIDGE_GESTURE_LONG_PRESS_TIME_MS) {
                    gesture = UI_BRIDGE_GESTURE_LONG_PRESS;
                } else {
                    gesture = UI_BRIDGE_GESTURE_SHORT_PRESS;
                }
                ESP_LOGD(TAG, "press detected: %d (duration: %lu ms, dx: %ld, dy: %ld)", gesture, press_duration, LV_ABS(dx), LV_ABS(dy));
            }
        }

        state->active = false;
        state->handled = false;
        break;
    }

    default:
        /* Ignore all other events (LV_EVENT_FLUSH_WAIT_START, LV_EVENT_VSYNC, etc.) */
        break;
    }
}

void ui_bridge_attach_gesture_handler(lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Attaching gesture handler to input device: %p", (void*)indev);
    lv_indev_add_event_cb(indev, ui_bridge_gesture_event_cb, LV_EVENT_ALL, NULL);
}
