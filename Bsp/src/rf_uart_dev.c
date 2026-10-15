/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : 无线串口-设备端协议状态机
 *                      (接真实设备串口 PA0/PA1，兼带 RESET/BOOT 一键下载）
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "rf_uart_dev.h"

#include "led.h"
#include "rf.h"
#include "sys_info.h"
#include "uart_bridge.h"

static struct simple_buf rf_buffer;
static rfPackage_t*      pPkt_t;
static rfTxBuf_t         TxBuf;

static volatile uint8_t  RF_bound_Flag;
static volatile uint32_t RfDataEventTimeMs;

static uint32_t DeviceUniqueId[2];
static uint8_t  PairedDongleId[8];
static uint8_t  rf_buf[RF_BUF_LEN];

static uint16_t Interval;
static uint16_t TimeoutMax;
static uint16_t Timeout;
static uint8_t  TxDataSeq;
static uint8_t  RfStatus;
static uint8_t  BoundStatus;
static uint8_t  getDataProbe;

struct simple_buf* pRfBuf = NULL;
uint32_t           gRfRxFlag;

/* ===== 进入 boot 魔数扫描（dev app：PC 经 dongle 透传 ESC IAPDEV ESC） ===== */
static const uint8_t sDevIapMagic[] = {0x1B, 'I', 'A', 'P', 'D', 'E', 'V', 0x1B};
static uint8_t       sDevMagicIdx = 0;
uint8_t              gDevEnterIap = 0;

__HIGH_CODE
static void rf_scan_dev_iap(const uint8_t* data, typeBufSize len)
{
    for(typeBufSize i = 0; i < len; i++)
    {
        if(data[i] == sDevIapMagic[sDevMagicIdx])
        {
            sDevMagicIdx++;
            if(sDevMagicIdx >= sizeof(sDevIapMagic))
            {
                sDevMagicIdx = 0;
                gDevEnterIap  = 1;
                return;
            }
        }
        else
        {
            sDevMagicIdx = (data[i] == sDevIapMagic[0]) ? 1 : 0;
        }
    }
}

static void rfProcessRx(rfPackage_t* pPkt);
static void rfProcessTx(void);
static void rfProcessTimeout(void);

// tf status callbacks
rfStatusCBs_t rfCBs = {
    rfProcessRx,
    rfProcessTx,
    rfProcessTimeout,
    rfProcessTimeout,
};

__HIGH_CODE
static void rf_record_data_event(void)
{
    RfDataEventTimeMs = SysRunTime_ms;
}

static void rf_led_update(void)
{
    uint32_t now = SysRunTime_ms;

    if(!RF_bound_Flag)
    {
        if((now % 200U) < 100U)
        {
            led_on();
        }
        else
        {
            led_off();
        }
    }
    else if((uint32_t)(now - RfDataEventTimeMs) < 20U)
    {
        led_on();
    }
    else
    {
        led_off();
    }
}

/*********************************************************************
 * @fn      uart_buffer_create
 *
 * @brief   Create a file called uart_buffer simple buffer of the buffer
 *          and assign its address to the variable pointed to by the buffer pointer.
 *
 * @param   buf    -   a parameter buf pointing to a pointer.
 *
 * @return  none
 */
static void rf_buffer_create(struct simple_buf** buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_buf, sizeof(rf_buf));
}

