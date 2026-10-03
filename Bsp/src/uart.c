/*******************************************************************************
 * @file      uart.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-03
 * 
 * @copyright Copyright (c) 2026
 * @brief     UART0配置模块源文件
 * 
 ******************************************************************************/

#define UART_TX_PIN bTXD_0
#define UART_RX_PIN bRXD_0

#include "CH57x_common.h"
#include "uart.h"

void uart0_init(void)
{
    GPIOA_ModeCfg(UART_TX_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(UART_RX_PIN, GPIO_ModeIN_PU);

    UART_DefInit();
}