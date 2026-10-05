/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_uart_dev.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2022/03/10
 * Description        : 无线串口-设备端（接真实设备串口 PA0/PA1 的一端）
 *******************************************************************************/

#ifndef __RF_UART_DEV_H
#define __RF_UART_DEV_H

#include <CH572rf.h>

#include "CH57x_common.h"
#include "buf.h"

#define ADV_INTERVAL          20
#define RESEND_COUNT          40
#define BOUND_INFO_FLASH_ADDR (1024 * 236)
#define RF_BUF_LEN            512

extern uint32_t           gRfRxFlag;
extern struct simple_buf* pRfBuf;

/* rf tx status */
#define STA_IDLE   0x00
#define STA_BUSY   0x01
#define STA_RESEND 0x02

void RF_UartDevInit(void);
void RF_DevStatusQuery(void);

#endif
