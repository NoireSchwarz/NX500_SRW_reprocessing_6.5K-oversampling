/* SPDX-License-Identifier: AGPL-3.0-only. R11k Still transfer diagnostics.
 * Gamma only: source WB/CC/LDC/LUT stay fixed; no BT.2020 encoding claim.
 * Exact profiles prevent arbitrary positive-toe or reduced-white requests. */
#ifndef NXL_GAMMA_TRANSFER_PROFILE_H
#define NXL_GAMMA_TRANSFER_PROFILE_H
#define GT_LINEAR 3u
#define GT_LOG_TRANSFER 4u
#define GT_LOG_MIDGRAY 5u
#define GT_MODE_MAX GT_LOG_MIDGRAY
static const uint32_t gt_transfer_x[19]={0u,95u,209u,296u,393u,516u,696u,953u,1307u,1804u,2491u,2949u,3346u,4578u,5995u,7991u,10461u,13464u,16383u};
static const uint32_t gt_transfer_log_y[19]={126u,164u,232u,270u,301u,331u,363u,398u,433u,468u,504u,522u,536u,571u,601u,632u,662u,690u,712u};
/* R11n: standard Samsung encoder with a calibrated digital input scale. */
static const uint32_t gt_midgray_x[19]={0u,24u,48u,65u,95u,148u,209u,296u,516u,953u,1804u,2965u,4578u,5995u,7991u,10461u,13464u,15000u,16383u};
static const uint32_t gt_midgray_y[19]={130u,152u,183u,213u,255u,305u,344u,383u,447u,517u,590u,647u,697u,728u,761u,792u,820u,833u,843u};
static inline uint32_t gt_profile_x(unsigned mode,unsigned i){return mode==GT_LOG_MIDGRAY?gt_midgray_x[i]:gt_transfer_x[i];}
static inline uint32_t gt_transfer_y(unsigned mode,unsigned i){return mode==GT_LOG_MIDGRAY?gt_midgray_y[i]:mode==GT_LINEAR?(gt_transfer_x[i]*1023u+8191u)/16383u:gt_transfer_log_y[i];}
#endif
