#include "csi_api.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Wi‑Fi / CSI / Radar / Ping 相关
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_csi_gain_ctrl.h"
#include "app_ifft.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "esp_lv_adapter.h"

static const char *TAG = "csi_api";

typedef struct {
    uint32_t id;
    uint32_t time;
    int8_t fft_gain;
    uint8_t agc_gain;
    int8_t buf[256];
} csi_recv_queue_t;

static QueueHandle_t s_csi_recv_queue = NULL;
static QueueHandle_t csi_display_queue = NULL;

static esp_ping_handle_t s_ping_handle = NULL;
static int64_t s_time_zero = 0;
static bool s_csi_pipeline_started = false;

static csi_data_callback_t s_csi_data_cb = NULL;
static void *s_csi_data_cb_ctx = NULL;

static bool chart_initialized;
static uint8_t chart_count;
static bool is_processing_stopped;

static esp_timer_handle_t update_timer;
esp_timer_handle_t csi_update_timer;
static csi_api_data_t last_data;  // 保存上一次的数据

// 与 esp_radar_csi.cpp 中的 LVGL_CHART_POINTS 保持一致，避免数组越界
#define CHART_POINTS 300 / 3   // 必须等于 LVGL_CHART_POINTS
static float range[CHART_POINTS];
// 使用 float 存放 Y 轴范围，避免负值写入 uint16_t 产生 655xx 这样的溢出
static float y_range[2];

// 简单滑动平均滤波窗口大小（最近 AVG_WINDOW 个点求平均）
#define AVG_WINDOW 10
static float avg_buffer[AVG_WINDOW];
static int avg_index;
static int avg_count;

#define DISPLAY_SAMPLE_STEP 3
#define LVGL_CHART_POINTS   (300 / DISPLAY_SAMPLE_STEP)
#define CONFIG_SEND_FREQUENCY 10

void processData(void);

esp_err_t csi_api_register_callback(csi_data_callback_t cb, void *ctx)
{
    s_csi_data_cb = cb;
    s_csi_data_cb_ctx = ctx;
    return ESP_OK;
}

static void csi_update_timer_cb(void *arg)
{
    if (!chart_initialized || is_processing_stopped) {
        return;
    }

    // 处理队列里的 CSI 数据并更新图表
    processData();

}

void startCsiUpdateTimer()
{
    if (csi_update_timer != NULL) return;

    esp_timer_create_args_t timer_args = {
        .callback = csi_update_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "csi_update_timer",
        .skip_unhandled_events = true
    };

    esp_timer_create(&timer_args, &csi_update_timer);
    esp_timer_start_periodic(csi_update_timer, 50 * 1000); // 50ms = 50*1000us
}

void stopCsiUpdateTimer()
{
    if (csi_update_timer != NULL) {
        esp_timer_stop(csi_update_timer);
        esp_timer_delete(csi_update_timer);
        csi_update_timer = NULL;
    }
}

void supplementLastData()
{
    // 定时器回调中调用，将上一次的数据重新推送到队列
    // 这样所有图表更新都在processData()中统一处理，避免LVGL渲染冲突
    if (chart_initialized && !is_processing_stopped && csi_display_queue) {
        // 将last_data重新入队，让processData统一处理
        if (xQueueSend(csi_display_queue, &last_data, 0) != pdTRUE) {
            // 队列满了就算了，不强制补充
        }
    }
}

bool csi_init()
{
    // 创建 CSI 数据队列（RadarCSI 内部用于 UI 刷新）
    if (!csi_display_queue) {
        csi_display_queue = xQueueCreate(20, sizeof(csi_api_data_t));
        if (csi_display_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create CSI display queue");
            return false;
        }
    }
    memset(&last_data, 0, sizeof(last_data));
    if (!update_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = supplementLastData,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "csi_update_timer",
            .skip_unhandled_events = true
        };
        esp_timer_create(&timer_args, &update_timer);
    }

    chart_initialized = true;
    chart_count = 0;

    // ESP_LOGI(TAG, "Charts initialized with 25ms update timer");
    ESP_LOGI(TAG, "RadarCSI initialized");
    return true;
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    // 只接收来自当前连接 AP（路由器）的 CSI，ctx 里传入的是 AP 的 BSSID
    if (ctx && memcmp(info->mac, ctx, 6)) {
        return;
    }

    static uint8_t agc_gain = 0;
    static int8_t fft_gain = 0;

