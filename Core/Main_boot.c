/*******************************************************************************
 * @file      Main_boot.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief IAP 引导（dongle / dev 共用单一 boot 镜像）
 *        启动读共享信息页：ImageFlag != IAP → 立即跳回 app；
 *        为 IAP 时进入升级流程（Step3/4 补齐 USB / UART-RF 通道），
 *        超时无升级动作则跳回 app。
 *
 *        关键约束（踩坑记录）：
 *        1. boot 不开 SysTick 中断（与官方 IAP 例程一致）：
 *           boot 若开 SysTick，jumpApp 后 app 的 handle_reset 拷贝
 *           highcode/向量表到同一 RAM 地址 0x20000000 期间，中断触发
 *           会跳到正被覆写的向量表 → 跑飞。DelayMs 自动走
 *           loopDelayus 兜底，无需 SysTick。
 *        2. jumpApp 前关 SysTick + 全局中断双保险。
 *        3. 启动文件 mstatus=0x1888（MPP=机器模式）：boot→app 二次 mret
 *           场景下，U-mode 执行 app handle_reset 的 csrw 特权指令会
 *           trap 死机。
 *******************************************************************************/

#include "CH57x_common.h"
#include "iap_usb.h"
#include "led.h"
#include "sys_info.h"

/* 无升级数据超时时间，超时后跳回 app（同官方例程 120s 量级） */
#define BOOT_IDLE_TIMEOUT_MS 120000u

/*********************************************************************
 * @fn      boot_jump_app
 *
 * @brief   跳转 app：先关 SysTick 与全局中断双保险，再裸跳
 */
__HIGH_CODE
void boot_jump_app(void)
{
    SysTick->CTLR = 0;    /* 关 SysTick 外设（STE/STIE 全清） */
    SysTick->CNTL = 0;
    PFIC_DisableAllIRQ(); /* 清 mstatus.MIE（boot 运行在 M-mode，合法） */
    jumpApp();
}

/*********************************************************************
 * @fn      main
 *
 * @brief   boot 入口
 */
__HIGH_CODE
int main(void)
{
    // 关闭仿真调试接口
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_12p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
    /* 不调 SysTick_Config：boot 全程不开中断（见文件头约束 1） */

    sys_info_t info;
    sys_info_read(&info);
    if(info.ImageFlag != FLAG_USER_CALL_IAP)
    {
        // 非升级请求上电：直接进 app（每次正常上电都走此路径）
        boot_jump_app();
    }

    led_init();
    led_on(); // 常亮：指示处于 IAP 模式

    /* 按启动通道分流：USB(Dongle) / UART(Detach, Step4 接入) */
    if(info.bootMode == SYS_BOOT_MODE_USB)
    {
        iap_usb_init();
        iap_usb_poll(); /* 内部含超时无数据 → boot_jump_app */
    }

    // Step4 前占位：UART(RF) 升级通道尚未接入，超时后跳回 app
    for(uint32_t waited = 0; waited < BOOT_IDLE_TIMEOUT_MS; waited += 100)
    {
        DelayMs(100);
    }
    boot_jump_app();
    return 0;
}

/******************************** endfile @boot ************************/
