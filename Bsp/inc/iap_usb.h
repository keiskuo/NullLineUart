/*******************************************************************************
 * @file      iap_usb.h
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief USB IAP 通道（boot 专用，移植官方 USB_IAP 例程）
 *        VID 0x1A86 / PID 0x55E0，EP2 bulk 64B，轮询模式（不开中断）。
 *******************************************************************************/

#ifndef __IAP_USB_H
#define __IAP_USB_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "CH57x_common.h"
#include "sys_info.h" /* jumpApp / APP_CODE_START_ADDR / APP_CODE_END_ADDR / bootMode */

    /* IAP 命令码（官方） */
#define CMD_IAP_PROM   0x80
#define CMD_IAP_ERASE  0x81
#define CMD_IAP_VERIFY 0x82
#define CMD_IAP_END    0x83

#define IAP_LEN 64

    /* USB IAP 数据包结构（官方 USB_IAP/iap.h） */
    typedef union _IAP_CMD_STRUCT
    {
        struct
        {
            uint8_t cmd;
            uint8_t len;
            uint8_t addr[4];
        } erase;
        struct
        {
            uint8_t cmd;
            uint8_t len;
            uint8_t status[2];
        } end;
        struct
        {
            uint8_t cmd;
            uint8_t len;
            uint8_t addr[4];
            uint8_t buf[IAP_LEN - 6];
        } verify;
        struct
        {
            uint8_t cmd;
            uint8_t len;
            uint8_t buf[IAP_LEN - 2];
        } program;
        struct
        {
            uint8_t buf[IAP_LEN];
        } other;
    } iap_cmd_t;

    /* USB IAP 通道接口 */
    void iap_usb_init(void); /* USB 设备初始化（轮询模式，PID 0x55E0） */
    void iap_usb_poll(void); /* 轮询主循环，内含超时无数据则 jumpApp */

    /* boot 安全跳回 app（Main_boot.c 实现：关 SysTick + 关中断 + 裸跳） */
    void boot_jump_app(void);

#ifdef __cplusplus
}
#endif

#endif /* __IAP_USB_H */