#if CONFIG_GAIN_CONTROL
    static int s_count = 0;
    static uint8_t agc_gain_baseline = 0;
    static int8_t fft_gain_baseline = 0;

    esp_csi_gain_ctrl_get_rx_gain(info, &agc_gain, &fft_gain);
    if (s_count < 100) {
        esp_csi_gain_ctrl_record_rx_gain(agc_gain, fft_gain);
    } else if (s_count == 100) {
        esp_csi_gain_ctrl_get_rx_gain_baseline(&agc_gain_baseline, &fft_gain_baseline);
        ESP_LOGI(TAG, "agc_gain_baseline: %d, fft_gain_baseline: %d", agc_gain_baseline, fft_gain_baseline);
#if CONFIG_FORCE_GAIN
        esp_csi_gain_ctrl_set_rx_force_gain(agc_gain_baseline, fft_gain_baseline);
        ESP_LOGI(TAG, "fft_force %d, agc_force %d", fft_gain_baseline, agc_gain_baseline);
#endif
    }
    s_count++;
#endif

    if (!s_csi_recv_queue) {
        ESP_LOGW(TAG, "<%s> s_csi_recv_queue is not initialized", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return;
    }

    csi_recv_queue_t *csi_send_queuedata = (csi_recv_queue_t *)calloc(1, sizeof(csi_recv_queue_t));
    if (!csi_send_queuedata) {
        ESP_LOGW(TAG, "Failed to allocate memory for csi_send_queuedata");
        return;
    }

    memcpy(&(csi_send_queuedata->id), info->payload + 15, sizeof(uint32_t));
    csi_send_queuedata->time = info->rx_ctrl.timestamp;
    csi_send_queuedata->agc_gain = agc_gain;
    csi_send_queuedata->fft_gain = fft_gain;

    memset(csi_send_queuedata->buf, 0, sizeof(csi_send_queuedata->buf));
    size_t copy_len = info->len > sizeof(csi_send_queuedata->buf) - 8 ? sizeof(csi_send_queuedata->buf) - 8 : info->len;
    memcpy(csi_send_queuedata->buf + 8, info->buf, copy_len);

    if (xQueueSend(s_csi_recv_queue, &csi_send_queuedata, 0) != pdTRUE) {
        free(csi_send_queuedata);
    }
}

void doUpdateChart(const csi_api_data_t *data, bool reset_timer)
{
    if (data == NULL) {
        return;
    }

    /* 1. 滑动平均滤波 */
    avg_buffer[avg_index] = data->cir;
    avg_index = (avg_index + 1) % AVG_WINDOW;
    if (avg_count < AVG_WINDOW) {
        avg_count++;
    }

    float cir_avg = 0.0f;
    for (int i = 0; i < avg_count; ++i) {
        cir_avg += avg_buffer[i];
    }
    cir_avg /= (avg_count > 0 ? avg_count : 1);

    /* 2. 保存当前点（仅用于计算，不画图） */
    range[chart_count] = cir_avg * 5;

    /* 3. 计算 Y 轴范围 */
    y_range[0] = 500;
    y_range[1] = 0;
    for (int i = 0; i < LVGL_CHART_POINTS; i++) {
        if (y_range[0] > range[i]) y_range[0] = range[i];
        if (y_range[1] < range[i]) y_range[1] = range[i];
    }

    uint8_t y_range_size = 30;
    if ((y_range[1] - y_range[0]) < y_range_size) {
        int delta = (y_range_size - (y_range[1] - y_range[0])) / 2;
        y_range[1] += delta;
        y_range[0] -= delta;
    }

    /* 4. 仅打印“用于画图的数据” */
    // ESP_LOGI(TAG,
    //          "chart_idx=%u, cir_avg=%.3f, y_min=%d, y_max=%d",
    //          chart_count,
    //          cir_avg,
    //          (int)y_range[0],
    //          (int)y_range[1]);
    csi_chart_config_t chart_val = {
        .y_range = {y_range[0], y_range[1]},
        .val = range[chart_count],
    };
    if (s_csi_data_cb) {
        s_csi_data_cb(chart_val, s_csi_data_cb_ctx);
    }

    /* 5. 更新计数器 */
    chart_count++;
    if (chart_count >= LVGL_CHART_POINTS) {
        chart_count = 0;
    }
}

