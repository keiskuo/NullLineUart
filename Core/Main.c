/*******************************************************************************
 * @file      Main.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-09-29
 *
 * @copyright Copyright (c) 2026
 * @brief     CH572 USB转2.4G无线串口 Dongle
 *
 ******************************************************************************/

#include "CH57x_common.h"
#include "led.h"
#include "rf.h"
#include "rf_uart_dongle.h"
#include "uart_debug.h"
#include "usb_uart.h"

/*******************************************************************************
 * @author    keiskuo (ke.l@foxmail.com)
 * @date      2026-10-05
 *
 * @brief     主函数
 *
 ******************************************************************************/
__HIGH_CODE
int main()
{
    // 关闭仿真调试接口
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_12p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
    SysTick_Config(FREQ_SYS / 1000); /* mDelaymS 延时基准 */

    led_init();   /* PA7 */
    uart_debug_init(); /* PA3 UART0，PRINT 调试输出 */
    USB_Init();
    RFRole_Init();   /* 射频硬件 + 使能 BLEB(基带)/BLEL(链路层) 中断 */
    RF_UartDongleInit(); /* 协议状态机 + 注册收发回调 */

    while(1)
    {
        USB_StatusQuery();
        RF_LedUpdate();
    }
}
