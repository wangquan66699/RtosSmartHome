#include "esp8266.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdarg.h>
#include <string.h>

/* 引用外部变量 */
extern QueueHandle_t xSensorQueue;
extern volatile uint8_t wifi_connected;
extern volatile uint8_t g_temperature;
extern volatile uint8_t g_humidity;
extern volatile float g_lux;

/* 传感器数据结构 */
typedef struct {
    uint8_t temp;
    uint8_t humi;
    float lux;
} SensorData_t;

/* -------------------- UART2 初始化 -------------------- */
static void UART2_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_3;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    USART_InitStruct.USART_BaudRate            = ESP8266_BAUDRATE;
    USART_InitStruct.USART_WordLength          = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits            = USART_StopBits_1;
    USART_InitStruct.USART_Parity              = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStruct);

    USART_Cmd(USART2, ENABLE);
}

/* -------------------- 串口收发 -------------------- */
static void UART2_SendByte(uint8_t ch) {
    USART_SendData(USART2, ch);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
}

static void UART2_SendString(const char *str) {
    while (*str) {
        UART2_SendByte((uint8_t)*str++);
    }
}

static uint8_t UART2_ReceiveByte(uint32_t timeout_ms) {
    timeout_ms = 200;
    TickType_t start = xTaskGetTickCount();
    while (!USART_GetFlagStatus(USART2, USART_FLAG_RXNE)) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            return 0;
        }
        taskYIELD();
    }
    return (uint8_t)USART_ReceiveData(USART2);
}

static void UART2_ClearBuffer(void) {
    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE)) {
        USART_ReceiveData(USART2);
    }
}

/* -------------------- AT 指令 -------------------- */
static BaseType_t ESP8266_SendCommand(const char *cmd, const char *expected, uint32_t timeout_ms) {
    char buf[64];
    uint16_t idx = 0;
    TickType_t start;

    UART2_SendString(cmd);
    UART2_SendString("\r\n");
    UART2_ClearBuffer();

    start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        uint8_t ch = UART2_ReceiveByte(100);
        if (ch) {
            if (idx < sizeof(buf) - 1) {
                buf[idx++] = ch;
                buf[idx] = '\0';
            }
            if (strstr(buf, expected) != NULL) {
                return pdTRUE;
            }
        }
        taskYIELD();
    }
    return pdFALSE;
}

static BaseType_t ESP8266_SendCommandWithRetry(const char *cmd, const char *expected,
                                                uint32_t timeout_ms, uint8_t retry_count) {
    if (retry_count > 2) retry_count = 2;
    for (uint8_t i = 0; i < retry_count; i++) {
        if (ESP8266_SendCommand(cmd, expected, timeout_ms)) {
            return pdTRUE;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return pdFALSE;
}

/* -------------------- ESP8266 初始化 -------------------- */
void ESP8266_Init(void) {
    UART2_Init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 关闭回显
    ESP8266_SendCommandWithRetry("ATE0", "OK", 1000, 2);

    if (!ESP8266_SendCommandWithRetry("AT", "OK", 1000, 2)) {
        wifi_connected = 0;
        return;
    }
    ESP8266_SendCommandWithRetry("AT+CWMODE=1", "OK", 1000, 2);
    ESP8266_SendCommandWithRetry("AT+CIPMUX=0", "OK", 1000, 2);

    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ESP8266_WIFI_SSID, ESP8266_WIFI_PWD);
    if (!ESP8266_SendCommandWithRetry(cmd, "OK", 5000, 2)) {
        wifi_connected = 0;
        return;
    }

    // TCP 连接部分（确保手机 APP 已开启 TCP Server）
    ESP8266_SendCommandWithRetry("AT+CIPMODE=1", "OK", 1000, 2);
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d", ESP8266_TCP_IP, ESP8266_TCP_PORT);
    if (!ESP8266_SendCommandWithRetry(cmd, "OK", 3000, 2)) {
        wifi_connected = 0;   // TCP 失败则 ERR
        return;
    }
    if (!ESP8266_SendCommandWithRetry("AT+CIPSEND", ">", 2000, 2)) {
        wifi_connected = 0;
        return;
    }

    wifi_connected = 1;   // 全部成功
}

/* -------------------- 发送数据 -------------------- */
void ESP8266_SendData(const char *data) {
    if (wifi_connected) {
        UART2_SendString(data);
    }
}

/* -------------------- ESP8266 任务（发送温湿度到手机） -------------------- */
void ESP8266_Task(void *arg) {
    char buffer[64];

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP8266_Init();

    while (1) {
        if (wifi_connected) {
            sprintf(buffer, "Temp:%dC Humi:%d%% Lux:%.1flx\r\n",
                    g_temperature, g_humidity, g_lux);
            ESP8266_SendData(buffer);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}