#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t start[2];      
    uint32_t id;           
    int64_t time_delta;    
    float cir;             // 振幅（单通道）
    float pha;             // 相位（单通道）
    uint8_t end[2];        
} __attribute__((packed)) csi_api_data_t;

typedef struct {
    float y_range[2];      // 图表显示范围（动态调整）
    float val;
} csi_chart_config_t;

// 回调函数类型
typedef void (*csi_data_callback_t)(csi_chart_config_t chart_val, void *ctx);

// 注册回调
esp_err_t csi_api_register_callback(csi_data_callback_t cb, void *ctx);

bool csi_init();

esp_err_t wifi_ping_router_start(void);

esp_err_t wifi_ping_router_stop(void);

void radar_csi_start_pipeline(void);

void startCsiUpdateTimer();

void stopCsiUpdateTimer();

void csi_api_start(csi_data_callback_t cb, void *ctx);
#ifdef __cplusplus
}
#endif
