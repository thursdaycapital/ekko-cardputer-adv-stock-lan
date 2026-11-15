#pragma once
#include <cstdint>
using BaseType_t=int;
using UBaseType_t=unsigned;
using TickType_t=uint32_t;
constexpr BaseType_t pdPASS=1,pdFAIL=0,pdTRUE=1,pdFALSE=0;
constexpr TickType_t portMAX_DELAY=UINT32_MAX;
constexpr TickType_t pdMS_TO_TICKS(uint32_t value){return value;}
