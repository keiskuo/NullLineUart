/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : 无线串口-接收端
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#include "rf_uart_dongle.h"

#include "led.h"
#include "rf.h"
#include "usb_uart.h"

static struct simple_buf* pRfBuf = NULL;
static struct simple_buf  rf_buffer;

static volatile uint8_t  RfTxActivityPending;
static volatile uint32_t RfDataEventTimeMs;
static volatile uint8_t  RF_bound_Flag;

static uint8_t  rf_buf[RF_BUF_LEN];
static uint8_t  BoundDeviceId[8];
static uint8_t  PendingDeviceId[8];
static uint32_t DongleUniqueId[2];

static rfTxBuf_t TxBuf;
static uint32_t  IntervalTimer;
static uint16_t  TimeoutMax;
static uint16_t  Timeout;

static uint8_t PairingPending;
static uint8_t DataSeq;
static uint8_t RfStatus;
static uint8_t BoundStatus;
static uint8_t RxDataStatus;

static void rfProcessRx(rfPackage_t* pPkt);
static void rfProcessTx(void);
static void rfProcessCrcError(void);
static void rfProcessTimeout(void);

// GAP Service Callbacks
rfStatusCBs_t rfCBs = {
    rfProcessRx,
    rfProcessTx,
    rfProcessCrcError,
    rfProcessTimeout,
};

