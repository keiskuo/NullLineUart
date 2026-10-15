#ifndef __IAP_UART_H
#define __IAP_UART_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "CH57x_common.h"
#include "sys_info.h" /* APP_CODE_START/END, jumpApp, bootMode */

    /* IAP 命令码（官方 UART_IAP） */
#define CMD_IAP_PROM   0x80
#define CMD_IAP_ERASE  0x81
#define CMD_IAP_VERIFY 0x82
#define CMD_IAP_END    0x83

#define IAP_LEN 64 /* 每包数据长度 */

#define IAP_DATA_SOP1 0xaa
#define IAP_DATA_SOP2 0x55
#define IAP_DATA_EOP1 0x55
#define IAP_DATA_EOP2 0xaa

    /* IAP 错误码（官方 UART_IAP） */
#define IAP_ERR_UNKNOWN       0
#define IAP_ERR_OVERTIME      1
#define IAP_ERR_CHECK         2
#define IAP_ERR_ADDR          3
#define IAP_ERR_ERASE_FAIL    4
#define IAP_ERR_PROG_NO_ERASE 5
#define IAP_ERR_WRITE_FAIL    6
#define IAP_ERR_VERIFY        7

    /* UART_IAP 数据包（带 SOP/EOP，官方 UART_IAP/iap.h） */
    typedef union _IAP_CMD_STRUCT
    {
        struct
        {
            uint8_t  sop1;
            uint8_t  sop2;
            uint8_t  cmd;
            uint8_t  len;
            uint32_t addr;
            uint16_t checkNum;
            uint8_t  eop1;
            uint8_t  eop2;
        } erase;
        struct
        {
            uint8_t  sop1;
            uint8_t  sop2;
            uint8_t  cmd;
            uint8_t  len;
            uint16_t checkNum;
            uint8_t  eop1;
            uint8_t  eop2;
        } end;
        struct
        {
            uint8_t  sop1;
            uint8_t  sop2;
            uint8_t  cmd;
            uint8_t  len;
            uint32_t addr;
            uint8_t  data[IAP_LEN];
            uint16_t checkNum;
            uint8_t  eop1;
            uint8_t  eop2;
        } verify;
        struct
        {
            uint8_t  sop1;
            uint8_t  sop2;
            uint8_t  cmd;
            uint8_t  len;
            uint8_t  data[IAP_LEN];
            uint16_t checkNum;
            uint8_t  eop1;
            uint8_t  eop2;
        } program;
        struct
        {
            uint8_t buf[IAP_LEN + 12];
        } other;
    } iap_cmd_t;

    /* boot 的 UART-IAP 通道（经 RF 收发字节） */
    void     iap_uart_init(void);                              /* 复位状态机 */
    void     iap_uart_feed_byte(uint8_t b);                    /* 喂入一个接收字节（主循环调用） */
    uint8_t  iap_uart_get_response(uint8_t* buf, uint8_t* len); /* 取应答(6B)，返回1=有 */
    void     iap_uart_tick(void);                              /* 超时处理(空闲→跳app) */
    void     iap_uart_set_yield(void (*fn)(void));             /* 长擦除块间回调(服务RF保活) */
    extern uint8_t g_iap_busy;                                 /* 1=正在处理IAP帧(用于超时判断) */

#ifdef __cplusplus
}
#endif

#endif /* __IAP_UART_H */