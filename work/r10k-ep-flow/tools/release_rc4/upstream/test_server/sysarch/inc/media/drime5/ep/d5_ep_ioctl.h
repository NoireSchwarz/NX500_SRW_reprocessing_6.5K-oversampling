/**
 * @file d5_ep_ioctl.h
 * @brief DRIMe5 EP IOCTL Interface Header File
 * @author TaeWook Nam <tw.nam@samsung.com>
 * 2013 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef D5_EP_IOCTL_H_
#define D5_EP_IOCTL_H_

#include "d5_ep_type.h"

/*
 * EP IOCTL Debug Message Macro
 */

/**< EP IOCTL 코드에서 디버그 메세지 출력 (TRUE: 출력, FALSE: 출력안함) */
/* #define EP_IOCTL_DEBUG_MSG_ON TRUE */
#define EP_IOCTL_ERROR_MSG_ON TRUE

#ifdef EP_IOCTL_DEBUG_MSG_ON
	#define EP_IOCTL_DEBUG_MSG(format, args...) pr_debug(format, ##args)
#else
	#define EP_IOCTL_DEBUG_MSG(format, args...)
#endif

#ifdef EP_IOCTL_ERROR_MSG_ON
	#define EP_IOCTL_ERROR_MSG(format, args...) pr_err(format, ##args)
#else
	#define EP_IOCTL_ERROR_MSG(format, args...)
#endif

#define EP_IOCTL_CMD_MAGIC		'h'

/* UDD *******************************************************/
#define EP_IOCTL_GET_PHYS_REG_INFO	_IOR(EP_IOCTL_CMD_MAGIC, 100, struct ep_reg_info)	/**< EP Sub-block IP들의 레지스터 주소를 가져오는 인터페이스 */

#define EP_IOCTL_CORE_INTR_WAIT					_IOW(EP_IOCTL_CMD_MAGIC, 6, struct ep_k_core_intr_wait_info)	/**< EP의 특정 Core Interrupt 발생시 까지 대기, 주의: Timeout 발생시 음수값(-ETIMEDOUT) 반환 */
#define EP_IOCTL_CORE_INTR_WAIT_QUEUE_INIT		_IOW(EP_IOCTL_CMD_MAGIC, 7, struct ep_k_core_intr_wait_info)	/**< EP_IOCTL_INTR_WAIT 인터페이스에서 사용하는 Wait Queue를 초기화 */
#define EP_IOCTL_ACC_INTR_WAIT					_IOWR(EP_IOCTL_CMD_MAGIC, 8, struct ep_k_acc_intr_wait_info)	/**< EP의 특정 Core Interrupt 발생시 까지 대기, 주의: Timeout 발생시 음수값(-ETIMEDOUT) 반환 */
#define EP_IOCTL_ACC_INTR_WAIT_QUEUE_INIT		_IOW(EP_IOCTL_CMD_MAGIC, 9, struct ep_k_acc_intr_wait_info)	/**< EP_IOCTL_INTR_WAIT 인터페이스에서 사용하는 Wait Queue를 초기화 */
#define EP_IOCTL_DMA_H_INTR_WAIT				_IOW(EP_IOCTL_CMD_MAGIC, 10, struct ep_k_dma_h_intr_wait_info)	/**< EP의 특정 DMAInterrupt 발생시 까지 대기, 주의: Timeout 발생시 음수값(-ETIMEDOUT) 반환 */
#define EP_IOCTL_DMA_H_INTR_WAIT_QUEUE_INIT		_IOW(EP_IOCTL_CMD_MAGIC, 11, struct ep_k_dma_h_intr_wait_info)	/**< EP_IOCTL_DMA_INTR_WAIT 인터페이스에서 사용하는 Wait Queue를 초기화 */
#define EP_IOCTL_DMA_L_INTR_WAIT				_IOW(EP_IOCTL_CMD_MAGIC, 12, struct ep_k_dma_l_intr_wait_info)	/**< EP의 특정 Core Interrupt 발생시 까지 대기, 주의: Timeout 발생시 음수값(-ETIMEDOUT) 반환 */
#define EP_IOCTL_DMA_L_INTR_WAIT_QUEUE_INIT		_IOW(EP_IOCTL_CMD_MAGIC, 13, struct ep_k_dma_l_intr_wait_info)	/**< EP_IOCTL_INTR_WAIT 인터페이스에서 사용하는 Wait Queue를 초기화 */

#define EP_IOCTL_CORE_INTR_ENABLE				_IOW(EP_IOCTL_CMD_MAGIC, 14, enum ep_k_core_int_sig)
#define EP_IOCTL_CORE_INTR_DISABLE				_IOW(EP_IOCTL_CMD_MAGIC, 15, enum ep_k_core_int_sig)
#define EP_IOCTL_ACC_INTR_ENABLE				_IOW(EP_IOCTL_CMD_MAGIC, 16, enum ep_k_acc_int_sig)
#define EP_IOCTL_ACC_INTR_DISABLE				_IOW(EP_IOCTL_CMD_MAGIC, 17, enum ep_k_acc_int_sig)
#define EP_IOCTL_DMA_H_INTR_ENABLE				_IOW(EP_IOCTL_CMD_MAGIC, 18, enum ep_k_dma_h_int_sig)
#define EP_IOCTL_DMA_H_INTR_DISABLE				_IOW(EP_IOCTL_CMD_MAGIC, 19, enum ep_k_dma_h_int_sig)
#define EP_IOCTL_DMA_L_INTR_ENABLE				_IOW(EP_IOCTL_CMD_MAGIC, 20, enum ep_k_dma_l_int_sig)
#define EP_IOCTL_DMA_L_INTR_DISABLE				_IOW(EP_IOCTL_CMD_MAGIC, 21, enum ep_k_dma_l_int_sig)

#define EP_IOCTL_SET_JPEG_MODE					_IOWR(EP_IOCTL_CMD_MAGIC, 22, enum ep_k_jpeg_opmode)

#define EP_IOCTL_UDD_LOCK		_IO(EP_IOCTL_CMD_MAGIC, 40)
#define EP_IOCTL_UDD_UNLOCK		_IO(EP_IOCTL_CMD_MAGIC, 41)

#define EP_IOCTL_SET_CLK_RATE	_IOW(EP_IOCTL_CMD_MAGIC, 50, int)

#endif /* D5_EP_IOCTL_H_ */

