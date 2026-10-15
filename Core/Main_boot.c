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
#include "iap_bridge.h"
#include "iap_uart.h"
#include "iap_usb.h"
#include "led.h"
#include "rf.h"
#include "rf_uart_dev.h"
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
    /* dev boot 开了 RF 中断（BLEB/BLEL）：跳转前必须关闭，否则 app 的
       handle_reset 拷贝向量表/重建状态期间 RF 中断触发 → 跑飞卡死，
       表现为"升级成功但 dev 停在 boot、flag 未改 APP" */
    RFRole_Shut();              /* 停射频收发 */
    PFIC_DisableIRQ(BLEB_IRQn); /* 关 BLEB 中断使能 */
    PFIC_DisableIRQ(BLEL_IRQn); /* 关 BLEL 中断使能 */
    SysTick->CTLR = 0;          /* 关 SysTick 外设（STE/STIE 全清） */
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
    SysTick_Config(GetSysClock() / 1000); /* dev boot 需毫秒时基 */
    sys_info_t info;
    sys_info_read(&info);
    if(info.ImageFlag != FLAG_USER_CALL_IAP)
    {
        // 非升级请求上电：直接进 app（每次正常上电都走此路径）
        boot_jump_app();
    }

    led_init();
    led_on(); // 常亮：指示处于 IAP 模式

    /* 按启动通道分流 */
    if(info.bootMode == SYS_BOOT_MODE_USB)
    {
        iap_usb_init();
        iap_usb_poll(); /* 内部含超时无数据 → boot_jump_app */
    }
    else /* SYS_BOOT_MODE_UART：RF 链路上的 UART-IAP 升级（dev 端） */
    {
        iap_uart_init();                       /* 复位 IAP 状态机 */
        iap_uart_set_yield(RF_DevStatusQuery); /* 长擦除块间服务 RF，保持 dongle 链接 */
        iap_bridge_init();                     /* RF字节环形缓冲 */
        RFRole_Init();                         /* 射频硬件 + 中断 */
        RF_UartDevInit();                      /* 读配对信息 + 注册收发回调 */
        while(1)
        {
            iap_bridge_pump();    /* 【关键】排空 pRfBuf → IAP状态机(RF收到的ERASE等数据) */
            iap_bridge_process(); /* 环形缓冲兜底数据 → IAP状态机 */
            RF_DevStatusQuery();  /* RF链路：收发 IAP 应答/数据(内部 rf_led_update) */
            iap_uart_tick();      /* 空闲超时 → 跳回 app */
        }
    }
    return 0;
}

/******************************** endfile @boot ************************/
