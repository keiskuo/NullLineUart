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
#include "sys_info.h"
#include "uart_debug.h"
#include "usb_uart.h"
#include "version.h"

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
    SysTick_Config(GetSysClock() / 1000); /* mDelaymS 延时基准 */

    // 上电即标记 APP 镜像：升级跳回或 app 跑飞后，下次上电 boot 仍可正确判断
    sys_info_switch_app();

    led_init();        /* PA7 */
    uart_debug_init(); /* PA3 UART0，PRINT 调试输出 */
    PRINT(" Version Info: %s\n", FW_VERSION_FULL);
    USB_Init();
    RFRole_Init();       /* 射频硬件 + 使能 BLEB(基带)/BLEL(链路层) 中断 */
    RF_UartDongleInit(); /* 协议状态机 + 注册收发回调 */

    while(1)
    {
        USB_StatusQuery();
        RF_LedUpdate();

        if(USB_PollEnterBoot()) /* COM 口收到进 boot 魔数 */
        {
            sys_info_enter_iap(SYS_BOOT_MODE_USB);
            DelayMs(20); /* 等待残余 USB 数据发送 */
            /* 复位前先干净断开 USB：让主机看到"设备移除"，
               否则 CH340(0x7523) 连接中直接复位会导致重枚举
               PID 0x55E0 时枚举状态错乱 → 设备描述符请求失败 */
            R8_USB_CTRL = RB_UC_RESET_SIE;
            R16_PIN_ALTERNATE &= ~(RB_UDP_PU_EN | RB_PIN_USB_EN);
            DelayMs(100); /* 给主机识别断开的时间 */
            SYS_ResetExecute(); /* 复位进 boot（boot 按 USB 通道升级） */
        }
    }
}