__HIGH_CODE
static void rf_record_data_event(void)
{
    RfDataEventTimeMs = SysRunTime_ms;
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

__HIGH_CODE
static BOOL rf_uid_valid(const uint8_t* deviceId)
{
    for(uint8_t index = 0; index < 8; index++)
    {
        if(deviceId[index])
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

__HIGH_CODE
static BOOL rf_save_bound_info(const uint8_t* deviceId)
{
    rfBoundInfo_t info;

    if(!rf_uid_valid(deviceId))
    {
        return FALSE;
    }
    info.head = BOUND_INFO_HEAD;
    for(uint8_t index = 0; index < 8; index++)
    {
        info.peerId[index] = deviceId[index];
    }

    if(FLASH_ROM_ERASE(BOUND_INFO_FLASH_ADDR, 4096) != 0)
    {
        return FALSE;
    }
    if(FLASH_ROM_WRITE(BOUND_INFO_FLASH_ADDR, &info, sizeof(info)) != 0)
    {
        return FALSE;
    }
    return FLASH_ROM_VERIFY(BOUND_INFO_FLASH_ADDR, &info, sizeof(info)) == 0;
}

/*******************************************************************************
 * @fn      rf_rand
 *
 * @brief   随机数生成函数
 *
 * @return  None.
 */
__HIGH_CODE
uint32_t rf_rand16(uint32_t seed)
{
    static uint32_t holdrand;
    uint64_t        tmp;

    holdrand += seed;
    holdrand = holdrand * 1664525 + 1013904223;
    tmp      = holdrand;
    tmp      = (tmp * 0xFFFF) >> 32;
    return tmp;
}

/*******************************************************************************
 * @fn      rf_rand
 *
 * @brief   随机数生成函数
 *
 * @return  None.
 */
__HIGH_CODE
uint32_t rf_rand_aa(uint16_t rand)
{
    uint32_t aa = rand;

    aa = (aa << 8) | 0x6E0000B6;
    return aa;
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
static void rf_disconnect(void)
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n");
    RFRole_Shut();
    BoundStatus  = BOUND_STATUS_IDLE;
    RxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END); // 关闭中断

    rf_tx_set_sync_word(AA);
    rf_tx_set_frequency(DEF_FREQUENCY);
    rf_rx_set_sync_word(AA);
    rf_rx_set_frequency(DEF_FREQUENCY);
    RfStatus = RF_STATUS_WAIT;
    for(uint8_t index = 0; index < 8; index++)
    {
        PendingDeviceId[index] = 0;
    }
    PairingPending = 0;
}

/*******************************************************************************
 * @fn      rf_bound
 *
 * @brief   建立连接
 *
 * @param   None.
 *
 * @return  None.
 */
static void rf_bound(bound_rsp_t* rsp)
{
    Timeout           = 0;
    BoundStatus       = BOUND_STATUS_WAIT;
    RfDataEventTimeMs = SysRunTime_ms - 20U;
    rf_tx_set_sync_word(rsp->accessaddr);
    rf_tx_set_frequency(rsp->channel);
    rf_tx_set_phy_type(rsp->phy);

    rf_rx_set_sync_word(rsp->accessaddr);
    rf_rx_set_frequency(rsp->channel);
    rf_rx_set_phy_type(rsp->phy);
    RF_bound_Flag = 1;
    PRINT("bound success.%X %x\n", rsp->accessaddr, rsp->channel);
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
    rfPackage_t* pPkt_t = (rfPackage_t*)TxBuf.TxBuf;
    RfTxActivityPending = 0;

    if(BoundStatus == BOUND_STATUS_IDLE)
    {
        if(pPkt->type == PKT_CMD_BOUND_REQ)
        {
            uint16_t     radioSeed = 0;
            bound_req_t* pReq_t    = (bound_req_t*)(pPkt + 1);
            if(pPkt->length == sizeof(bound_req_t) + 2)
            {
                int8_t rssi = *(uint8_t*)((uint8_t*)pPkt + pPkt->length + 2 + 2);

                BOOL requestAccepted = FALSE;
                if(rf_uid_valid(BoundDeviceId))
                {
                    requestAccepted = rf_uid_equal(pReq_t->deviceId, BoundDeviceId);
                }
                else if(rf_uid_valid(pReq_t->deviceId) && rssi > -35)
                {
                    requestAccepted = TRUE;
                    PairingPending  = 1;
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        PendingDeviceId[index] = pReq_t->deviceId[index];
                    }
                }
                if(requestAccepted)
                {
                    bound_rsp_t* pRsp_t = (bound_rsp_t*)&TxBuf.TxBuf[PKT_HEAD_LEN];

                    for(uint8_t index = 0; index < 8; index++)
                    {
                        radioSeed = (uint16_t)((radioSeed << 5) | (radioSeed >> 11));
                        radioSeed ^= pReq_t->deviceId[index];
                    }
                    if(!radioSeed)
                    {
                        radioSeed = 1;
                    }
                    radioSeed = (uint16_t)rf_rand16(radioSeed);
                    if(!radioSeed)
                    {
                        radioSeed = 1;
                    }
                    RfStatus           = RF_STATUS_TXRSP;
                    DataSeq            = 0;
                    pPkt_t->type       = PKT_CMD_BOUND_RSP;
                    pPkt_t->length     = PKT_DATA_OFFSET + sizeof(bound_rsp_t);
                    pPkt_t->seq        = DataSeq;
                    pPkt_t->resv       = 0;
                    pRsp_t->accessaddr = rf_rand_aa(radioSeed);
                    pRsp_t->channel    = radioSeed & 0x3F;
                    pRsp_t->phy        = CONN_PHY_TYPE; // 2M
                    pRsp_t->interval   = CONN_INTERVAL;
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        pRsp_t->dongleId[index] = ((uint8_t*)DongleUniqueId)[index];
                    }
                    pRsp_t->timeout = CONN_TIMEOUT;
                    rf_tx_start(pPkt_t, 20);
                    DataSeq++;
                    IntervalTimer = pReq_t->interval * 1000;
                    TimeoutMax    = BOUND_EST_COUNT;
                    return;
                }
                else
                {
                    PRINT(" reject..\n");
                }
                PRINT("rssi=%d \n", rssi);
            }
        }
        RfStatus = RF_STATUS_WAIT;
    }
    else
    {
        rfRsp_t* pRsp_t = (rfRsp_t*)&TxBuf.TxBuf[PKT_HEAD_LEN];
        if(pPkt->type == PKT_CMD_GET_STATUS)
        {
            RfStatus = RF_STATUS_TX;
            if(pPkt->seq == DataSeq)
            {
                if(PairingPending && BoundStatus == BOUND_STATUS_WAIT)
                {
                    if(!rf_save_bound_info(PendingDeviceId))
                    {
                        PRINT("bound info save failed.\n");
                        for(uint8_t index = 0; index < 8; index++)
                        {
                            PendingDeviceId[index] = 0;
                        }
                        PairingPending = 0;
                        BoundStatus    = BOUND_STATUS_IDLE;
                        RF_bound_Flag  = 0;
                        rf_tx_set_sync_word(AA);
                        rf_tx_set_frequency(DEF_FREQUENCY);
                        rf_rx_set_sync_word(AA);
                        rf_rx_set_frequency(DEF_FREQUENCY);
                        RfStatus = RF_STATUS_RX;
                        rf_rx_start(IntervalTimer);
                        return;
                    }
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        BoundDeviceId[index]   = PendingDeviceId[index];
                        PendingDeviceId[index] = 0;
                    }
                    PairingPending = 0;
                }
                uint8_t     s;
                typeBufSize len = DATA_LEN_MAX_TX;
                pPkt_t->type    = PKT_CMD_RSP_STATUS;
                if(BoundStatus < BOUND_STATUS_EST)
                {
                    // 发送串口波特率
                    pPkt_t->length            = PKT_DATA_OFFSET + 1 + sizeof(Uart0Para);
                    pRsp_t->opcode            = OPCODE_BSP;
                    pRsp_t->buad_t.BaudRate   = Uart0Para.BaudRate;
                    pRsp_t->buad_t.StopBits   = Uart0Para.StopBits;
                    pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                    pRsp_t->buad_t.DataBits   = Uart0Para.DataBits;
                    pRsp_t->buad_t.ioStaus    = Uart0Para.ioStaus;
                    pPkt_t->seq               = DataSeq;
                    pPkt_t->resv              = 0;
                }
                else
                {
                    s = USB_RxQuery(pRsp_t->other.rspData, &len);
                    if(s == 0)
                    {
                        pPkt_t->length      = PKT_DATA_OFFSET + 1 + len;
                        pRsp_t->opcode      = OPCODE_DATA;
                        pPkt_t->seq         = DataSeq;
                        pPkt_t->resv        = 0;
                        RfTxActivityPending = 1;
                    }
                    else if(s == 0x80)
                    {
                        // 发送串口波特率
                        pPkt_t->length            = PKT_DATA_OFFSET + 1 + sizeof(Uart0Para);
                        pRsp_t->opcode            = OPCODE_BSP;
                        pRsp_t->buad_t.BaudRate   = Uart0Para.BaudRate;
                        pRsp_t->buad_t.StopBits   = Uart0Para.StopBits;
                        pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                        pRsp_t->buad_t.DataBits   = Uart0Para.DataBits;
                        pRsp_t->buad_t.ioStaus    = Uart0Para.ioStaus;
                        pPkt_t->seq               = DataSeq;
                        pPkt_t->resv              = 0;
                    }
                    else
                    {
                        pPkt_t->length = PKT_DATA_OFFSET;
                        pRsp_t->opcode = OPCODE_ACK;
                        pPkt_t->seq    = DataSeq;
                        pPkt_t->resv   = 0;
                    }
                }
                DataSeq++;
            }
            rf_tx_start(pPkt_t, 20);
            if(pRsp_t->opcode == OPCODE_BSP)
            {
                PRINT("uart param.\n");
            }
        }
        else if(pPkt->type == PKT_DATA_FLAG)
        {
            RfStatus = RF_STATUS_TX;
            if(pPkt->seq == DataSeq)
            {
                rf_record_data_event();
                typeBufSize len = pPkt->length;
                len -= PKT_DATA_OFFSET;
                write_buf(pRfBuf, (pPkt + 1), &len);
                RxDataStatus = DATA_STATUS_RCV;
                // 如果接收缓存满，则会丢数据
                len          = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_DATA_RSP_ACK;

                if(BoundStatus == BOUND_STATUS_EST && !USB_RxQuery(pRsp_t->other.rspData, &len))
                {
                    pPkt_t->length      = PKT_DATA_OFFSET + 1 + len;
                    pRsp_t->opcode      = OPCODE_DATA;
                    pPkt_t->seq         = DataSeq;
                    pPkt_t->resv        = 0;
                    RfTxActivityPending = 1;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->seq    = DataSeq;
                    pPkt_t->resv   = 0;
                }
                DataSeq++;
            }
            else
            {
                // 重传
            }
            rf_tx_start(pPkt_t, 20);
        }
        else
        {
            PRINT("error data.\n");
            if(++Timeout > TimeoutMax)
            {
                RxDataStatus = DATA_STATUS_TIMEOUT;
                PRINT("connect timeout.\n");
            }
            else
            {
                RfStatus = RF_STATUS_WAIT;
            }
            return;
        }
        if(BoundStatus == BOUND_STATUS_WAIT)
        {
            // 连接确认，启动连接通信超时
            BoundStatus   = BOUND_STATUS_EST;
            IntervalTimer = CONN_INTERVAL * 1000;
            TimeoutMax    = (CONN_TIMEOUT * 10 / CONN_INTERVAL);
        }
        Timeout = 0;
    }
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
static void rfProcessTx(void)
{
    if(RfTxActivityPending)
    {
        rf_record_data_event();
        RfTxActivityPending = 0;
    }
    if(RfStatus == RF_STATUS_TXRSP)
    {
        rf_bound((bound_rsp_t*)&TxBuf.TxBuf[PKT_HEAD_LEN]);
    }
    RfStatus = RF_STATUS_RX;
    rf_rx_start(IntervalTimer);
}

