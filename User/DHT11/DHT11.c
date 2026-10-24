#include "dht11.h"

static void DHT11_Delay_us(uint32_t us)
{
    volatile uint32_t cycles = us * 9;
    while(cycles--);
}

static void DHT11_GPIO_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {
        .GPIO_Pin = DHT11_GPIO_PIN,
        .GPIO_Mode = GPIO_Mode_IPU,
        .GPIO_Speed = GPIO_Speed_50MHz
    };
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);
}

static void DHT11_GPIO_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {
        .GPIO_Pin = DHT11_GPIO_PIN,
        .GPIO_Mode = GPIO_Mode_Out_PP,
        .GPIO_Speed = GPIO_Speed_50MHz
    };
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);
}


//使能
void DHT11_Init(void)
{
    RCC_APB2PeriphClockCmd(DHT11_RCC_APB, ENABLE);
}

uint8_t DHT11_Read(uint8_t *temp, uint8_t *humi)
{
    uint8_t buffer[5] = {0};
    uint32_t timeout;
		
		
		//起始信号 低电平18ms   
    DHT11_GPIO_Output();															
    GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
    DHT11_Delay_us(18000);
    GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
    DHT11_Delay_us(30);
    DHT11_GPIO_Input();
    DHT11_Delay_us(10);
		

    timeout = 0;
		//超时检测 超时直接退出函数
		//DHT11 收到起始信号后，会：
		//先拉低约 80us
		//再拉高约 80us
		//然后开始传输 40位数据
		
		//等待引脚从高变低，即 DHT11 开始响应
    while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_SET) {
        if(++timeout > 100) return 0;
        DHT11_Delay_us(1);
    }
		
		//
		//等待低电平结束，即 80us 低电平过去
    timeout = 0;
    while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_RESET) {
        if(++timeout > 200) return 0;
        DHT11_Delay_us(1);
    }
		
		// 等待高电平结束，即 80us 高电平过去
    timeout = 0;
    while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_SET) {
        if(++timeout > 200) return 0;
        DHT11_Delay_us(1);
    }
			
    for(uint8_t i=0; i<40; i++) {
        uint32_t wait = 0;
        while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_RESET) {
            if(++wait > 200) return 0;
            DHT11_Delay_us(1);
        }
				//高电平一般持续70us低电平一般28us  通过添加40us的延迟判断高低电平
        DHT11_Delay_us(40);
        if(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_SET) {
            buffer[i/8] |= (1 << (7 - (i%8)));		
            wait = 0;	
						//高电平一定要等他结束 不然会影响其他位	
            while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == Bit_SET) {
                if(++wait > 100) break;
                DHT11_Delay_us(1);
            }
        }
    }

		if ((uint8_t)(buffer[0] + buffer[1] + buffer[2] + buffer[3]) == buffer[4])  {
        *humi = buffer[0];
        *temp = buffer[2];
        return 1;
    }
    return 0;
}