#ifndef __IAP_BRIDGE_H
#define __IAP_BRIDGE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "CH57x_common.h"

    /* dev boot 虚拟串口桥：把 RF 收到的字节接到 UART_IAP 状态机 */
    void     iap_bridge_init(void);
    void     iap_bridge_pump(void);                            /* 主循环:排空 rf_uart_dev 的 pRfBuf 喂 IAP */
    uint16_t iap_bridge_process(void); /* 主循环调用：喂 IAP 状态机，返回处理字节数 */

#ifdef __cplusplus
}
#endif

#endif /* __IAP_BRIDGE_H */