/*******************************************************************************
 * @fn      RF_ProcessRxError
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessCrcError(void)
{
    RfStatus = RF_STATUS_WAIT;
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
    RfTxActivityPending = 0;
    if(BoundStatus && ++Timeout > TimeoutMax)
    {
        RxDataStatus = DATA_STATUS_TIMEOUT;
        PRINT("connect timeout.\n");
    }
    else
    {
        RfStatus = RF_STATUS_WAIT;
    }
}

/*******************************************************************************
 * @fn      RF_DongleRxQuery
 *
 * @brief
 *
 * @return  None.
 */
__HIGH_CODE
void RF_LedUpdate(void)
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
        return;
    }

    if((uint32_t)(now - RfDataEventTimeMs) < 20U)
    {
        led_on();
    }
    else
    {
        led_off();
    }
}

__HIGH_CODE
uint8_t RF_DongleRxQuery(void* buf, typeBufSize* len)
{
    if(RxDataStatus == DATA_STATUS_RCV)
    {
        if(read_buf(pRfBuf, buf, len) == 0)
        {
            PFIC_DisableIRQ(BLEL_IRQn);
            RxDataStatus = DATA_STATUS_START;
            PFIC_EnableIRQ(BLEL_IRQn);
        }
    }
    else if(RxDataStatus == DATA_STATUS_TIMEOUT)
    {
        *len = 0;
        rf_disconnect();
    }
    else
    {
        *len = 0;
        if(RfStatus == RF_STATUS_WAIT)
        {
            RfStatus = RF_STATUS_RX;
            rf_rx_start(IntervalTimer);
        }
    }
    return *len;
}