static BOOL rf_uid_valid(const uint8_t* uniqueId)
{
    for(uint8_t index = 0; index < 8; index++)
    {
        if(uniqueId[index])
        {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL rf_uid_equal(const uint8_t* first, const uint8_t* second)
{
    for(uint8_t index = 0; index < 8; index++)
    {
        if(first[index] != second[index])
        {
            return FALSE;
        }
    }
    return TRUE;
}

/*******************************************************************************
 * @fn      rf_disconnect
 *
 * @brief   断开连接
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rf_disconnect(void)
{
    BoundStatus = BOUND_STATUS_IDLE;
    UART_SetTimer(ADV_INTERVAL);
    rf_tx_set_sync_word(AA);
    rf_tx_set_frequency(DEF_FREQUENCY);

    rf_rx_set_sync_word(AA);
    rf_rx_set_frequency(DEF_FREQUENCY);

    RF_bound_Flag = 0;
    PRINT("disconnect.\n");
}

static BOOL rf_write_bound_info(const rfBoundInfo_t* info)
{
    // 与 IAP 镜像标志共用同一扇区，内部读-改-写避免清掉 ImageFlag
    return sys_info_save_peer(info->peerId);
}

/*******************************************************************************
 * @fn      rf_bound
 *
 * @brief   断开连接
 *
 * @param   None.
 *
 * @return  None.
 */
__attribute__((noinline)) static void rf_bound(bound_rsp_t* rsp)
{
    rfBoundInfo_t info;
    uint8_t       hasDongleId = rf_uid_valid(rsp->dongleId);

    if(!hasDongleId || (rf_uid_valid(PairedDongleId) && !rf_uid_equal(rsp->dongleId, PairedDongleId)))
    {
        PRINT("dongle id mismatch.\n");
        return;
    }
    if(!rf_uid_valid(PairedDongleId))
    {
        info.head = BOUND_INFO_HEAD;
        for(uint8_t index = 0; index < 8; index++)
        {
            info.peerId[index] = rsp->dongleId[index];
        }
        if(!rf_write_bound_info(&info))
        {
            PRINT("dongle id save failed.\n");
            return;
        }
        for(uint8_t index = 0; index < 8; index++)
        {
            PairedDongleId[index] = rsp->dongleId[index];
        }
    }

    Timeout           = 0;
    BoundStatus       = BOUND_STATUS_WAIT;
    RfDataEventTimeMs = SysRunTime_ms - 20U;
    Interval          = rsp->interval;
    TimeoutMax        = (rsp->timeout * 10 / rsp->interval);
    rf_tx_set_sync_word(rsp->accessaddr);
    rf_tx_set_frequency(rsp->channel);
    rf_tx_set_phy_type(rsp->phy);
    rf_rx_set_sync_word(rsp->accessaddr);
    rf_rx_set_frequency(rsp->channel);
    rf_rx_set_phy_type(rsp->phy);
    RF_bound_Flag = 1;
    PRINT("bound success.%x %x\n", rsp->accessaddr, rsp->channel);
}

/*******************************************************************************
 * @fn      RF_ProcessRx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessRx(rfPackage_t* pPkt)
{
    if(TxDataSeq == pPkt->seq)
    {
        if(pPkt->type == PKT_DATA_RSP_ACK)
        {
            // 数据发送成功
            rf_record_data_event();
            if(pPkt->length > PKT_DATA_OFFSET + 1)
            {
                typeBufSize len;

                pPkt_t = pPkt;
                {
                    rfRsp_t* pRsp_t = (rfRsp_t*)(pPkt_t + 1);
                    len             = pPkt_t->length - PKT_DATA_OFFSET - 1;
                    rf_scan_dev_iap(pRsp_t->other.rspData, len);
                    gRfRxFlag       = write_buf(pRfBuf, pRsp_t->other.rspData, &len);
                    if(!len)
                    {
                        UART_Send(pRsp_t->other.rspData, pPkt_t->length - PKT_DATA_OFFSET - 1);
                    }
                    if(!R8_UART_TFC)
                    {
                        PFIC_SetPendingIRQ(UART_IRQn);
                    }
                }
            }
            getDataProbe = 6;
        }
        else if(pPkt->type == PKT_CMD_BOUND_RSP)
        {
            rf_bound((bound_rsp_t*)(pPkt + 1));
        }
        else if(pPkt->type == PKT_CMD_RSP_STATUS)
        {
            rfRsp_t* pRsp_t = (rfRsp_t*)(pPkt + 1);

            if(BoundStatus == BOUND_STATUS_WAIT)
            {
                BoundStatus = BOUND_STATUS_EST;
                UART_SetTimer(Interval);
            }

            if(pPkt->length == PKT_DATA_OFFSET)
            {}
            // 状态应答为对端设备波特率
            else if(pRsp_t->opcode == OPCODE_BSP)
            {
                if((pRsp_t->buad_t.BaudRate > 400000) && (pRsp_t->buad_t.BaudRate < 1000000))
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
                }
                else
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
                }
                SysTick_Config(GetSysClock() / 1000);
                mDelaymS(10);

                UART_BaudRateCfg(pRsp_t->buad_t.BaudRate);
                // 停止位
                if(pRsp_t->buad_t.StopBits)
                {
                    R8_UART_LCR |= RB_LCR_STOP_BIT; // 2个停止位
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_STOP_BIT; // 1个停止位
                }

                // 奇偶校验
                if(pRsp_t->buad_t.ParityType)
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_MOD;
                    R8_UART_LCR |= ((pRsp_t->buad_t.ParityType - 1) & 3) << 4;
                    R8_UART_LCR |= RB_LCR_PAR_EN;
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_EN;
                }

                // 数据位
                R8_UART_LCR &= ~RB_LCR_WORD_SZ;
                R8_UART_LCR |= (pRsp_t->buad_t.DataBits - 5);
                if(pRsp_t->buad_t.DataBits)
                {}

#if ((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
                // DTR 电平状态
                if(pRsp_t->buad_t.ioStaus & 0x20)
                {
                    GPIOA_SetBits(DTR_PIN);
                }
                else
                {
                    GPIOA_ResetBits(DTR_PIN);
                }
                // RTS 电平状态
                if(pRsp_t->buad_t.ioStaus & 0x40)
                {
                    GPIOA_SetBits(RTS_PIN);
                }
                else
                {
                    GPIOA_ResetBits(RTS_PIN);
                }
#endif
            }
            else if(pRsp_t->opcode == OPCODE_DATA)
            {
                typeBufSize len;

                pPkt_t       = pPkt;
                getDataProbe = 6;

#if (DTR_RTS_FUNC == FALSE)
                // RESET与BOOT引脚控制协议解析 仅在连接下载时触发
                if(pRsp_t->other.rspData[0] == 0x7f && (pPkt_t->length - PKT_DATA_OFFSET - 1) == 1)
                {
                    // BOOT脚拉高、RESET低电平复位进BOOT
                    GPIOA_SetBits(BOOT_PIN);
                    mDelaymS(1);
                    GPIOA_ResetBits(RESET_PIN);
                    mDelaymS(1);
                    GPIOA_SetBits(RESET_PIN);
                    mDelaymS(1);
                    GPIOA_ResetBits(BOOT_PIN);
                    mDelaymS(50);
                }
                if(pRsp_t->other.rspData[0] == 0x21 && pRsp_t->other.rspData[1] == 0xDE)
                {
                    GPIOA_ResetBits(RESET_PIN);
                    mDelaymS(1);
                    GPIOA_SetBits(RESET_PIN);
                }
#endif
                {
                    rfRsp_t* pRsp_t = (rfRsp_t*)(pPkt_t + 1);
                    len             = pPkt_t->length - PKT_DATA_OFFSET - 1;
                    rf_scan_dev_iap(pRsp_t->other.rspData, len);
                    gRfRxFlag       = write_buf(pRfBuf, pRsp_t->other.rspData, &len);
                    if(!len)
                    {
                        UART_Send(pRsp_t->other.rspData, pPkt_t->length - PKT_DATA_OFFSET - 1);
                    }
                    if(!R8_UART_TFC)
                    {
                        PFIC_SetPendingIRQ(UART_IRQn);
                    }
                }
                rf_record_data_event();
            }
        }
        else
        {}
        RfStatus     = RF_STATUS_IDLE;
        TxBuf.status = STA_IDLE;
        TxDataSeq++;
        Timeout = 0;
    }
}

/*******************************************************************************
 * @fn      RF_ProcessTx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessTx(void)
{
    if(RfStatus == RF_STATUS_RETX)
    {
        RfStatus = RF_STATUS_REWAIT;
    }
    else
    {
        RfStatus = RF_STATUS_WAITRSP;
    }
    rf_rx_start(150);
}

/*******************************************************************************
 * @fn      RF_ProcessTimeout
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessTimeout(void)
{
    TxBuf.status = STA_RESEND;
    if(RfStatus == RF_STATUS_WAITRSP)
    {
        TxBuf.resendCount = RESEND_COUNT;
    }
    else
    {
        if(BoundStatus == BOUND_STATUS_WAIT)
        {
            if(++Timeout > BOUND_EST_COUNT)
            {
                rf_disconnect();
            }
            TxBuf.status = STA_IDLE;
        }
        else if(BoundStatus == BOUND_STATUS_EST)
        {
            if(++Timeout > TimeoutMax)
            {
                rf_disconnect();
                TxBuf.status = STA_IDLE;
            }
        }
    }
}

/*******************************************************************************
 * @fn      RF_DevStatusQuery
 *
 * @brief   状态处理
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
void RF_DevStatusQuery(void)
{
    uint8_t s;

    rf_led_update();
    if(TxBuf.status == STA_IDLE)
    {
        rfPackage_t* pPkt_t = (rfPackage_t*)TxBuf.TxBuf;

        TxBuf.len = DATA_LEN_MAX_TX;
        s         = UART_RxQuery((void*)(pPkt_t + 1), &TxBuf.len);
        // 发送数据
        if(s == 0)
        {
            RfStatus       = RF_STATUS_TX;
            pPkt_t->type   = PKT_DATA_FLAG;
            pPkt_t->length = TxBuf.len + PKT_DATA_OFFSET;
            TxBuf.status   = STA_BUSY;
            pPkt_t->seq    = TxDataSeq;
            pPkt_t->resv   = 0;
            rf_tx_start(TxBuf.TxBuf, 60);
        }
        else if(s == 0x80)
        {
            if(BoundStatus)
            {
                // 获取状态
                RfStatus       = RF_STATUS_GETS;
                pPkt_t->type   = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
            }
            else
            {
                // 请求绑定
                bound_req_t* pReq_t = (bound_req_t*)&TxBuf.TxBuf[PKT_HEAD_LEN];

                RfStatus         = RF_STATUS_REQ;
                pPkt_t->type     = PKT_CMD_BOUND_REQ;
                pPkt_t->length   = PKT_DATA_OFFSET + sizeof(bound_req_t);
                pReq_t->interval = ADV_INTERVAL;
                for(uint8_t index = 0; index < 8; index++)
                {
                    pReq_t->deviceId[index] = ((uint8_t*)DeviceUniqueId)[index];
                }
                TxDataSeq = 0;
            }
            TxBuf.status = STA_BUSY;
            pPkt_t->seq  = TxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start(TxBuf.TxBuf, 60);
        }
        else
        {
            if(getDataProbe)
            {
                getDataProbe--;
                RfStatus       = RF_STATUS_GETS;
                pPkt_t->type   = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                TxBuf.status   = STA_BUSY;
                pPkt_t->seq    = TxDataSeq;
                pPkt_t->resv   = 0;
                rf_tx_start(TxBuf.TxBuf, 60);
            }
        }
    }
    else if(TxBuf.status == STA_RESEND)
    {
        if(TxBuf.resendCount)
        {
            if(TxBuf.resendCount < RESEND_COUNT / 2)
            {
                PRINT("*%d %d\n", TxBuf.resendCount, TxDataSeq);
            }
            RfStatus = RF_STATUS_RETX;
            if(TxBuf.resendCount != 0xFF) TxBuf.resendCount--;
            TxBuf.status = STA_BUSY;
            rf_tx_start(TxBuf.TxBuf, 60);
        }
        else
        {
            // 发送失败丢弃
            PRINT(" resend fail.%d\n", TxBuf.TxBuf[1]);
            TxBuf.status = STA_IDLE;
        }
    }
}

/*******************************************************************************
 * @fn      RF_UartDevInit
 *
 * @brief   RF uart发送应用初始化
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
void RF_UartDevInit(void)
{
    rfBoundInfo_t info;

    PRINT("----------------- rf uart tx mode -----------------\n");
    TxDataSeq    = 0;
    gRfRxFlag    = 0;
    BoundStatus  = BOUND_STATUS_IDLE;
    TxBuf.status = 0;
    rf_buffer_create(&pRfBuf);

    GET_UNIQUE_ID((uint8_t*)DeviceUniqueId);
    for(uint8_t index = 0; index < 8; index++)
    {
        PairedDongleId[index] = 0;
    }
    FLASH_ROM_READ(BOUND_INFO_FLASH_ADDR, &info, sizeof(info));
    if(info.head == BOUND_INFO_HEAD && rf_uid_valid(info.peerId))
    {
        for(uint8_t index = 0; index < 8; index++)
        {
            PairedDongleId[index] = info.peerId[index];
        }
    }
    RFRole_RegisterStatusCbs(&rfCBs);
}

/******************************** endfile @rf ******************************/
