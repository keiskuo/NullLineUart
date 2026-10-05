/*******************************************************************************
 * @file      Main_dev.c
 * @brief     CH572 2.4G 无线串口 —— 设备端（接真实设备串口的一端）
 *            与Dongle，PC 侧 USB 无线串口适配器成对。
 ******************************************************************************/

#include "CH57x_common.h"
#include "led.h"
#include "rf.h"
#include "rf_uart_dev.h"
#include "uart_bridge.h"

__attribute__((used)) __HIGH_CODE void process_main(void)
{
    while(1)
    {
        RF_DevStatusQuery();
    }
}

/*******************************************************************************
 * @brief 主函数
 ******************************************************************************/
__HIGH_CODE
int main(void)
{
    HSECFG_Capacitance(HSECap_12p);

    /*上电先跑 24MHz；收到 OPCODE_BSP 后按对端波特率在
      rf_uart_dev.c: rfProcessRx() 内切 24M / 100M */
    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
    UART_Init();
    PRINT("start.\n");
    PRINT("%s\n", VER_RF_LIB);
    RFRole_Init();    /* 射频硬件 + 使能 BLEB/BLEL 中断 */
    RF_UartDevInit(); /* 读 Flash 里的 serverData + 注册收发回调 */
    process_main();
}