void updateChart(csi_api_data_t *data)
{
    if (!chart_initialized) {
        ESP_LOGW(TAG, "Charts not initialized");
        return;
    }
    
    // 检查是否是补充的数据（与上次数据相同）
    bool is_supplemented = (memcmp(&last_data, data, sizeof(csi_api_data_t)) == 0);
    
    // 如果是新数据，保存并重置定时器
    if (!is_supplemented) {
        memcpy(&last_data, data, sizeof(csi_api_data_t));
        
        // 重置定时器 - 25ms内没有新数据将触发补充
        if (update_timer) {
            esp_timer_stop(update_timer);
            esp_timer_start_once(update_timer, 25000);  // 25ms = 25000us
        }
    } else {
        // 补充数据时也重置定时器，以便持续补充
        if (update_timer) {
            esp_timer_stop(update_timer);
            esp_timer_start_once(update_timer, 25000);
        }
        
        // 周期性打印补充日志（降低频率避免刷屏）
        static int s_supplement_count = 0;
        if ((s_supplement_count++ % 20) == 0) {
            ESP_LOGD(TAG, "Supplemented last data to prevent waveform freeze");
        }
    }
    // 执行实际的图表更新
    doUpdateChart(data, true);
}

void processData(void)
{
    if (!csi_display_queue) {
        ESP_LOGE(TAG, "CSI display queue not initialized");
        return;
    }

    csi_api_data_t data;

    while (xQueueReceive(csi_display_queue, &data, 0) == pdTRUE) {
        if (is_processing_stopped) {
            continue;   // 不要 free
        }

        esp_lv_adapter_lock(-1);
        updateChart(&data);
        esp_lv_adapter_unlock();
    }
}


bool pushData(const csi_api_data_t *data)
{
    if (is_processing_stopped || csi_display_queue == NULL) {
        ESP_LOGW(TAG, "drop data, stopped=%d queue=%p",
                 is_processing_stopped, csi_display_queue);
        return false;
    }

    // 尝试入队，如果满了就丢掉最旧
    if (xQueueSend(csi_display_queue, data, 0) != pdTRUE) {
        csi_api_data_t tmp;
        xQueueReceive(csi_display_queue, &tmp, 0);  // 丢掉最旧
        xQueueSend(csi_display_queue, data, 0);
        return false;
    }

    return true;
}


static void radar_wifi_csi_init()
{
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    if (!s_csi_recv_queue) {
        s_csi_recv_queue = xQueueCreate(20, sizeof(csi_recv_queue_t *));
    }

    // 参考 get-started/csi_recv_copy 的配置，针对 C5/C6
#if CONFIG_IDF_TARGET_ESP32C5
    wifi_csi_config_t csi_config = {
        .enable                   = true,
        .acquire_csi_legacy       = false,
        .acquire_csi_force_lltf   = 0,
        .acquire_csi_ht20         = true,
        .acquire_csi_ht40         = true,
        .acquire_csi_vht          = false,
        .acquire_csi_su           = false,
        .acquire_csi_mu           = false,
        .acquire_csi_dcm          = false,
        .acquire_csi_beamformed   = false,
        .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg            = false,
        .dump_ack_en              = false,
        .reserved                 = false
    };
#elif CONFIG_IDF_TARGET_ESP32C6
    wifi_csi_config_t csi_config = {
        .enable                 = true,
        .acquire_csi_legacy     = false,
        .acquire_csi_ht20       = true,
        .acquire_csi_ht40       = true,
        .acquire_csi_vht        = false,
        .acquire_csi_su         = false,
        .acquire_csi_mu         = false,
        .acquire_csi_dcm        = false,
        .acquire_csi_beamformed = false,
        .acquire_csi_he_stbc    = 2,
        .val_scale_cfg          = false,
        .dump_ack_en            = false,
        .reserved               = false
    };
#else
    // 其它芯片（如 ESP32）保持默认 legacy 配置
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = false,
    };
#endif

    static wifi_ap_record_t s_ap_info = {0};
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&s_ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    // 把 AP 的 BSSID 通过 ctx 传进回调，用来过滤 CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, s_ap_info.bssid));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

