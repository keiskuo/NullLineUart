/*******************************************************************************
 * @file      iap_usb.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief boot �?USB IAP 通道（移植官�?USB_IAP 例程�?
 *        VID 0x1A86 / PID 0x55E0，EP2 bulk 64B，纯轮询不开中断
 *        （轮询可避免 jumpApp 前中断残�?+ 向量表拷贝冲突）�?
 *******************************************************************************/

#include "iap_usb.h"

#undef pSetupReqPak
#define pSetupReqPak ((PUSB_SETUP_REQ)EP0_Databuf)

#define DevEP0SIZE 64

/* ---- USB 描述符（官方 USB_IAP，PID 0x55E0�?---- */
// 设备描述�?
const uint8_t MyDevDescr[] =
{
    0x12, 0x01, 0x10, 0x01, 0xFF, 0x80, 0x55,
    DevEP0SIZE, 0x86, 0x1A, 0xE0, 0x55, // VID/PID = 0x1A86/0x55E0
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
};
// 配置描述�?
const uint8_t MyCfgDescr[] =
{
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32, 0x09, 0x04, 0x00, 0x00,
    0x02, 0xFF, 0x80, 0x55, 0x00, 0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};
// 语言描述�?
const uint8_t MyLangDescr[] =
{ 0x04, 0x03, 0x09, 0x04 };
// 厂家信息
const uint8_t MyManuInfo[] =
{ 0x0E, 0x03, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0 };
// 产品信息
const uint8_t MyProdInfo[] =
{ 0x0C, 0x03, 'C', 0, 'H', 0, '5', 0, '7', 0, 'x', 0 };

uint8_t DevConfig;
uint8_t SetupReqCode;
uint16_t SetupReqLen;
const uint8_t *pDescr;

/* ---- 端点 DMA 缓冲（用户自定义 RAM�?---- */
__attribute__((aligned(4))) uint8_t EP0_Databuf[64];      // ep0(64)，仅需 setup
__attribute__((aligned(4))) uint8_t EP2_Databuf[64 + 64]; // ep2_out(64)+ep2_in(64)
/* EP1/EP3 未被 IAP 使用（描述符仅 EP0/EP2），不分配缓冲以节省 RAM */

__attribute__((aligned(4))) uint8_t g_write_buf[256 + 64]; // 每满 256 字节写一�?flash
volatile uint16_t g_buf_write_ptr = 0;
volatile uint32_t g_flash_write_ptr = 0;
uint32_t g_tcnt = 0;
__attribute__((aligned(4))) iap_usb_cmd_t g_iap_cmd;

static void iap_usb_process(void);
static void myDevEP2_OUT_Deal(uint8_t l);
static void myDevEP2_IN_Deal(uint8_t s);
static void my_memcpy(void *dst, const void *src, uint32_t len);

/*********************************************************************
 * @fn      iap_usb_process
 *
 * @brief   USB 传输处理（轮询调用），移植官�?USB_DevTransProcess
 */
