/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_uart_rx.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2022/03/10
 * Description        :
 *******************************************************************************/

#ifndef __RF_UART_DONGLE_H
#define __RF_UART_DONGLE_H

#include <CH572rf.h>

#include "CH57x_common.h"
#include "buf.h"

#define RF_BUF_LEN    512
#define CONN_INTERVAL 10
#define CONN_TIMEOUT  100
#define CONN_PHY_TYPE 1 // 2M

void    RF_UartDongleInit(void);
uint8_t RF_DongleRxQuery(void* buf, typeBufSize* len);
void    RF_LedUpdate(void);

#endif