/*******************************************************************************
 * @fn      RF_UartTxInit
 *
 * @brief   RF uart发送应用初始化
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
void RF_UartDongleInit(void)
{
    rfBoundInfo_t info;

    PRINT("----------------- rf uart rx mode -----------------\n");
    RxDataStatus = DATA_STATUS_IDLE;
    rf_buffer_create(&pRfBuf);
    DataSeq = 0;
    for(uint8_t index = 0; index < 8; index++)
    {
        BoundDeviceId[index]   = 0;
        PendingDeviceId[index] = 0;
    }
    PairingPending = 0;
    FLASH_ROM_READ(BOUND_INFO_FLASH_ADDR, &info, sizeof(info));
    if(info.head == BOUND_INFO_HEAD && rf_uid_valid(info.peerId))
    {
        for(uint8_t index = 0; index < 8; index++)
        {
            BoundDeviceId[index] = info.peerId[index];
        }
    }
    GET_UNIQUE_ID((uint8_t*)DongleUniqueId);
    BoundStatus   = BOUND_STATUS_IDLE;
    TxBuf.status  = 0;
    IntervalTimer = CONN_INTERVAL * 1000;
    RFRole_RegisterStatusCbs(&rfCBs);
    RfStatus = RF_STATUS_WAIT;
}

/******************************** endfile @rf ******************************/
