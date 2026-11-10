/* SPDX-License-Identifier: AGPL-3.0-only; bounded read-only step snapshots. */
#ifndef NXL_P12_STEPS_H
#define NXL_P12_STEPS_H
#define P12_STEP_MAGIC 0x30325350u
#define P12_STEP_MAX 64u
#define P12_TOP_COUNT 45u
static const uint32_t p12_top_offsets[P12_TOP_COUNT]={0x1000,0x1020,0x1024,0x1048,0x1070,0x1074,0x1078,0x107c,0x1080,0x10c8,0x10cc,0x10d0,0x10d4,0x10d8,0x1100,0x1110,0x1114,0x1118,0x1218,0x1104,0x110c,0x1400,0x1404,0x1700,0x1704,0x1008,0x100c,0x1200,0x1408,0x140c,0x1410,0x1418,0x141c,0x1420,0x1424,0x1428,0x142c,0x1708,0x170c,0x1710,0x1718,0x171c,0x1720,0x1724,0x172c};
struct p12_step {
 uint32_t step,elapsed_ms,top[P12_TOP_COUNT],lut_valid,lut[5],kernel_valid;
 uint32_t linux_mask[5],acc8_queue[3],core5_queue[3],mutex[3],rtos_mask[5];
 uint32_t last_dma_kind,last_dma_va,last_dma_pa,last_ioctl;
 int32_t ioctl_return;uint32_t ioctl_errno,memory_sample[4],acc7_queue[3];
};
struct p12_capture {uint32_t magic,version,count,record_bytes;struct p12_step record[P12_STEP_MAX];uint32_t crc;};
_Static_assert(sizeof(struct p12_step)==344,"fixed P20 diagnostic ABI");
_Static_assert(sizeof(struct p12_capture)==22036,"fixed P20 capture ABI");
#endif
