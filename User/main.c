#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"          // 互斥量头文件
#include "OLED.h"
#include "dht11.h"
#include "bh1750.h"
#include "esp8266.h"         // 包含外部声明
#include <stdio.h>

/* -------------------- 数据结构定义 -------------------- */
// 温湿度数据（单独类型）
typedef struct {
    uint8_t temp;
    uint8_t humi;
} TempHumi_t;

// 光照数据（单独类型）
typedef struct {
    float lux;
} Lux_t;

/* -------------------- 全局变量（供其他文件使用） -------------------- */
// 队列句柄
QueueHandle_t xTempHumiQueue;
QueueHandle_t xLuxQueue;

// 互斥量句柄（用于保护全局变量）
SemaphoreHandle_t xDataMutex;

// 传感器最新数据（全局变量，供 esp8266.c 等文件读取）
uint8_t g_temperature = 25;
uint8_t g_humidity = 60;
float   g_lux = 100.0;

// WiFi 连接状态（由 esp8266.c 修改）
volatile uint8_t wifi_connected = 0;

/* -------------------- 任务函数声明（如果需要在前面引用） -------------------- */
void DHT11_Task(void *arg);
void BH1750_Task(void *arg);
void OLED_Task(void *arg);
void ESP8266_Task(void *arg);   // 在 esp8266.c 中实现

/* -------------------- 任务函数实现 -------------------- */

// DHT11 任务（每2秒采样一次）
void DHT11_Task(void *arg)
{
    uint8_t t, h;
    TempHumi_t th_data;
    BaseType_t ret;

    while (1) {
        // ① 读取DHT11（时序敏感，关中断保护）
        taskENTER_CRITICAL();
        ret = DHT11_Read(&t, &h);
        taskEXIT_CRITICAL();

        if (ret == pdTRUE) {
            th_data.temp = t;
            th_data.humi = h;

            // ② 更新全局变量（加互斥量保护）
            xSemaphoreTake(xDataMutex, portMAX_DELAY);
            g_temperature = t;
            g_humidity = h;
            xSemaphoreGive(xDataMutex);

            // ③ 发送温湿度到队列（队列满时覆盖旧数据）
            if (xQueueSend(xTempHumiQueue, &th_data, 0) != pdPASS) {
                xQueueOverwrite(xTempHumiQueue, &th_data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// BH1750 任务（每1秒采样一次）
void BH1750_Task(void *arg)
{
    float lux;
    Lux_t lux_data;

    BH1750_Init();

    while (1) {
        lux = BH1750_ReadLightIntensity();
        if (lux < 0.1) lux = 100.0;   // 默认值

        lux_data.lux = lux;

        // ① 更新全局变量（加互斥量）
        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        g_lux = lux;
        xSemaphoreGive(xDataMutex);

        // ② 发送光照到队列
        if (xQueueSend(xLuxQueue, &lux_data, 0) != pdPASS) {
            xQueueOverwrite(xLuxQueue, &lux_data);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// OLED 显示任务（从两个队列分别接收，合并显示）
void OLED_Task(void *arg)
{
    TempHumi_t th = {25, 60};
    Lux_t lux_data = {100.0};
    char str[16];

    OLED_Init();
    OLED_ShowString(1, 1, "Temp:   C");
    OLED_ShowString(2, 1, "Humi:   %");
    OLED_ShowString(3, 1, "Lux:       lx");
    OLED_ShowString(4, 1, "WiFi:   ");

    while (1) {
        // 从温湿度队列接收（超时100ms，避免阻塞显示）
        if (xQueueReceive(xTempHumiQueue, &th, pdMS_TO_TICKS(100)) == pdPASS) {
            sprintf(str, "%2d", th.temp);
            OLED_ShowString(1, 6, str);
            sprintf(str, "%2d", th.humi);
            OLED_ShowString(2, 6, str);
        }

        // 从光照队列接收（超时0，立即返回）
        if (xQueueReceive(xLuxQueue, &lux_data, 0) == pdPASS) {
            sprintf(str, "%-6.1f", lux_data.lux);
            OLED_ShowString(3, 6, str);
        }

        // WiFi状态显示
        if (wifi_connected) {
            OLED_ShowString(4, 6, "OK ");
        } else {
            OLED_ShowString(4, 6, "ERR");
        }

        vTaskDelay(pdMS_TO_TICKS(200));   // 200ms刷新一次显示
    }
}

// 初始化任务（创建所有对象和任务）
void Init_BSP(void *param)
{
    // 创建互斥量
    xDataMutex = xSemaphoreCreateMutex();
    if (xDataMutex == NULL) {
        // 处理错误（此处可添加死循环或错误灯）
        while (1);
    }

    // 创建两个队列（深度4，足够缓冲）
    xTempHumiQueue = xQueueCreate(4, sizeof(TempHumi_t));
    xLuxQueue      = xQueueCreate(4, sizeof(Lux_t));

    if (xTempHumiQueue == NULL || xLuxQueue == NULL) {
        while (1);  // 队列创建失败，死循环
    }

    // 创建任务（优先级：DHT11最高，BH1750次之，显示和WiFi最低）
    xTaskCreate(DHT11_Task,    "DHT11",   256, NULL, 3, NULL);
    xTaskCreate(BH1750_Task,   "BH1750",  256, NULL, 2, NULL);
    xTaskCreate(OLED_Task,     "OLED",    512, NULL, 1, NULL);
    xTaskCreate(ESP8266_Task,  "ESP8266", 512, NULL, 1, NULL);

    vTaskDelete(NULL);
}

/* -------------------- 主函数 -------------------- */
int main(void)
{
    // 硬件初始化（假设已在各驱动中完成）
    xTaskCreate(Init_BSP, "Init", 256, NULL, 3, NULL);
    vTaskStartScheduler();

    while (1);   // 不会执行到这里
}