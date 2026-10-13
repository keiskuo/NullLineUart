/*******************************************************************************
 * @file      sys_info.h
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief CODE Flash 末尾 4K 扇区：RF 配对信息与 IAP 镜像标志共用页。
 *        同一扇区内所有写操作必须“读-改-写”，避免互相覆盖。
 *******************************************************************************/

#ifndef __SYS_INFO_H
#define __SYS_INFO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "CH57x_common.h"

    /* CODE Flash 末尾 4K 扇区（236K 处），官方 IAP 标志区与本工程配对信息区 */
#define SYS_INFO_FLASH_ADDR (1024U * 236U)

    /* 镜像标志：IAP 镜像据此决定停留 boot 还是跳转 app */
#define FLAG_USER_CALL_IAP 0x55
#define FLAG_USER_CALL_APP 0xAA

    /* 进入 IAP 的传输通道（boot 据此选择 USB 或 UART(RF) 升级通路） */
#define SYS_BOOT_MODE_USB  0x00 // Dongle：官方 USB IAP
#define SYS_BOOT_MODE_UART 0x01 // Device：经 Dongle 透传的 UART IAP

    /* App 链接起始地址：boot 独占 0x0 ~ APP_CODE_START，Link.ld 与 boot 保持一致 */
#define APP_CODE_START_ADDR 0x00008000u
#define APP_CODE_END_ADDR   0x0003B000u /* 末尾 4K 为共享信息页，不可侵入 */

#define jumpApp ((void (*)(void))((int*)APP_CODE_START_ADDR))

    /**
     * @brief 扇区内容。前 12 字节与旧版配对信息(rfBoundInfo_t)布局兼容，
     *        旧固件写入的数据在新固件下配对信息依然有效。
     */
    typedef struct __attribute__((aligned(4)))
    {
        uint32_t head;      // 配对有效标志，见 BOUND_INFO_HEAD
        uint8_t  peerId[8]; // 对端唯一标识
        uint8_t  ImageFlag; // 镜像标志 FLAG_USER_CALL_xxx，0xFF=未初始化
        uint8_t  bootMode;  // 进入 IAP 的通道 SYS_BOOT_MODE_xxx
        uint8_t  revd[2];
    } sys_info_t;

    void   sys_info_read(sys_info_t* info);
    BOOL   sys_info_save_peer(const uint8_t* peerId); // RMW：仅改配对域
    BOOL   sys_info_enter_iap(uint8_t bootMode);      // RMW：置 IAP 标志+通道
    BOOL   sys_info_switch_app(void);                 // RMW：置 APP 标志

#ifdef __cplusplus
}
#endif

#endif /* __SYS_INFO_H */
