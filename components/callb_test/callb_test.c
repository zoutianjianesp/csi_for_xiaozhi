#include "callb_test.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "callb_test"

struct data_generator_t {
    data_callback_t callback;
    void *callback_ctx;
    bool running;
    TaskHandle_t task_handle;
};

static void data_generator_task(void *arg)
{
    data_generator_t *gen = (data_generator_t *)arg;
    while (gen->running) {
        float new_data = (float)rand() / RAND_MAX;  // 模拟生成数据

        // 调用用户回调
        if (gen->callback) {
            gen->callback(new_data, gen->callback_ctx);
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 每 0.1 秒
    }
    vTaskDelete(NULL);
}

esp_err_t data_generator_create(data_generator_t **gen_out, data_callback_t cb, void *ctx)
{
    if (!gen_out) return ESP_ERR_INVALID_ARG;

    data_generator_t *gen = (data_generator_t *)calloc(1, sizeof(data_generator_t));
    if (!gen) return ESP_ERR_NO_MEM;

    gen->callback = cb;
    gen->callback_ctx = ctx;
    gen->running = true;

    // 创建 FreeRTOS 任务
    xTaskCreate(data_generator_task, "data_gen_task", 2048, gen, 5, &gen->task_handle);

    *gen_out = gen;
    return ESP_OK;
}

esp_err_t data_generator_destroy(data_generator_t *gen)
{
    if (!gen) return ESP_ERR_INVALID_ARG;

    gen->running = false;

    // 等待任务自行删除
    vTaskDelay(pdMS_TO_TICKS(200));

    free(gen);
    return ESP_OK;
}

