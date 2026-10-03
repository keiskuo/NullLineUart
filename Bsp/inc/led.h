/*******************************************************************************
 * @file      led.h
 * @author    keiskuo (ke.l@foxmail.com)
 * @version   1.0.0
 * @date      2026-10-03
 * 
 * @copyright Copyright (c) 2026
 * @brief     LED控制模块头文件
 * 
 ******************************************************************************/
#pragma once

#include "CH57x_common.h"

void led_init(void);
void led_toggle(uint16_t interval);