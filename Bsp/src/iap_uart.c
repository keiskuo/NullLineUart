/*******************************************************************************
 * @file      iap_uart.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief boot 的 UART-IAP 状态机（移植官方 UART_IAP 例程）。
 *        字节由 RF 链路经 iap_uart_feed_byte 喂入（主循环，flash 操作安全），
 *        解析 AA55...55AA 帧并操作 flash，应答经 iap_uart_get_response 取回
 *        发回 RF。END 或空闲超时 → 跳回 app。
 *        运行在 highcode（ram），避免擦写 flash 期间执行冲突。
 *******************************************************************************/

#include "iap_uart.h"

/* flash 操作需在 ram 中运行 */

void boot_jump_app(void); /* Main_boot.c */

static uint8_t g_update_permition = 0;
static uint8_t iap_rec_data_state = 0; /* 0=WAIT_SOP1 */
static uint8_t all_data_rec_cnt = 0;
static uint8_t part_rec_cnt = 0;

static iap_cmd_t iap_rec_data;
__attribute__((aligned(4))) static uint8_t g_write_buf[256 + 64];
static uint16_t g_buf_write_ptr = 0;
static uint32_t g_flash_write_ptr = 0;

static uint8_t iap_rsp_data[6] = {IAP_DATA_SOP1, IAP_DATA_SOP2, 0, 0, IAP_DATA_EOP1, IAP_DATA_EOP2};
static uint8_t g_iap_rsp_pending = 0;
uint8_t g_iap_busy = 0;
static uint32_t g_iap_last_activity = 0;
static void (*g_iap_yield)(void) = 0;

#define IAP_IDLE_TIMEOUT_MS 120000u

/* 状态枚举 */
enum
{
    ST_WAIT_SOP1 = 0,
    ST_WAIT_SOP2,
    ST_WAIT_CMD,
    ST_WAIT_LEN,
    ST_WAIT_ADDR,
    ST_WAIT_DATA,
    ST_WAIT_CHECKNUM,
    ST_WAIT_EOP1,
    ST_WAIT_EOP2,
    ST_OK,
};

__attribute__((section(".highcode")))
static void my_memcpy_u(void* dst, const void* src, uint32_t l)
{
    uint8_t* d = (uint8_t*)dst;
    uint8_t* s = (uint8_t*)src;
    while(l--)
    {
        *d++ = *s++;
    }
}

void iap_uart_init(void)
{
    iap_rec_data_state = ST_WAIT_SOP1;
    all_data_rec_cnt   = 0;
    part_rec_cnt       = 0;
    g_buf_write_ptr    = 0;
    g_flash_write_ptr  = APP_CODE_START_ADDR;
    g_update_permition = 0;
    g_iap_rsp_pending  = 0;
    g_iap_busy         = 0;
    g_iap_last_activity = SysRunTime_ms;
}

/*********************************************************************
 * @fn      iap_uart_set_yield
 *
 * @brief   设置长擦除块间的回调（服务 RF 链路保持连接）
 */
void iap_uart_set_yield(void (*fn)(void))
{
    g_iap_yield = fn;
}

uint8_t iap_uart_get_response(uint8_t* buf, uint8_t* len)
{
    if(g_iap_rsp_pending)
    {
        g_iap_rsp_pending = 0;
        my_memcpy_u(buf, iap_rsp_data, 6);
        *len = 6;
        return 1;
    }
    return 0;
}

void iap_uart_tick(void)
{
    if(SysRunTime_ms - g_iap_last_activity > IAP_IDLE_TIMEOUT_MS)
    {
        boot_jump_app();
    }
}

/*********************************************************************
 * @fn      iap_uart_feed_byte
 *
 * @brief   喂入一个接收字节，驱动状态机；完整帧则校验+执行命令+排队应答
 */
