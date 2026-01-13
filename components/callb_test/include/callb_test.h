#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

typedef struct data_generator_t data_generator_t;  // 不完整类型

typedef void (*data_callback_t)(float data, void *ctx);

esp_err_t data_generator_create(data_generator_t **gen_out, data_callback_t cb, void *ctx);
esp_err_t data_generator_destroy(data_generator_t *gen);

#ifdef __cplusplus
}
#endif
