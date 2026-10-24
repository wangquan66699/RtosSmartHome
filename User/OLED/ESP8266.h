#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

/* 配置 WiFi 和 TCP 服务器参数（请根据实际情况修改） */
#define ESP8266_WIFI_SSID     "WifiTest"
#define ESP8266_WIFI_PWD      "88888888"
#define ESP8266_TCP_IP        "192.168.1.156"   // 手机 TCP 服务器 IP
#define ESP8266_TCP_PORT      8080

/* 串口波特率 */
#define ESP8266_BAUDRATE      115200

/* 函数声明 */
void ESP8266_Init(void);
void ESP8266_SendData(const char *data);
void ESP8266_Task(void *arg);

#endif