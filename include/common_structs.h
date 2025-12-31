#pragma once
#include <stdbool.h>

typedef struct {
    float voltage;
    float current;
    float power;
    float energy;
    float frequency;
    float pf;
    bool valid;
} pzem_data_t;