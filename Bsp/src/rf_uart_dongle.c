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

/******************************************************************************/
/* 头文件包含 */
#include "rf.h"
#include "rf_uart_dongle.h"
#include "usb_uart.h"
#include "led.h"

uint8_t volatile RF_bound_Flag;

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
static uint8_t rf_buf[RF_BUF_LEN];
static struct simple_buf *pRfBuf = NULL;
static struct simple_buf rf_buffer;
uint32_t gBaudRate;

rfTxBuf_t       gTxBuf;
uint32_t        gSysClock;
uint32_t        gIntervalTimer;
uint16_t        gTimeoutMax;
uint16_t        gTimeout;
static uint8_t  gBoundDeviceId[8];
static uint8_t  gPendingDeviceId[8];
static uint32_t gDongleUniqueId[2];
static uint8_t  gPairingPending;

static volatile uint8_t  gRfTxActivityPending;
static volatile uint32_t gRfDataEventTimeMs;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t gRxDataStatus;

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessCrcError( void );
static  void rfProcessTimeout( void );

__HIGH_CODE
static void rf_record_data_event(void)
{
    gRfDataEventTimeMs = SysRunTime_ms;
}

// GAP Service Callbacks
rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessCrcError,
    rfProcessTimeout,
};

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
static void rf_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_buf, sizeof(rf_buf) );
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
uint32_t rf_rand16( uint32_t seed )
{
    static uint32_t holdrand;
    uint64_t tmp;

    holdrand += seed;
    holdrand = holdrand * 1664525 + 1013904223;
    tmp = holdrand;
    tmp = (tmp*0xFFFF)>>32;
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
uint32_t rf_rand_aa( uint16_t rand )
{
    uint32_t aa = rand;

    aa = (aa<<8)|0x6E0000B6;
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
static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
    RFRole_Shut( );
    gBoundStatus = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END); // 关闭中断

    rf_tx_set_sync_word(AA);
    rf_tx_set_frequency(DEF_FREQUENCY);
    rf_rx_set_sync_word(AA);
    rf_rx_set_frequency(DEF_FREQUENCY);
    gRfStatus          = RF_STATUS_WAIT;
    for(uint8_t index = 0; index < 8; index++)
    {
        gPendingDeviceId[index] = 0;
    }
    gPairingPending = 0;
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
static void rf_bound(bound_rsp_t* rsp)
{
    gTimeout           = 0;
    gBoundStatus       = BOUND_STATUS_WAIT;
    gRfDataEventTimeMs = SysRunTime_ms - 20U;
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
static void rfProcessRx( rfPackage_t *pPkt )
{
    rfPackage_t* pPkt_t  = (rfPackage_t*)gTxBuf.TxBuf;
    gRfTxActivityPending = 0;

    if(gBoundStatus == BOUND_STATUS_IDLE)
    {
        if(pPkt->type == PKT_CMD_BOUND_REQ)
        {
            uint16_t     radioSeed = 0;
            bound_req_t* pReq_t             = (bound_req_t*)(pPkt + 1);
            if(pPkt->length == sizeof(bound_req_t) + 2)
            {
                int8_t rssi = *(uint8_t*)((uint8_t*)pPkt + pPkt->length + 2 + 2);

                BOOL requestAccepted = FALSE;
                if(rf_uid_valid(gBoundDeviceId))
                {
                    requestAccepted = rf_uid_equal(pReq_t->deviceId, gBoundDeviceId);
                }
                else if(rf_uid_valid(pReq_t->deviceId) && rssi > -35)
                {
                    requestAccepted = TRUE;
                    gPairingPending = 1;
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        gPendingDeviceId[index] = pReq_t->deviceId[index];
                    }
                }
                if(requestAccepted)
                {
                    bound_rsp_t* pRsp_t = (bound_rsp_t*)&gTxBuf.TxBuf[PKT_HEAD_LEN];

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
                    gRfStatus          = RF_STATUS_TXRSP;
                    gDataSeq           = 0;
                    pPkt_t->type       = PKT_CMD_BOUND_RSP;
                    pPkt_t->length     = PKT_DATA_OFFSET + sizeof(bound_rsp_t);
                    pPkt_t->seq        = gDataSeq;
                    pPkt_t->resv       = 0;
                    pRsp_t->accessaddr = rf_rand_aa(radioSeed);
                    pRsp_t->channel    = radioSeed & 0x3F;
                    pRsp_t->phy        = CONN_PHY_TYPE; // 2M
                    pRsp_t->interval   = CONN_INTERVAL;
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        pRsp_t->dongleId[index] = ((uint8_t*)gDongleUniqueId)[index];
                    }
                    pRsp_t->timeout    = CONN_TIMEOUT;
                    rf_tx_start(pPkt_t, 20);
                    gDataSeq++;
                    gIntervalTimer = pReq_t->interval * 1000;
                    gTimeoutMax    = BOUND_EST_COUNT;
                    return;
                }
                else
                {
                    PRINT(" reject..\n");
                }
                PRINT("rssi=%d \n", rssi);
            }
        }
        gRfStatus = RF_STATUS_WAIT;
    }
    else
    {
        rfRsp_t* pRsp_t = (rfRsp_t*)&gTxBuf.TxBuf[PKT_HEAD_LEN];
        if(pPkt->type == PKT_CMD_GET_STATUS)
        {
            gRfStatus = RF_STATUS_TX;
            if(pPkt->seq == gDataSeq)
            {
                if(gPairingPending && gBoundStatus == BOUND_STATUS_WAIT)
                {
                    if(!rf_save_bound_info(gPendingDeviceId))
                    {
                        PRINT("bound info save failed.\n");
                        for(uint8_t index = 0; index < 8; index++)
                        {
                            gPendingDeviceId[index] = 0;
                        }
                        gPairingPending = 0;
                        gBoundStatus = BOUND_STATUS_IDLE;
                        RF_bound_Flag = 0;
                        rf_tx_set_sync_word(AA);
                        rf_tx_set_frequency(DEF_FREQUENCY);
                        rf_rx_set_sync_word(AA);
                        rf_rx_set_frequency(DEF_FREQUENCY);
                        gRfStatus = RF_STATUS_RX;
                        rf_rx_start(gIntervalTimer);
                        return;
                    }
                    for(uint8_t index = 0; index < 8; index++)
                    {
                        gBoundDeviceId[index] = gPendingDeviceId[index];
                        gPendingDeviceId[index] = 0;
                    }
                    gPairingPending = 0;
                }
                uint8_t     s;
                typeBufSize len = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_CMD_RSP_STATUS;
                if( gBoundStatus < BOUND_STATUS_EST  )
                {
                    // 发送串口波特率
                    pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(Uart0Para);
                    pRsp_t->opcode = OPCODE_BSP;
                    pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                    pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                    pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                    pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                    pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else
                {
                    s = USB_RxQuery( pRsp_t->other.rspData, &len );
                    if( s == 0 )
                    {
                        pPkt_t->length       = PKT_DATA_OFFSET + 1 + len;
                        pRsp_t->opcode       = OPCODE_DATA;
                        pPkt_t->seq          = gDataSeq;
                        pPkt_t->resv         = 0;
                        gRfTxActivityPending = 1;
                    }
                    else if( s == 0x80 )
                    {
                        // 发送串口波特率
                        pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(Uart0Para);
                        pRsp_t->opcode = OPCODE_BSP;
                        pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                        pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                        pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                        pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                        pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                    else
                    {
                        pPkt_t->length = PKT_DATA_OFFSET;
                        pRsp_t->opcode = OPCODE_ACK;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                }
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
            if( pRsp_t->opcode == OPCODE_BSP )
            {
                PRINT("uart param.\n");
            }
        }
        else if( pPkt->type == PKT_DATA_FLAG )
        {

            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                rf_record_data_event();
                typeBufSize len = pPkt->length;
                len -= PKT_DATA_OFFSET;
                write_buf( pRfBuf, (pPkt+1), &len );
                gRxDataStatus = DATA_STATUS_RCV;
                // 如果接收缓存满，则会丢数据
                len = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_DATA_RSP_ACK;
                
                if( gBoundStatus == BOUND_STATUS_EST && !USB_RxQuery( pRsp_t->other.rspData, &len ) )
                {
                    pPkt_t->length       = PKT_DATA_OFFSET + 1 + len;
                    pRsp_t->opcode       = OPCODE_DATA;
                    pPkt_t->seq          = gDataSeq;
                    pPkt_t->resv         = 0;
                    gRfTxActivityPending = 1;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                gDataSeq++;
            }
            else
            {
                // 重传
            }
            rf_tx_start( pPkt_t, 20 );
            
        }
        else
        {
            PRINT("error data.\n");
            if( ++gTimeout > gTimeoutMax )
            {
                gRxDataStatus = DATA_STATUS_TIMEOUT;
                PRINT("connect timeout.\n");
            }
            else
            {
                gRfStatus = RF_STATUS_WAIT;
            }
            return ;
        }
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            // 连接确认，启动连接通信超时
            gBoundStatus = BOUND_STATUS_EST;
            gIntervalTimer = CONN_INTERVAL*1000;
            gTimeoutMax = (CONN_TIMEOUT*10/CONN_INTERVAL);
       }
       gTimeout = 0;
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
    if(gRfTxActivityPending)
    {
        rf_record_data_event();
        gRfTxActivityPending = 0;
    }
    if(gRfStatus == RF_STATUS_TXRSP)
    {
        rf_bound((bound_rsp_t*)&gTxBuf.TxBuf[PKT_HEAD_LEN]);
    }
    gRfStatus = RF_STATUS_RX;
    rf_rx_start(gIntervalTimer);
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
    gRfStatus = RF_STATUS_WAIT;
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
    gRfTxActivityPending = 0;
    if(gBoundStatus && ++gTimeout > gTimeoutMax)
    {
        gRxDataStatus = DATA_STATUS_TIMEOUT;
        PRINT("connect timeout.\n");
    }
    else
    {
        gRfStatus = RF_STATUS_WAIT;
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

    if((uint32_t)(now - gRfDataEventTimeMs) < 20U)
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
    if(gRxDataStatus == DATA_STATUS_RCV)
    {
        if(read_buf(pRfBuf, buf, len) == 0)
        {
            PFIC_DisableIRQ(BLEL_IRQn);
            gRxDataStatus = DATA_STATUS_START;
            PFIC_EnableIRQ(BLEL_IRQn);
        }
    }
    else if(gRxDataStatus == DATA_STATUS_TIMEOUT)
    {
        *len = 0;
        rf_disconnect();
    }
    else
    {
        *len = 0;
        if(gRfStatus == RF_STATUS_WAIT)
        {
            gRfStatus = RF_STATUS_RX;
            rf_rx_start(gIntervalTimer);
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
    gRxDataStatus = DATA_STATUS_IDLE;
    rf_buffer_create(&pRfBuf);
    gDataSeq           = 0;
    for(uint8_t index = 0; index < 8; index++)
    {
        gBoundDeviceId[index] = 0;
        gPendingDeviceId[index] = 0;
    }
    gPairingPending = 0;
    FLASH_ROM_READ(BOUND_INFO_FLASH_ADDR, &info, sizeof(info));
    if(info.head == BOUND_INFO_HEAD && rf_uid_valid(info.peerId))
    {
        for(uint8_t index = 0; index < 8; index++)
        {
            gBoundDeviceId[index] = info.peerId[index];
        }
    }
    GET_UNIQUE_ID((uint8_t*)gDongleUniqueId);
    gBoundStatus   = BOUND_STATUS_IDLE;
    gTxBuf.status  = 0;
    gIntervalTimer = CONN_INTERVAL * 1000;
    gSysClock      = GetSysClock();
    RFRole_RegisterStatusCbs(&rfCBs);
    gRfStatus = RF_STATUS_WAIT;
}

/******************************** endfile @rf ******************************/
