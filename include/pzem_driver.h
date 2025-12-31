#pragma once
#include "common_structs.h"
#include "driver/gpio.h"
#include "driver/uart.h"

// Config
#define PZEM_TXD_PIN    (GPIO_NUM_17)
#define PZEM_RXD_PIN    (GPIO_NUM_16)
#define UART_PORT_NUM   UART_NUM_2

void pzem_init(void);
pzem_data_t pzem_read_registers(void);