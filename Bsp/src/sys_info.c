/*******************************************************************************
 * @file      sys_info.c
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-08
 *
 * @brief CODE Flash 末尾 4K 扇区的读-改-写访问（配对信息 + IAP 镜像标志）。
 *        扇区擦除会清掉整页，故任何单域更新都必须回写整页保留另一域。
 *******************************************************************************/
#include "sys_info.h"
#include "rf.h" // BOUND_INFO_HEAD

/*********************************************************************
 * @fn      sys_info_write
 *
 * @brief   整页擦除后写入（调用前须完成读-改-写组装）
 *
 * @return  TRUE-成功  FALSE-失败
 */
__HIGH_CODE
static BOOL sys_info_write(const sys_info_t* info)
{
    if(FLASH_ROM_ERASE(SYS_INFO_FLASH_ADDR, 4096) != 0)
    {
        return FALSE;
    }
    if(FLASH_ROM_WRITE(SYS_INFO_FLASH_ADDR, (void*)info, sizeof(sys_info_t)) != 0)
    {
        return FALSE;
    }
    return FLASH_ROM_VERIFY(SYS_INFO_FLASH_ADDR, (void*)info, sizeof(sys_info_t)) == 0;
}

/*********************************************************************
 * @fn      sys_info_read
 *
 * @brief   读取整页信息
 */
__HIGH_CODE
void sys_info_read(sys_info_t* info)
{
    FLASH_ROM_READ(SYS_INFO_FLASH_ADDR, info, sizeof(sys_info_t));
}

/*********************************************************************
 * @fn      sys_info_save_peer
 *
 * @brief   保存配对信息（保留镜像标志）
 *
 * @return  TRUE-成功  FALSE-失败
 */
__HIGH_CODE
BOOL sys_info_save_peer(const uint8_t* peerId)
{
    __attribute__((aligned(4))) sys_info_t info;

    sys_info_read(&info);
    info.head = BOUND_INFO_HEAD;
    for(uint8_t index = 0; index < 8; index++)
    {
        info.peerId[index] = peerId[index];
    }
    return sys_info_write(&info);
}

/*********************************************************************
 * @fn      sys_info_enter_iap
 *
 * @brief   置 IAP 镜像标志并记录升级通道（保留配对信息）
 *
 * @return  TRUE-成功  FALSE-失败
 */
__HIGH_CODE
BOOL sys_info_enter_iap(uint8_t bootMode)
{
    __attribute__((aligned(4))) sys_info_t info;

    sys_info_read(&info);
    if((info.ImageFlag == FLAG_USER_CALL_IAP) && (info.bootMode == bootMode))
    {
        return TRUE;
    }
    info.ImageFlag = FLAG_USER_CALL_IAP;
    info.bootMode  = bootMode;
    return sys_info_write(&info);
}

/*********************************************************************
 * @fn      sys_info_switch_app
 *
 * @brief   置 APP 镜像标志（保留配对信息），仅在值变化时才写 flash
 *
 * @return  TRUE-成功  FALSE-失败
 */
__HIGH_CODE
BOOL sys_info_switch_app(void)
{
    __attribute__((aligned(4))) sys_info_t info;

    sys_info_read(&info);
    if(info.ImageFlag == FLAG_USER_CALL_APP)
    {
        return TRUE;
    }
    info.ImageFlag = FLAG_USER_CALL_APP;
    return sys_info_write(&info);
}

/******************************** endfile @sys *************************/
