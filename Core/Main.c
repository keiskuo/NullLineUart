/*******************************************************************************
 * @file      Main.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-09-29
 *
 * @copyright Copyright (c) 2026
 * @brief     LED闪烁示例
 *
 ******************************************************************************/

#include "CH57x_common.h"

/*******************************************************************************
 * @author    keiskuo (ke.l@foxmail.com)
 * @date      2026-09-29
 * @return    int
 *
 * @brief     主函数
 *
 ******************************************************************************/
__HIGH_CODE
int main()
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_12p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
    /* SysTick 作为延时基准：1mS 计数周期，只启动计数器、不使能Systick中断 */
    __SysTick_Config(FREQ_SYS / 1000); // SysTick 1ms 中断
    GPIOA_SetBits(bTXD_4);
    GPIOA_ModeCfg(bTXD_4, GPIO_ModeOut_PP_5mA);

    while(1)
    {
        GPIOA_SetBits(bTXD_4);
        DelayMs(1000);
        GPIOA_ResetBits(bTXD_4);
        DelayMs(1000);
    }
}
