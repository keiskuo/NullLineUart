/*******************************************************************************
 * @file      led.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-03
 *
 * @copyright Copyright (c) 2026
 * @brief     led模块，提供LED初始化和控制功能
 *
 ******************************************************************************/

#include "CH57x_common.h"
#include "led.h"

#define LED_PIN bTXD_4

/*******************************************************************************
 * @author    keiskuo (ke.l@foxmail.com)
 * @date      2026-10-03
 *
 * @brief     LED控制模块，负责初始化LED引脚
 *
 ******************************************************************************/
void led_init(void)
{
    GPIOA_ResetBits(LED_PIN);
    GPIOA_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);
}

/*******************************************************************************
 * @author    keiskuo (ke.l@foxmail.com)
 * @date      2026-10-03
 * @param     interval - LED闪烁的时间间隔，单位为毫秒
 *
 * @brief     以指定时间间隔切换LED状态
 *
 ******************************************************************************/
void led_toggle(uint16_t interval)
{
    GPIOA_SetBits(LED_PIN);
    DelayMs(interval);
    GPIOA_ResetBits(LED_PIN);
    DelayMs(interval);
}
