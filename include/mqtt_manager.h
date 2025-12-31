#pragma once
#include "common_structs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void mqtt_manager_init(void);
void mqtt_send_pzem_data(pzem_data_t data, bool relay_state);
void mqtt_publisher_task(void *arg);