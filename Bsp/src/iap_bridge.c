/*******************************************************************************
 * @file      iap_bridge.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief dev boot 的"虚拟串口桥"：实现 uart_bridge.h 接口，但把 RF 收到的
 *        字节接到 UART_IAP 状态机（iap_uart.c），而不是物理 PA0/PA1。
 *        - UART_Send：rfProcessRx(RF中断)收到字节 -> 写入环形缓冲(ISR安全)
 *        - UART_RxQuery：主循环取 IAP 应答 -> 经 RF 发回 dongle
 *        - 主循环负责把环形缓冲的字节喂给 iap_uart_feed_byte(flash 操作在主循环)
 *******************************************************************************/

#include "CH57x_common.h"
#include "uart_bridge.h"
#include "iap_uart.h"
#include "rf_uart_dev.h" /* pRfBuf / gRfRxFlag: rf_uart_dev 的 RF 接收缓冲 */
#include "buf.h"

#define IAP_RX_RING_LEN 128
static uint8_t            iap_rx_ring[IAP_RX_RING_LEN];
static struct simple_buf  iap_rx_buf_obj;
static struct simple_buf* pIapRxBuf = NULL;

void iap_bridge_init(void)
{
    pIapRxBuf = simple_buf_create(&iap_rx_buf_obj, iap_rx_ring, sizeof(iap_rx_ring));
}

/* rfProcessRx(RF中断)调用：把收到的字节缓存，稍后主循环处理 */
__HIGH_CODE
void UART_Send(char* data, uint16_t size)
{
    if(pIapRxBuf == NULL)
    {
        return;
    }
    typeBufSize len = size;
    write_buf(pIapRxBuf, data, &len);
}

/*********************************************************************
 * @fn      iap_bridge_pump
 *
 * @brief   排空 rf_uart_dev 的接收缓冲 pRfBuf，把字节喂给 IAP 状态机。
 *          【关键】正常模式 pRfBuf 由 UART 中断排空发到设备串口；
 *          dev boot 没有 UART 中断，pRfBuf 无人消费，RF 收到的 IAP
 *          数据会死在缓冲里到不了状态机 —— 必须主循环主动排空。
 */
void iap_bridge_pump(void)
{
    uint8_t tmp[64];

    if(pRfBuf == NULL)
    {
        return;
    }
    for(;;)
    {
        typeBufSize n = sizeof(tmp);
        read_buf(pRfBuf, tmp, &n);
        if(n == 0)
        {
            break; /* 缓冲空 */
        }
        for(typeBufSize i = 0; i < n; i++)
        {
            iap_uart_feed_byte(tmp[i]);
        }
        gRfRxFlag = 0;
    }
}

/* 主循环调用：把本桥环形缓冲的字节喂给 IAP 状态机，返回处理的字节数 */
uint16_t iap_bridge_process(void)
{
    uint8_t     tmp[32];
    typeBufSize total = 0;

    if(pIapRxBuf == NULL)
    {
        return 0;
    }
    for(;;)
    {
        typeBufSize len = sizeof(tmp);
        if(read_buf(pIapRxBuf, tmp, &len) == 0 || len == 0)
        {
            break;
        }
        for(typeBufSize i = 0; i < len; i++)
        {
            iap_uart_feed_byte(tmp[i]);
        }
        total += len;
    }
    return total;
}

/* RF_DevStatusQuery(主循环)调用：取 IAP 应答经 RF 发回；
   周期性返回 0x80 驱动 RF 状态机发 BOUND_REQ/GET_STATUS（与 dev app 的
   TMR 定时器行为一致——否则 dev boot 永远不发绑定请求，连不上 dongle） */
__HIGH_CODE
uint8_t UART_RxQuery(void* buf, typeBufSize* len)
{
    static uint32_t last_poll = 0;
    uint8_t        rsp_len   = 0;

    if(iap_uart_get_response((uint8_t*)buf, &rsp_len))
    {
        *len = rsp_len;
        return 0; /* 有 IAP 应答，经 RF 发送 */
    }
    if(SysRunTime_ms - last_poll >= 10) /* 约 10ms 一次轮询事件 */
    {
        last_poll = SysRunTime_ms;
        *len      = 0;
        return 0x80;
    }
    *len = 0;
    return 0xFF; /* 无数据 */
}

void UART_SetTimer(uint16_t ms)
{
    (void)ms; /* dev boot 用自己的 tick 处理超时 */
}

void UART_SetBuad(uint32_t buad)
{
    (void)buad; /* dev boot 无物理串口 */
}

void uart_bridge_init(void)
{
    iap_bridge_init();
}

/******************************** endfile @iapbridge ******************/