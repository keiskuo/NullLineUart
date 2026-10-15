/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2022/06/30
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef __UART_BRIDGE_H
#define __UART_BRIDGE_H

#include "buf.h"

#define UART_BUF_LEN (1024 * 1) /* 设备串口缓冲；缩小以给 RF 调用链留足栈空间 */

#define TXD_PIN (1 << 0) // PA0
#define RXD_PIN (1 << 1) // PA1

/**
 * @brief   DTR/RTS功能与ST一键下载功能
 * @note    默认开启ST一键下载功能且关闭DTR/RTS功能；
 *          如需开启DTR/RTS功能，将DTR_RTS_FUNC置1即可；
 *          DTR / RESET_PIN     PA2
 *          RTS / BOOT_PIN      PA3
 */

#ifndef DTR_RTS_FUNC
    #define DTR_RTS_FUNC FALSE
#endif
#if ((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
    #define DTR_PIN (1 << 2)
    #define RTS_PIN (1 << 3)
#else
    #define RESET_PIN (1 << 2)
    #define BOOT_PIN  (1 << 3)
#endif

enum uart_status {
    UART_STATUS_IDLE,
    UART_STATUS_START,
    UART_STATUS_RCVING,
    UART_STATUS_RCV_END,
    UART_STATUS_SENDING,
    UART_STATUS_SEND,
    UART_STATUS_NUM,
};

#define DATA_LEN_UART (32)

void    uart_bridge_init(void);
uint8_t UART_RxQuery(void* buf, typeBufSize* len);
void    UART_SetBuad(uint32_t buad);
void    UART_SetTimer(uint16_t ms);
void    UART_Send(uint8_t* data, uint16_t size);

#endif /* __UART_BRIDGE_H */
