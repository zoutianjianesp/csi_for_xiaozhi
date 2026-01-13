#include "ui_bridge.h"
#include "ui_bridge_priv.h"
#include "display/emote_display.h"
#include <esp_log.h>
#include <lvgl.h>
#include <esp_lv_adapter.h>
#include <string.h>
#include <stdlib.h>

#define TAG "ui_bridge"

typedef struct ui_bridge_page_node {
    const char *page_id;
    lv_obj_t **container;
    bool in_cycle;  /* Whether this page should be included in cycle navigation */
    struct ui_bridge_page_node *next;
} ui_bridge_page_node_t;

static ui_bridge_page_node_t *s_page_list = NULL;  /* Linked list head */
static const char *s_current_page = NULL;
static ui_bridge_page_switch_cb_t s_page_switch_cb = NULL;
static void *s_page_switch_user_data = NULL;

/* Cached display pointer for emote refresh */
static emote::EmoteDisplay *s_cached_emote_display = nullptr;

static void ui_bridge_refresh_emote_display(void)
{
    if (s_cached_emote_display == nullptr) {
        ESP_LOGW(TAG, "Refresh all: emote_display is nullptr (not initialized)");
        return;
    }

    s_cached_emote_display->RefreshAll();
}

void ui_bridge_handle_gesture_navigation(ui_bridge_gesture_type_t gesture_type)
{
    int direction = 0;
    const char *gesture_name = NULL;

    switch (gesture_type) {
    case UI_BRIDGE_GESTURE_SWIPE_LEFT:
    case UI_BRIDGE_GESTURE_SWIPE_DOWN:
        direction = -1;  /* Previous page */
        gesture_name = (gesture_type == UI_BRIDGE_GESTURE_SWIPE_LEFT) ? "LEFT" : "DOWN";
        break;
    case UI_BRIDGE_GESTURE_SWIPE_RIGHT:
    case UI_BRIDGE_GESTURE_SWIPE_UP:
        direction = 1;   /* Next page */
        gesture_name = (gesture_type == UI_BRIDGE_GESTURE_SWIPE_RIGHT) ? "RIGHT" : "UP";
        break;
    default:
        return;
    }

    /* Check if page containers are registered */
    if (s_page_list == NULL) {
        ESP_LOGW(TAG, "No page containers registered");
        return;
    }

    /* Count total pages (only those in cycle) and find current page index */
    unsigned int total_count = 0;
    int current_index = -1;
    ui_bridge_page_node_t *node = s_page_list;
    while (node != NULL) {
        if (node->in_cycle) {
            if (node->page_id == s_current_page) {
                current_index = (int)total_count;
            }
            total_count++;
        }
        node = node->next;
    }

    /* If current page is not in registered list or no cycle pages, ignore gesture */
    if (current_index < 0 || total_count == 0) {
        return;
    }

    /* Calculate next page index with round-robin */
    int next_index = (current_index + direction + (int)total_count) % (int)total_count;

    const char *target_page = NULL;
    const char *current_name = "UNKNOWN";
    const char *next_name = "UNKNOWN";
    node = s_page_list;
    int index = 0;
    while (node != NULL) {
        if (node->in_cycle) {
            if (index == current_index) {
                current_name = node->page_id ? node->page_id : "UNKNOWN";
            }
            if (index == next_index) {
                target_page = node->page_id;
                next_name = node->page_id ? node->page_id : "UNKNOWN";
            }
            index++;
        }
        node = node->next;
    }

    ESP_LOGI(TAG, "SWIPE_%s: %s (%d) -> %s (%d)",
             gesture_name, current_name, current_index, next_name, next_index);

    /* Call custom callback if set */
    if (s_page_switch_cb && s_page_switch_cb(target_page, s_page_switch_user_data)) {
        return;
    }

    /* Default page switching */
    ui_bridge_switch_page(target_page);
}

bool ui_bridge_register_page(const char *page_id, lv_obj_t **container, bool in_cycle)
{
    if (page_id == NULL) {
        ESP_LOGE(TAG, "Page ID cannot be NULL");
        return false;
    }

    /* Check if already registered */
    ui_bridge_page_node_t *node = s_page_list;
    while (node != NULL) {
        if (node->page_id != NULL && strcmp(node->page_id, page_id) == 0) {
            ESP_LOGW(TAG, "Page container '%s' already registered, updating", page_id);
            node->container = container;
            node->in_cycle = in_cycle;
            return true;
        }
        node = node->next;
    }

    /* Create new node */
    ui_bridge_page_node_t *new_node = (ui_bridge_page_node_t *)malloc(sizeof(ui_bridge_page_node_t));
    if (new_node == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for page container node");
        return false;
    }

    new_node->page_id = page_id;
    new_node->container = container;
    new_node->in_cycle = in_cycle;
    new_node->next = s_page_list;  /* Insert at head */
    s_page_list = new_node;

    /* Count nodes for logging */
    unsigned int count = 0;
    node = s_page_list;
    while (node != NULL) {
        count++;
        node = node->next;
    }
    ESP_LOGD(TAG, "Registered page: %s (in_cycle: %s, total: %u)",
             page_id ? page_id : "UNKNOWN", in_cycle ? "true" : "false", count);
    return true;
}

void ui_bridge_switch_page(const char *page_id)
{
    if (page_id == NULL) {
        ESP_LOGW(TAG, "Cannot switch to NULL page");
        return;
    }

    /* Update current page state */
    s_current_page = page_id;

    esp_lv_adapter_lock(-1);

    /* Set dummy draw mode for home page */
    lv_display_t *disp = lv_display_get_default();
    bool enable_dummy = (strcmp(page_id, UI_BRIDGE_PAGE_HOME) == 0);
    if (disp != nullptr) {
        esp_lv_adapter_set_dummy_draw(disp, enable_dummy);
    }

    /* Control visibility of all registered containers */
    ui_bridge_page_node_t *node = s_page_list;
    while (node != NULL) {
        lv_obj_t *container = *node->container;
        if (container != NULL) {
            if (node->page_id != NULL && strcmp(node->page_id, page_id) == 0) {
                // Load screen with fade-in animation
                ESP_LOGI(TAG, "Loading screen: %s", node->page_id);
                lv_screen_load_anim(container, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, false);
            } else {
                // lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
            }
        }
        node = node->next;
    }

    esp_lv_adapter_unlock();

    /* Refresh emote display if switching to home page */
    if (enable_dummy) {
        ui_bridge_refresh_emote_display();
    }
}

const char *ui_bridge_get_current_page(void)
{
    return s_current_page;
}

void ui_bridge_set_page_switch_callback(ui_bridge_page_switch_cb_t cb, void *user_data)
{
    s_page_switch_cb = cb;
    s_page_switch_user_data = user_data;
}

/* Internal function to set cached emote display (called from ui_bridge_init) */
void ui_bridge_set_emote_display(emote::EmoteDisplay *display)
{
    s_cached_emote_display = display;
}
