/**
 * @file d5_ep_type.h
 * @brief DRIMe5 EP Register Define Header File
 * @author TaeWook Nam <tw.nam@samsung.com>
 * 2013 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef D5_EP_TYPES_H_
#define D5_EP_TYPES_H_

enum ep_dd_onoff {
	EP_DD_OFF,		/**< 동작  중지 및 설정 OFF */
	EP_DD_ON		/**< 동작  중지 및 설정 ON*/
};


enum ep_k_core_int_sig {
	EP_K_INT_CORE_PATH_CFG_WDMA_INT = 0,

	EP_K_INT_CORE_RDXI0_EOF = 1,
	EP_K_INT_CORE_RDXI1_EOF,
	EP_K_INT_CORE_RDXI2_EOF,
	EP_K_INT_CORE_RDXI3_EOF,

	EP_K_INT_CORE_WDXI0_EOF = 5,
	EP_K_INT_CORE_WDXI1_EOF,
	EP_K_INT_CORE_WDXI2_EOF,
	EP_K_INT_CORE_WDXI3_EOF,
	EP_K_INT_CORE_WDXI4_EOF,

	EP_K_INT_CORE_LDC_EOF = 10,
	EP_K_INT_CORE_MC_M2M_EOF,
	EP_K_INT_CORE_MC_OTF_EOF,
	EP_K_INT_CORE_RSZ_OUT0_EOF,
	EP_K_INT_CORE_RSZ_OUT1_EOF,
	EP_K_INT_CORE_RSZ_OUT2_EOF,
	EP_K_INT_CORE_RSZ_OUT3_EOF,
	EP_K_INT_CORE_RSZ_OUT4_EOF,

	EP_K_INT_CORE_LDC_EOT = 18,
	EP_K_INT_CORE_MC_M2M_EOT,
	EP_K_INT_CORE_MC_OTF_EOT,
	EP_K_INT_CORE_RSZ_OUT0_EOT,
	EP_K_INT_CORE_RSZ_OUT1_EOT,
	EP_K_INT_CORE_RSZ_OUT2_EOT,
	EP_K_INT_CORE_RSZ_OUT3_EOT,
	EP_K_INT_CORE_RSZ_OUT4_EOT,

	EP_K_INT_CORE_LDC_ERR_H_SIZE_OVER = 26,
	EP_K_INT_CORE_LDC_ERR_V_SIZE_OVER,

	EP_K_INT_CORE_MAX_NUM,
};

/* d5_ep DMA interrupt */
enum ep_k_dma_h_int_sig {
	EP_K_INT_DMA_H_WDMA5_BLAST = 0,
	EP_K_INT_DMA_H_WDMA6_BLAST,
	EP_K_INT_DMA_H_WDMA7_BLAST,
	EP_K_INT_DMA_H_WDMA8_BLAST,
	EP_K_INT_DMA_H_WDMA9_BLAST,

	EP_K_INT_DMA_H_RDXI0_EOT = 5,
	EP_K_INT_DMA_H_RDXI0_PAUSE,
	EP_K_INT_DMA_H_RDXI1_EOT,
	EP_K_INT_DMA_H_RDXI1_PAUSE,
	EP_K_INT_DMA_H_RDXI2_EOT,
	EP_K_INT_DMA_H_RDXI2_PAUSE,
	EP_K_INT_DMA_H_RDXI3_EOT,
	EP_K_INT_DMA_H_RDXI3_PAUSE,

	EP_K_INT_DMA_H_WDXI0_EOT = 13,
	EP_K_INT_DMA_H_WDXI0_PAUSE,
	EP_K_INT_DMA_H_WDXI1_EOT,
	EP_K_INT_DMA_H_WDXI1_PAUSE,
	EP_K_INT_DMA_H_WDXI2_EOT,
	EP_K_INT_DMA_H_WDXI2_PAUSE,
	EP_K_INT_DMA_H_WDXI3_EOT,
	EP_K_INT_DMA_H_WDXI3_PAUSE,
	EP_K_INT_DMA_H_WDXI4_EOT,
	EP_K_INT_DMA_H_WDXI4_PAUSE,

	EP_K_INT_DMA_H_TDMA_ERR = 23,
	EP_K_INT_DMA_H_TDMA_END,

	EP_K_INT_DMA_H_MAX_NUM,
};

/* d5_ep DMA interrupt */
enum ep_k_dma_l_int_sig {
	EP_K_INT_DMA_L_RDMA0_FSM_WAITING_ERR = 0,
	EP_K_INT_DMA_L_RDMA1_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_RDMA2_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_RDMA3_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_WDMA0_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_WDMA1_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_WDMA2_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_WDMA3_FSM_WAITING_ERR,
	EP_K_INT_DMA_L_WDMA4_FSM_WAITING_ERR,

