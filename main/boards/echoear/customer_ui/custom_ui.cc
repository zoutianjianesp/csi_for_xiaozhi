#include <string.h>  /* For strcmp */
#include <esp_log.h>
#include <esp_lv_adapter.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "custom_ui.h"
#include "alarm_api.h"
#include "csi_ui/ui/ui.h"
//#include "../esp_radar_csi.h"
#include "csi_api.h"
#define TAG "alarm_controller"

// ============================================================================
// Type Definitions
// ============================================================================
//using esp_brookesia::apps::RadarCSI;
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
static lv_chart_series_t *ser = NULL;
// Queue and task for deferred chart updates
static QueueHandle_t chart_queue = NULL;
static TaskHandle_t chart_task_handle = NULL;
#define CHART_QUEUE_LEN 16
#define CHART_TASK_STACK_SIZE 4096

static void chart_task(void *arg)
{
    csi_chart_config_t recv;
    for (;;) {
        if (chart_queue && xQueueReceive(chart_queue, &recv, portMAX_DELAY) == pdTRUE) {
            const char *current_page = ui_bridge_get_current_page();
            if (current_page && strcmp(current_page, "SCREEN_W") == 0) {
                esp_lv_adapter_lock(-1);
                if (ui_ScreenW_Chart && ser) {
                    lv_chart_set_next_value(ui_ScreenW_Chart, ser, (uint16_t)(recv.val));
                    lv_chart_set_range(ui_ScreenW_Chart, LV_CHART_AXIS_PRIMARY_Y, recv.y_range[0], recv.y_range[1]);
                }
                if (ui_ScreenW_Chart_Yaxis1) {
                    lv_scale_set_range(ui_ScreenW_Chart_Yaxis1, recv.y_range[0], recv.y_range[1]);
                }
                esp_lv_adapter_unlock();
            }
        }
    }
    vTaskDelete(NULL);
}
// ============================================================================
// Global Variables
// ============================================================================
static void my_csi_cb(csi_chart_config_t chart_val, void *ctx)
{
    ESP_LOGI("MAIN", "cir_avg=%.2f, y_range=[%.2f, %.2f]", chart_val.val, chart_val.y_range[0], chart_val.y_range[1]);
    /* Enqueue the chart data for the chart task to process to avoid blocking here. */
    if (chart_queue) {
        if (xQueueSend(chart_queue, &chart_val, 0) != pdTRUE) {
            ESP_LOGW(TAG, "chart_queue full, dropping chart sample");
        }
        return;
    }

    /* Fallback: if queue not created, do the update inline (legacy behavior) */
    const char *current_page = ui_bridge_get_current_page();
    if (current_page && strcmp(current_page, "SCREEN_W") == 0) {
        esp_lv_adapter_lock(-1);
        if (ui_ScreenW_Chart && ser) {
            lv_chart_set_next_value(ui_ScreenW_Chart, ser, (uint16_t)(chart_val.val));
        }
        if (ui_ScreenW_Chart_Yaxis1) {
            lv_scale_set_range(ui_ScreenW_Chart_Yaxis1, chart_val.y_range[0], chart_val.y_range[1]);
        }
        esp_lv_adapter_unlock();
    }
}

static bool custom_ui_page_switch_callback(const char *target_page, void *user_data)
{
    const char *current_page = ui_bridge_get_current_page();

    /* Special handling for pomodoro page */
    if (target_page != NULL && strcmp(target_page, PAGE_POMODORO) == 0 &&
            (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0)) {
        alarm_start_pomodoro(5);
        return true;  /* Handled, skip default switch */
    }
    if (target_page != NULL && strcmp(target_page, "SCREEN_W") == 0 &&
            (current_page == NULL || strcmp(current_page, "SCREEN_W") != 0)) {
        ESP_LOGI(TAG, "Start Pinging!");
        csi_init();
        //RadarCSI::getInstance()->initCharts();
        // 恢复数据处理
        //RadarCSI::getInstance()->resumeDataProcessing();
        //RadarCSI::getInstance()->startPing();
        esp_lv_adapter_lock(-1);
        if (ui_ScreenW_Chart && !ser) {
            lv_chart_set_range(ui_ScreenW_Chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
            lv_chart_set_point_count(ui_ScreenW_Chart, 100);
            ser = lv_chart_add_series(ui_ScreenW_Chart, lv_color_hex(0x388E3C), LV_CHART_AXIS_PRIMARY_Y); // 深绿
            lv_chart_refresh(ui_ScreenW_Chart);
            // 禁止滚动
            lv_obj_clear_flag(ui_ScreenW_Chart, LV_OBJ_FLAG_SCROLLABLE);
        }
        esp_lv_adapter_unlock();
        wifi_ping_router_start();
        //RadarCSI::getInstance()->startPipeline();
        radar_csi_start_pipeline();
        startCsiUpdateTimer();
        csi_api_register_callback(my_csi_cb, NULL);
    }
    if (current_page != NULL && strcmp(current_page, "SCREEN_W") == 0 &&
        (target_page == NULL || strcmp(target_page, "SCREEN_W") != 0)) {
        ESP_LOGI(TAG, "Stop Pinging!");
        //RadarCSI::getInstance()->stopPing();
        wifi_ping_router_stop();
        stopCsiUpdateTimer();
    }

    return false;  /* Use default switch */
}

void custom_ui_create(void)
{
    /* Create and register pomodoro container */
    container_pomodoro = alarm_pomodoro_create_with_parent(NULL);
    ui_bridge_register_page(PAGE_POMODORO, &container_pomodoro, true);

    /* Create and register sleep container */
    container_sleep = alarm_sleep_24h_create_with_parent(NULL);
    ui_bridge_register_page(PAGE_SLEEP, &container_sleep, true);

    /* Create and register muuyu container */
    container_muyu = alarm_muyu_create_with_parent(NULL);
    ui_bridge_register_page(PAGE_MUYU, &container_muyu, true);

    /* Create and register time up container */
    container_time_up = alarm_time_up_create_with_parent(NULL);
    ui_bridge_register_page(PAGE_TIME_UP, &container_time_up, false);
    /* Create and register screenW container */
    container_screenW = ui_ScreenW_screen_init(NULL);
    ui_bridge_register_page("SCREEN_W", &container_screenW, true);

    /* Create and register csi_light container */
    container_csi_behav = ui_csi_behav_Screen_screen_init(NULL);
    ui_bridge_register_page("CSI_BEHAV", &container_csi_behav, true);

    /* Register page switch callback for custom handling (e.g., pomodoro) */
    /* Create chart queue and task used to process CSI chart updates off the callback
       to avoid blocking the producer (callback) context. */
    if (!chart_queue) {
        chart_queue = xQueueCreate(CHART_QUEUE_LEN, sizeof(csi_chart_config_t));
        if (chart_queue) {
            BaseType_t r = xTaskCreate(chart_task, "chart_task", CHART_TASK_STACK_SIZE, NULL, 5, &chart_task_handle);
            if (r != pdPASS) {
                ESP_LOGW(TAG, "Failed to create chart_task");
                vQueueDelete(chart_queue);
                chart_queue = NULL;
            }
        } else {
            ESP_LOGW(TAG, "Failed to create chart_queue");
        }
    }

    ui_bridge_set_page_switch_callback(custom_ui_page_switch_callback, NULL);
}
