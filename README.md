# STM32F103C8T6 + FreeRTOS 智能家居系统

[![Platform](https://img.shields.io/badge/platform-STM32F103C8T6-blue)](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-brightgreen)](https://www.freertos.org/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

基于 STM32F103C8T6（Cortex-M3）和 FreeRTOS 的智能家居环境监测系统。采集温湿度、光照强度等环境数据，通过 OLED 屏幕本地显示，同时通过 ESP8266 WiFi 模块将数据实时透传至手机端。

> STM32F103C8T6 + FreeRTOS Smart Home System  collects temperature, humidity, and ambient light data, displays on OLED, and streams to a mobile phone via ESP8266 WiFi (TCP).

---

## 功能特性 / Features

- **多任务实时调度** — 基于 FreeRTOS，各传感器采集、显示、WiFi 通信运行在独立任务中
- **温湿度采集** — DHT11 数字传感器，每 2 秒采集一次
- **光照强度采集** — BH1750 高精度光照传感器，I2C 通信，每 1 秒采集一次
- **OLED 实时显示** — 0.96 寸 OLED（I2C），200ms 刷新率，显示温度/湿度/光照/WiFi 状态
- **WiFi 数据透传** — ESP8266（ESP-01S）通过 TCP 将传感器数据实时发送至手机
- **互斥锁与消息队列** — 使用互斥量保护共享数据，队列在任务间传递传感器数据

---

## 硬件连接 / Hardware Wiring

| 外设 | 引脚 | 说明 |
|------|------|------|
| **DHT11** | | |
| DATA | PA6 | 数据线 |
| **ESP-01S (ESP8266)** | | |
| TX | PA3 | 接 STM32 RX |
| RX | PA2 | 接 STM32 TX |
| EN | 3.3V | 使能 |
| RST | 3.3V | 复位 |
| **BH1750** | | |
| SCL | PB10 | I2C 时钟 |
| SDA | PB11 | I2C 数据 |
| **OLED (0.96 I2C)** | | |
| SCL | PB8 | I2C 时钟 |
| SDA | PB9 | I2C 数据 |

所有 VCC 接 3.3V，GND 共地。

---

## 软件架构 / Software Architecture

`
+---------------------------------------------+
|                   main.c                     |
|            vTaskStartScheduler()             |
+------------------+--------------------------+
                   |
      +------------+------------+--------------+
      v            v            v              v
+----------+ +----------+ +----------+ +----------+
| DHT11    | | BH1750   | | OLED     | | ESP8266  |
| Task     | | Task     | | Task     | | Task     |
| (Pri 3)  | | (Pri 2)  | | (Pri 1)  | | (Pri 1)  |
+----+-----+ +----+-----+ +----+-----+ +----+-----+
     |            |            |            |
     +----+-------+            |            |
         v                     v            |
   +----------+          +----------+       |
   | Mutex    |          | OLED     |       |
   | (共享数据)|          | Display  |       |
   +----------+          +----------+       |
         |                                  |
         +----------------------------------+
                    v
           +--------------+
           |  ESP8266 TCP |
           |  手机 App     |
           +--------------+
`

### 任务说明 / Task Overview

| 任务 | 栈大小 | 优先级 | 周期 | 功能 |
|------|--------|--------|------|------|
| Init_BSP | 256B | 3 | 一次性 | 创建互斥量、队列，启动子任务后自删 |
| DHT11_Task | 256B | 3 | 2s | 读取温湿度，更新全局变量并发送队列 |
| BH1750_Task | 256B | 2 | 1s | 读取光照强度，更新全局变量并发送队列 |
| OLED_Task | 512B | 1 | 200ms | 从队列接收数据，刷新 OLED 显示 |
| ESP8266_Task | 512B | 1 | 3s | WiFi 连接管理，TCP 数据发送 |

### IPC 机制 / Inter-Task Communication

- **互斥量 (Mutex)**: 保护 g_temperature, g_humidity, g_lux 全局变量
- **消息队列 (Queue)**: xTempHumiQueue(温湿度), xLuxQueue(光照) 容量4，支持覆盖写入

---

## 开发环境 / Development

| 项目 | 说明 |
|------|------|
| **MCU** | STM32F103C8T6 (Cortex-M3, 72MHz, 64KB Flash, 20KB SRAM) |
| **RTOS** | FreeRTOS V202212.01 |
| **标准库** | STM32F10x Standard Peripheral Library V3.6.0 |
| **IDE** | Keil MDK-ARM (uVision) |
| **编译器** | Arm Compiler 5 / 6 |
| **调试器** | ST-Link / J-Link |

---

## 目录结构 / Project Structure

`
RtosSmartHome/
├── CMSIS/                        # ARM Cortex-M3 CMSIS
│   ├── CoreSupport/              # core_cm3.c/h
│   └── startup_stm32f10x_md.s    # 启动文件
├── FreeRTOS/                     # FreeRTOS 内核源码
│   ├── include/                  # 头文件
│   ├── portable/                 # 移植层 (MemMang, RVDS)
│   └── *.c                       # 内核源文件
├── STM32F10x_StdPeriph_Driver/   # ST 标准外设库 V3.6.0
│   ├── inc/                      # 头文件
│   └── src/                      # 源文件
├── User/                         # 用户应用层
│   ├── main.c                    # 主程序 (FreeRTOS 任务创建)
│   ├── FreeRTOSConfig.h          # FreeRTOS 配置
│   ├── stm32f10x_conf.h          # 标准库配置文件
│   ├── stm32f10x_it.c/h          # 中断服务函数
│   ├── BH1750/                   # BH1750 光照传感器驱动
│   ├── DHT11/                    # DHT11 温湿度传感器驱动
│   ├── OLED/                     # OLED 显示 + ESP8266 驱动
│   └── BSP/                      # 板级支持包 (UART)
├── MDK-ARM/                      # Keil 工程文件
│   ├── Project.uvprojx           # uVision 工程
│   └── Project.uvoptx            # 工程选项
├── .gitignore
├── LICENSE
└── README.md
`

---

## 快速开始 / Quick Start

### 1. 克隆仓库

git clone https://github.com/YOUR_USERNAME/RtosSmartHome.git

### 2. 配置 WiFi 参数

编辑 User/OLED/ESP8266.h，修改以下宏定义为你的 WiFi 和手机 TCP 服务器信息：

#define ESP8266_WIFI_SSID     "YourWiFiSSID"
#define ESP8266_WIFI_PWD      "YourPassword"
#define ESP8266_TCP_IP        "192.168.x.x"   // 手机 IP
#define ESP8266_TCP_PORT      8080

### 3. 编译与下载

- 用 Keil MDK-ARM 打开 MDK-ARM/Project.uvprojx
- 编译工程 (F7)
- 通过 ST-Link / J-Link 下载到 STM32F103C8T6 开发板

### 4. 手机端

在手机上安装 TCP 调试助手 App：
- 创建 TCP Server，监听端口 8080
- 确保手机和 ESP8266 连接同一个 WiFi
- 设备启动后，手机端每 3 秒收到一条传感器数据

---

## 效果展示 / Demo

OLED 显示：

Temp: 25 C
Humi: 60 %
Lux:  100.0 lx
WiFi: OK

手机 TCP 端收到的数据格式：

Temp:25C Humi:60% Lux:100.0lx

---

## 许可证 / License

本项目用户应用层代码采用 MIT License。

项目中包含的第三方组件遵循其各自的许可证：
- **FreeRTOS** — MIT License (Copyright Amazon.com, Inc.)
- **STM32F10x Standard Peripheral Library** — STMicroelectronics 许可协议
- **CMSIS** — Apache 2.0 License (Copyright ARM Limited)

---

## 作者 / Author

如果你觉得这个项目有用，请给个 Star!

If you find this project helpful, please give it a Star!