__HIGH_CODE
static void iap_usb_process(void)
{
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;

    intflag = R8_USB_INT_FG;
    if(intflag & RB_UIF_TRANSFER)
    {
        g_tcnt = 0; // USB 有数据，清空超时计数
        if(intflag & RB_U_IS_NAK)
        {
        }
        else
        {
            switch(R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
            {
            case UIS_TOKEN_IN | 2:
                R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                break;
            case UIS_TOKEN_OUT | 2:
                if(R8_USB_INT_ST & RB_UIS_TOG_OK)
                {
                    len = R8_USB_RX_LEN;
                    my_memcpy(g_iap_cmd.other.buf, EP2_Databuf, len);
                    myDevEP2_OUT_Deal(len);
                }
                break;
            case UIS_TOKEN_IN | 1:
                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                break;
            case UIS_TOKEN_IN:
                switch(SetupReqCode)
                {
                case USB_GET_DESCRIPTOR:
                    len = SetupReqLen >= DevEP0SIZE ? DevEP0SIZE : SetupReqLen;
                    my_memcpy(EP0_Databuf, pDescr, len);
                    SetupReqLen -= len;
                    pDescr += len;
                    R8_UEP0_T_LEN = len;
                    R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                    break;
                case USB_SET_ADDRESS:
                    R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
                    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    break;
                default:
                    R8_UEP0_T_LEN = 0;
                    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    break;
                }
                break;
            case UIS_TOKEN_OUT:
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            default:
                break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }

        if(R8_USB_INT_ST & RB_UIS_SETUP_ACT) // Setup 阶段
        {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            SetupReqLen  = pSetupReqPak->wLength;
            SetupReqCode = pSetupReqPak->bRequest;
            chtype       = pSetupReqPak->bRequestType;
            len          = 0;
            errflag      = 0;
            if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
            {
                errflag = 0xFF; /* 非标准请�?*/
            }
            else /* 标准请求 */
            {
                switch(SetupReqCode)
                {
                case USB_GET_DESCRIPTOR:
                    switch((pSetupReqPak->wValue) >> 8)
                    {
                    case USB_DESCR_TYP_DEVICE:
                        pDescr = MyDevDescr;
                        len    = MyDevDescr[0];
                        break;
                    case USB_DESCR_TYP_CONFIG:
                        pDescr = MyCfgDescr;
                        len    = MyCfgDescr[2];
                        break;
                    case USB_DESCR_TYP_STRING:
                        switch((pSetupReqPak->wValue) & 0xff)
                        {
                        case 1:
                            pDescr = MyManuInfo;
                            len    = MyManuInfo[0];
                            break;
                        case 2:
                            pDescr = MyProdInfo;
                            len    = MyProdInfo[0];
                            break;
                        case 0:
                            pDescr = MyLangDescr;
                            len    = MyLangDescr[0];
                            break;
                        default:
                            errflag = 0xFF;
                            break;
                        }
                        break;
                    default:
                        errflag = 0xFF;
                        break;
                    }
                    /* 同官�?206-213 行：截断到描述符实际长度�?
                       并把首包描述符数据预取进 EP0 缓冲（否�?EP0 �?
                       仍是 SETUP 包内容，主机读到垃圾 �?描述符失败） */
                    if(SetupReqLen > len)
                    {
                        SetupReqLen = len;
                    }
                    len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                    my_memcpy(EP0_Databuf, pDescr, len);
                    pDescr += len;
                    break;
                case USB_SET_ADDRESS:
                    SetupReqLen = (pSetupReqPak->wValue) & 0xff;
                    break;

                case USB_GET_CONFIGURATION:
                    EP0_Databuf[0] = DevConfig;
                    if(SetupReqLen > 1)
                    {
                        SetupReqLen = 1;
                    }
                    break;

                case USB_SET_CONFIGURATION:
                    DevConfig = (pSetupReqPak->wValue) & 0xff;
                    break;

                case USB_CLEAR_FEATURE:
                    if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        switch((pSetupReqPak->wIndex) & 0xff)
                        {
                        case 0x82:
                            R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                            break;
                        case 0x02:
                            R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
                            break;
                        case 0x81:
                            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                            break;
                        case 0x01:
                            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
                            break;
                        default:
                            errflag = 0xFF;
                            break;
                        }
                    }
                    else
                    {
                        errflag = 0xFF;
                    }
                    break;

                case USB_GET_INTERFACE:
                    EP0_Databuf[0] = 0x00;
                    if(SetupReqLen > 1)
                    {
                        SetupReqLen = 1;
                    }
                    break;

                case USB_GET_STATUS:
                    EP0_Databuf[0] = 0x00;
                    EP0_Databuf[1] = 0x00;
                    if(SetupReqLen > 2)
                    {
                        SetupReqLen = 2;
                    }
                    break;

                default:
                    errflag = 0xff;
                    break;
                }
            }
            if(errflag == 0xff) /* 请求不支持，返回 STALL */
            {
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
            }
            else
            {
                if(chtype & 0x80) /* 上传 */
                {
                    len = (SetupReqLen > DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                    SetupReqLen -= len;
                }
                else
                {
                    len = 0;
                }
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL  = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
            }
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    else if(intflag & RB_UIF_BUS_RST)
    {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP2_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP3_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    else if(intflag & RB_UIF_SUSPEND)
    {
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
    else
    {
        R8_USB_INT_FG = intflag;
    }
}

/*********************************************************************
 * @fn      myDevEP2_OUT_Deal
 *
 * @brief   端点2 数据下传：解�?IAP 命令并操�?Flash
 */
__HIGH_CODE
static void myDevEP2_OUT_Deal(uint8_t l)
{
    uint8_t s = 0;
    uint32_t addr;

    switch(g_iap_cmd.other.buf[0])
    {
    case CMD_IAP_PROM: /* 编程 */
        if(g_iap_cmd.program.len == 0)
        {
            if(g_buf_write_ptr != 0)
            {
                g_buf_write_ptr = ((g_buf_write_ptr + 3) & (~3)); /* 字节对齐 */
                s = FLASH_ROM_WRITE(g_flash_write_ptr, (PUINT32)g_write_buf, g_buf_write_ptr);
                g_buf_write_ptr = 0;
            }
        }
        else
        {
            my_memcpy(g_write_buf + g_buf_write_ptr, g_iap_cmd.program.buf, g_iap_cmd.program.len);
            g_buf_write_ptr += g_iap_cmd.program.len;
            if(g_buf_write_ptr >= 256)
            {
                s = FLASH_ROM_WRITE(g_flash_write_ptr, (PUINT32)g_write_buf, 256);
                g_flash_write_ptr += 256;
                g_buf_write_ptr = g_buf_write_ptr - 256;
                my_memcpy(g_write_buf, g_write_buf + 256, g_buf_write_ptr);
            }
        }
        myDevEP2_IN_Deal(s);
        break;

    case CMD_IAP_ERASE: /* 擦除 */
        addr = (g_iap_cmd.erase.addr[0]
                | (uint32_t)g_iap_cmd.erase.addr[1] << 8
                | (uint32_t)g_iap_cmd.erase.addr[2] << 16
                | (uint32_t)g_iap_cmd.erase.addr[3] << 24);
        if(addr == APP_CODE_START_ADDR)
        {
            s = FLASH_ROM_ERASE(APP_CODE_START_ADDR, APP_CODE_END_ADDR - APP_CODE_START_ADDR);
            g_buf_write_ptr  = 0;
            g_flash_write_ptr = APP_CODE_START_ADDR;
        }
        else
        {
            s = 0xfe;
        }
        myDevEP2_IN_Deal(s);
        break;

    case CMD_IAP_VERIFY: /* 校验 */
        my_memcpy(g_write_buf, g_iap_cmd.verify.buf, g_iap_cmd.verify.len);
        addr = (g_iap_cmd.verify.addr[0]
                | (uint32_t)g_iap_cmd.verify.addr[1] << 8
                | (uint32_t)g_iap_cmd.verify.addr[2] << 16
                | (uint32_t)g_iap_cmd.verify.addr[3] << 24);
        s = FLASH_ROM_VERIFY(addr, g_write_buf, g_iap_cmd.verify.len);
        myDevEP2_IN_Deal(s);
        break;

    case CMD_IAP_END: /* 结束：复�?USB 后跳�?app */
        R8_USB_CTRL = RB_UC_RESET_SIE;
        R16_PIN_ALTERNATE &= ~(RB_UDP_PU_EN | RB_PIN_USB_EN);
        DelayMs(10);
        boot_jump_app();
        break;

    default:
        myDevEP2_IN_Deal(0xfe);
        break;
    }
    (void)l;
}

/*********************************************************************
 * @fn      myDevEP2_IN_Deal
 *
 * @brief   端点2 数据上传：回�?2 字节状�?
 */
__HIGH_CODE
static void myDevEP2_IN_Deal(uint8_t s)
{
    EP2_Databuf[64] = s;
    EP2_Databuf[65] = 0;
    R8_UEP2_T_LEN   = 2;
    R8_UEP2_CTRL    = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

/*********************************************************************
 * @fn      my_memcpy
 *
 * @brief   数据拷贝（RAM 中运行，避免擦写 flash 期间执行冲突�?
 */
__HIGH_CODE
static void my_memcpy(void *dst, const void *src, uint32_t len)
{
    PUINT8 pdst = (PUINT8)dst;
    PUINT8 psrc = (PUINT8)src;
    while(len)
    {
        *pdst++ = *psrc++;
        len--;
    }
}

/*********************************************************************
 * @fn      iap_usb_init
 *
 * @brief   USB 设备初始化（轮询模式，不开 USB 中断，同官方 USB_IAP�?
 */
void iap_usb_init(void)
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    R8_USB_CTRL = 0x00; // 清除设定模式，取�?RB_UC_CLR_ALL
    R8_UEP4_1_MOD = RB_UEP4_RX_EN | RB_UEP4_TX_EN | RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN | RB_UEP3_RX_EN | RB_UEP3_TX_EN;

    R16_UEP0_DMA = (uint16_t)(uint32_t)EP0_Databuf;
    R16_UEP2_DMA = (uint16_t)(uint32_t)EP2_Databuf;
    /* EP1/EP3 无缓冲（未使用），跳过 DMA 分配 */

    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP4_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

    R8_USB_DEV_AD = 0x00;
    R8_USB_CTRL   = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ALTERNATE |= RB_PIN_USB_EN | RB_UDP_PU_EN;
    R8_USB_INT_FG = 0xFF;
    R8_UDEV_CTRL  = RB_UD_PD_DIS | RB_UD_PORT_EN;
    R8_USB_INT_EN = 0; // 禁止 USB 中断，采用轮询方�?
}

/*********************************************************************
 * @fn      iap_usb_poll
 *
 * @brief   轮询主循环（同官�?Main_Circulation）：
 *          超时�?USB 数据 �?跳回 app
 */
void iap_usb_poll(void)
{
    uint16_t j = 0;
    while(1)
    {
        j++;
        if(j > 5) // �?100us 处理一�?
        {
            j = 0;
            iap_usb_process();
        }
        DelayUs(20);
        g_tcnt++;
        if(g_tcnt > 3000000) // 超时无升级数�?�?跳回 app
        {
            R8_USB_CTRL = RB_UC_RESET_SIE;
            R16_PIN_ALTERNATE &= ~(RB_UDP_PU_EN | RB_PIN_USB_EN);
            DelayMs(10);
            boot_jump_app();
        }
    }
}

/******************************** endfile @iapusb **********************/