	EP_K_INT_DMA_L_RDXI0_ERR = 9,
	EP_K_INT_DMA_L_RDXI1_ERR,
	EP_K_INT_DMA_L_RDXI2_ERR,
	EP_K_INT_DMA_L_RDXI3_ERR,

	EP_K_INT_DMA_L_WDXI0_ERR = 13,
	EP_K_INT_DMA_L_WDXI1_ERR,
	EP_K_INT_DMA_L_WDXI2_ERR,
	EP_K_INT_DMA_L_WDXI3_ERR,
	EP_K_INT_DMA_L_WDXI4_ERR,

	EP_K_INT_DMA_L_RDMA4_ERR = 18,
	EP_K_INT_DMA_L_RDMA5_ERR,
	EP_K_INT_DMA_L_RDMA6_ERR,
	EP_K_INT_DMA_L_RDMA7_ERR,
	EP_K_INT_DMA_L_RDMA8_ERR,
	EP_K_INT_DMA_L_RDMA9_ERR,

	EP_K_INT_DMA_L_WDMA5_ERR = 24,
	EP_K_INT_DMA_L_WDMA6_ERR,
	EP_K_INT_DMA_L_WDMA7_ERR,
	EP_K_INT_DMA_L_WDMA8_ERR,
	EP_K_INT_DMA_L_WDMA9_ERR,

	EP_K_INT_DMA_L_MAX_NUM,
};

enum ep_k_jpeg_opmode {
	EP_K_JPEG_OPMODE_ENC = 0,
	EP_K_JPEG_OPMODE_DEC,

	EP_K_JPEG_OPMODE_MAX_NUM,
};

/* d5_ep JPEG interrupt */
enum ep_k_acc_int_sig {
	EP_K_INT_ACC_JPEGIF_DEC = 0,
	EP_K_INT_ACC_JPEGIF_ENC,
	EP_K_INT_ACC_JPEG_ERR,
	EP_K_INT_ACC_JPEG_DEND,
	EP_K_INT_ACC_JPEG_MRKDET,
	EP_K_INT_ACC_JPEG_EEND,
	EP_K_INT_ACC_JPEG_MRKSET,

	EP_K_INT_ACC_3DLUT_RD_FINISH = 7,
	EP_K_INT_ACC_3DLUT_WR_FINISH,
	EP_K_INT_ACC_LVR_GRP_FRAME,
	EP_K_INT_ACC_LVR_VID_FRAME,
	EP_K_INT_ACC_FD_FRAME,
	EP_K_INT_ACC_FD_OPERATION,
	EP_K_INT_ACC_BITBLT,
	EP_K_INT_ACC_LVR_VID_LINE,
	EP_K_INT_ACC_FD_TILE,

	EP_K_INT_ACC_WDMA5_BLAST = 16,
	EP_K_INT_ACC_WDMA6_BLAST,
	EP_K_INT_ACC_WDMA7_BLAST,
	EP_K_INT_ACC_WDMA8_BLAST,
	EP_K_INT_ACC_WDMA9_BLAST,

	EP_K_INT_ACC_MAX_NUM,
};

struct ep_reg_phys_info {
	unsigned int reg_start_addr; /**< Register physical start address */
	unsigned int reg_size;		 /**< Register size */
};

struct ep_reg_info {
	struct ep_reg_phys_info reg_base_top;
	struct ep_reg_phys_info reg_base_ldc;
	struct ep_reg_phys_info reg_base_mc;
	struct ep_reg_phys_info reg_base_rsz;
	struct ep_reg_phys_info reg_base_lvr;
	struct ep_reg_phys_info reg_base_bblt;
	struct ep_reg_phys_info reg_base_fd;
	struct ep_reg_phys_info reg_base_jpeg;
	struct ep_reg_phys_info reg_base_3dlut;
	struct ep_reg_phys_info reg_base_nog;
};


struct ep_k_core_intr_wait_info {
	int timeout_ms; /**< 타임아웃 시간 (millisecond 단위) */
	enum ep_k_core_int_sig intr; /**< 인터럽트 종류 */
};

struct ep_k_acc_intr_wait_info {
	int timeout_ms; /**< 타임아웃 시간 (millisecond 단위) */
	unsigned int jpeg_err;
	enum ep_k_acc_int_sig intr; /**< 인터럽트 종류 */
};

struct ep_k_dma_l_intr_wait_info {
	int timeout_ms; /**< 타임아웃 시간 (millisecond 단위) */
	enum ep_k_dma_l_int_sig intr; /**< 인터럽트 종류 */
};

struct ep_k_dma_h_intr_wait_info {
	int timeout_ms; /**< 타임아웃 시간 (millisecond 단위) */
	enum ep_k_dma_h_int_sig intr; /**< 인터럽트 종류 */
};

#endif

