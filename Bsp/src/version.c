/*******************************************************************************
 * @file      version.c
 * @brief     固件版本信息字符串。全局符号 gFwVersionStr 经链接选项
 *            -Wl,-u,gFwVersionStr 强制保留，Release/Boot 二进制内也可
 *            用 strings/搜索工具直接查到版本，不依赖 DEBUG 打印。
 *******************************************************************************/
#include "version.h"

const char gFwVersionStr[] = FW_VERSION_FULL;

/******************************** endfile @version ************************/