#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h"

// ===== 用户配置：修改为你的实际引脚 =====
#define DHT11_GPIO_PORT      GPIOA
#define DHT11_GPIO_PIN       GPIO_Pin_6      // 改为 PA6
#define DHT11_RCC_APB        RCC_APB2Periph_GPIOA
// ========================================

void DHT11_Init(void);
uint8_t DHT11_Read(uint8_t *temp, uint8_t *humi);

#endif