__attribute__((section(".highcode")))
void iap_uart_feed_byte(uint8_t b)
{
    g_iap_last_activity   = SysRunTime_ms;
    all_data_rec_cnt++;
    if(all_data_rec_cnt >= sizeof(iap_rec_data))
    {
        all_data_rec_cnt = 0;
    }
    iap_rec_data.other.buf[all_data_rec_cnt] = b;

    switch(iap_rec_data_state)
    {
    case ST_WAIT_SOP1:
        if(iap_rec_data.other.buf[all_data_rec_cnt] == IAP_DATA_SOP1)
        {
            iap_rec_data.other.buf[0] = iap_rec_data.other.buf[all_data_rec_cnt];
            all_data_rec_cnt          = 0;
            iap_rec_data_state        = ST_WAIT_SOP2;
        }
        break;
    case ST_WAIT_SOP2:
        if(iap_rec_data.other.buf[all_data_rec_cnt] == IAP_DATA_SOP2)
        {
            iap_rec_data_state = ST_WAIT_CMD;
        }
        else
        {
            iap_rec_data_state = ST_WAIT_SOP1;
        }
        break;
    case ST_WAIT_CMD:
        if((iap_rec_data.other.buf[all_data_rec_cnt] < CMD_IAP_PROM) || (iap_rec_data.other.buf[all_data_rec_cnt] > CMD_IAP_END))
        {
            iap_rec_data_state = ST_WAIT_SOP1;
        }
        else
        {
            iap_rec_data_state = ST_WAIT_LEN;
        }
        break;
    case ST_WAIT_LEN:
        if(iap_rec_data.other.buf[all_data_rec_cnt] <= IAP_LEN)
        {
            part_rec_cnt = 0;
            if((iap_rec_data.other.buf[2] == CMD_IAP_ERASE) || (iap_rec_data.other.buf[2] == CMD_IAP_VERIFY))
            {
                iap_rec_data_state = ST_WAIT_ADDR;
            }
            else if(iap_rec_data.other.buf[3] > 0)
            {
                iap_rec_data_state = ST_WAIT_DATA;
            }
            else
            {
                iap_rec_data_state = ST_WAIT_CHECKNUM;
            }
        }
        else
        {
            iap_rec_data_state = ST_WAIT_SOP1;
        }
        break;
    case ST_WAIT_ADDR:
        part_rec_cnt++;
        if(part_rec_cnt >= 4)
        {
            part_rec_cnt = 0;
            if(iap_rec_data.other.buf[3] > 0)
            {
                iap_rec_data_state = ST_WAIT_DATA;
            }
            else
            {
                iap_rec_data_state = ST_WAIT_CHECKNUM;
            }
        }
        break;
    case ST_WAIT_DATA:
        part_rec_cnt++;
        if(part_rec_cnt >= iap_rec_data.other.buf[3])
        {
            iap_rec_data_state = ST_WAIT_CHECKNUM;
            part_rec_cnt       = 0;
        }
        break;
    case ST_WAIT_CHECKNUM:
        part_rec_cnt++;
        if(part_rec_cnt >= 2)
        {
            iap_rec_data_state = ST_WAIT_EOP1;
        }
        break;
    case ST_WAIT_EOP1:
        if(iap_rec_data.other.buf[all_data_rec_cnt] == IAP_DATA_EOP1)
        {
            iap_rec_data_state = ST_WAIT_EOP2;
        }
        else
        {
            iap_rec_data_state = ST_WAIT_SOP1;
        }
        break;
    case ST_WAIT_EOP2:
        if(iap_rec_data.other.buf[all_data_rec_cnt] == IAP_DATA_EOP2)
        {
            iap_rec_data_state = ST_OK;
        }
        else
        {
            iap_rec_data_state = ST_WAIT_SOP1;
        }
        break;
    default:
        break;
    }

    if(iap_rec_data_state == ST_OK)
    {
        uint16_t check_num = 0, check_num_rec, check_num_i;

        iap_rsp_data[2]    = 0x00;
        iap_rsp_data[3]    = 0x00;
        iap_rec_data_state = ST_WAIT_SOP1;

        for(check_num_i = 2; check_num_i < all_data_rec_cnt - 3; check_num_i++)
        {
            check_num += iap_rec_data.other.buf[check_num_i];
        }
        check_num_rec = iap_rec_data.other.buf[check_num_i] | (iap_rec_data.other.buf[check_num_i + 1] << 8);

        if(check_num_rec == check_num)
        {
            switch(iap_rec_data.other.buf[2])
            {
            case CMD_IAP_PROM:
                if(g_update_permition == 1)
                {
                    if(iap_rec_data.program.len == 0)
                    {
                        if(g_buf_write_ptr != 0)
                        {
                            if(FLASH_ROM_WRITE(g_flash_write_ptr, (PUINT32)g_write_buf, g_buf_write_ptr))
                            {
                                iap_rsp_data[2] = 0xfe;
                                iap_rsp_data[3] = IAP_ERR_WRITE_FAIL;
                            }
                            g_buf_write_ptr = 0;
                        }
                    }
                    else
                    {
                        my_memcpy_u(g_write_buf + g_buf_write_ptr, iap_rec_data.program.data, iap_rec_data.program.len);
                        g_buf_write_ptr += iap_rec_data.program.len;
                        if(g_buf_write_ptr >= 256)
                        {
                            if(FLASH_ROM_WRITE(g_flash_write_ptr, (PUINT32)g_write_buf, 256))
                            {
                                iap_rsp_data[2] = 0xfe;
                                iap_rsp_data[3] = IAP_ERR_WRITE_FAIL;
                                break;
                            }
                            g_flash_write_ptr += 256;
                            g_buf_write_ptr = g_buf_write_ptr - 256;
                            my_memcpy_u(g_write_buf, g_write_buf + 256, g_buf_write_ptr);
                        }
                    }
                }
                else
                {
                    iap_rsp_data[2] = 0xfe;
                    iap_rsp_data[3] = IAP_ERR_PROG_NO_ERASE;
                }
                break;
            case CMD_IAP_ERASE:
            {
                if(iap_rec_data.erase.addr == APP_CODE_START_ADDR)
                {
                    /* 分 4K 块擦除，块间回调服务 RF 链路保持连接：
                       整区一次擦除约 200ms 会阻塞主循环，RF 得不到服务，
                       dongle 侧超时断链后应答发不出去 */
                    uint32_t ea    = APP_CODE_START_ADDR;
                    uint8_t  efail = 0;
                    while(ea < APP_CODE_END_ADDR)
                    {
                        uint32_t chunk = APP_CODE_END_ADDR - ea;
                        if(chunk > 4096)
                        {
                            chunk = 4096;
                        }
                        if(FLASH_ROM_ERASE(ea, chunk) != 0)
                        {
                            efail = 1;
                            break;
                        }
                        ea += chunk;
                        if(g_iap_yield)
                        {
                            g_iap_yield(); /* 服务 RF，维持 dongle 链接 */
                        }
                    }
                    if(!efail)
                    {
                        g_update_permition = 1;
                        g_buf_write_ptr    = 0;
                        g_flash_write_ptr  = APP_CODE_START_ADDR;
                    }
                    else
                    {
                        iap_rsp_data[2] = 0xfe;
                        iap_rsp_data[3] = IAP_ERR_ERASE_FAIL;
                    }
                }
                else
                {
                    iap_rsp_data[2] = 0xfe;
                    iap_rsp_data[3] = IAP_ERR_ADDR;
                }
                break;
            }
            case CMD_IAP_VERIFY:
                if(((iap_rec_data.verify.addr % 4) == 0) && (iap_rec_data.verify.addr >= APP_CODE_START_ADDR) && (iap_rec_data.verify.addr < APP_CODE_END_ADDR))
                {
                    my_memcpy_u(g_write_buf, iap_rec_data.verify.data, iap_rec_data.verify.len);
                    if(FLASH_ROM_VERIFY(iap_rec_data.verify.addr, g_write_buf, iap_rec_data.verify.len))
                    {
                        iap_rsp_data[2] = 0xfe;
                        iap_rsp_data[3] = IAP_ERR_VERIFY;
                    }
                }
                else
                {
                    iap_rsp_data[2] = 0xfe;
                    iap_rsp_data[3] = IAP_ERR_ADDR;
                }
                break;
            case CMD_IAP_END:
                boot_jump_app(); /* 升级完成，跳回 app */
                break;
            default:
                iap_rsp_data[2] = 0xfe;
                iap_rsp_data[3] = IAP_ERR_UNKNOWN;
                break;
            }
        }
        else
        {
            iap_rsp_data[2] = 0xfe;
            iap_rsp_data[3] = IAP_ERR_CHECK;
        }
        g_iap_rsp_pending = 1;
    }
}

/******************************** endfile @iapuart ******************/