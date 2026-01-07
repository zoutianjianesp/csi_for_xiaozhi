#ifndef UI_BRIDGE_PRIV_H
#define UI_BRIDGE_PRIV_H

#include "ui_bridge.h"

#ifdef __cplusplus
/* Forward declaration */
namespace emote {
    class EmoteDisplay;
}
extern "C" {
#endif

/**
 * @file ui_bridge_priv.h
 * @brief Internal/private functions and constants for ui_bridge module
 *
 * This header contains internal function declarations and constants that are used
 * between different implementation files of the ui_bridge module.
 * These should NOT be used by external code.
 */

/* Gesture detection constants (internal) */
#define UI_BRIDGE_GESTURE_LONG_PRESS_TIME_MS     500      /* Long press duration in milliseconds */
#define UI_BRIDGE_GESTURE_SWIPE_THRESHOLD        30       /* Minimum distance for swipe detection */

/* Gesture start position validation constants (internal) */
#define UI_BRIDGE_EDGE_THRESHOLD                 30       /* Distance from edge to be considered edge region */
#define UI_BRIDGE_CENTER_RANGE                   50       /* Range around center (±50 pixels) */

/**
 * @brief Gesture event types (internal)
 */
typedef enum {
    UI_BRIDGE_GESTURE_NONE = 0,
    UI_BRIDGE_GESTURE_SWIPE_LEFT,
    UI_BRIDGE_GESTURE_SWIPE_RIGHT,
    UI_BRIDGE_GESTURE_SWIPE_UP,
    UI_BRIDGE_GESTURE_SWIPE_DOWN,
    UI_BRIDGE_GESTURE_SHORT_PRESS,
    UI_BRIDGE_GESTURE_LONG_PRESS,
} ui_bridge_gesture_type_t;

/**
 * @brief Handle gesture-based page navigation (internal)
 *
 * This function is called by the gesture detection module when a valid
 * navigation gesture is detected.
 *
 * @param gesture_type The detected gesture type
 */
void ui_bridge_handle_gesture_navigation(ui_bridge_gesture_type_t gesture_type);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/**
 * @brief Set cached emote display pointer (internal, C++ only)
 *
 * This function is called by ui_bridge_init to cache the emote display
 * pointer for later use.
 *
 * @param display Pointer to the EmoteDisplay instance
 */
void ui_bridge_set_emote_display(emote::EmoteDisplay *display);
#endif

#endif // UI_BRIDGE_PRIV_H