esp_err_t wifi_ping_router_stop(void)
{
    if (s_ping_handle != NULL) {
        ESP_LOGW(TAG, "wifi_ping_router_stop");
        esp_ping_stop(s_ping_handle);
        esp_ping_delete_session(s_ping_handle);
        s_ping_handle = NULL;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

static void process_csi_data_task(void *pvParameter)
{
    csi_recv_queue_t *csi_recv_queue_data = NULL;
    Complex_Iq x_iq[64];
    float cir = 0;
    float pha = 0;

    s_time_zero = esp_timer_get_time();

    while (xQueueReceive(s_csi_recv_queue, &csi_recv_queue_data, portMAX_DELAY) == pdTRUE) {
        if (!csi_recv_queue_data) {
            continue;
        }

#if !CONFIG_FORCE_GAIN && CONFIG_GAIN_CONTROL
        float scaling_factor = 0;
        esp_csi_gain_ctrl_get_gain_compensation(&scaling_factor, csi_recv_queue_data->agc_gain, csi_recv_queue_data->fft_gain);
#else
        float scaling_factor = 1.0f;
#endif

        for (int i = 0; i < 64; i++) {
            x_iq[i].real = _IQ16(csi_recv_queue_data->buf[2 * i]);
            x_iq[i].imag = _IQ16(csi_recv_queue_data->buf[2 * i + 1]);
        }
        fft_iq(x_iq, 1);
        cir = complex_magnitude_iq(x_iq[0]) * scaling_factor;
        pha = complex_phase_iq(x_iq[0]);

        // 打包数据
        csi_api_data_t data;

        data.id = csi_recv_queue_data->id;
        data.time_delta = (int64_t)csi_recv_queue_data->time - s_time_zero;
        data.cir = cir;
        data.pha = pha;


        // 回调（只读，不能保存指针）
        // if (s_csi_data_cb) {
        //     s_csi_data_cb(data, s_csi_data_cb_ctx);
        // }

        pushData(&data);

        free(csi_recv_queue_data);

    }
}


esp_err_t wifi_ping_router_start(void)
{
    ESP_LOGW(TAG, "wifi_ping_router_start");
    
    // 如果已经在运行，先停止
    if (s_ping_handle != NULL) {
        ESP_LOGW(TAG, "Ping already running, stopping first");
        wifi_ping_router_stop();
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.count           = 0;                               // 无限次
    ping_config.interval_ms     = 1000 / CONFIG_SEND_FREQUENCY;    // 发送频率
    ping_config.task_stack_size = 3072;
    ping_config.data_size       = 1;

    esp_netif_ip_info_t local_ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);
    ESP_LOGI(TAG, "got ip:" IPSTR ", gw: " IPSTR, IP2STR(&local_ip.ip), IP2STR(&local_ip.gw));
    ping_config.target_addr.u_addr.ip4.addr = ip4_addr_get_u32(&local_ip.gw);
    ping_config.target_addr.type = ESP_IPADDR_TYPE_V4;

    esp_ping_callbacks_t cbs = { 0 };
    esp_ping_new_session(&ping_config, &cbs, &s_ping_handle);
    esp_ping_start(s_ping_handle);

    return ESP_OK;
}

static void csi_wait_wifi_task(void *pv)
{
    while (1) {
        // 获取 STA netif
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
                ip_info.ip.addr != 0) {

                ESP_LOGI(TAG, "WiFi STA connected, got IP: " IPSTR,
                         IP2STR(&ip_info.ip));

                // 初始化 CSI（注册 csicb，开始采集）
                radar_wifi_csi_init();

                s_csi_pipeline_started = true;

                // 创建 CSI 处理任务
                xTaskCreate(
                    process_csi_data_task,
                    "process_csi_data_task",
                    4096,
                    NULL,
                    6,
                    NULL
                );

                ESP_LOGI(TAG, "CSI pipeline started (without auto-ping)");

                // 自杀（只运行一次）
                vTaskDelete(NULL);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void radar_csi_start_pipeline(void)
{
    if (s_csi_pipeline_started) {
        return;
    }

    // 等待主工程 Wi-Fi STA 连上 AP 并获取 IP
    xTaskCreate(
        csi_wait_wifi_task,
        "csi_wait_wifi",
        4096,
        NULL,
        5,
        NULL
    );
}
