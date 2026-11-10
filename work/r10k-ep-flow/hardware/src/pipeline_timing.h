/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_PIPELINE_TIMING_H
#define NXL_PIPELINE_TIMING_H
#define P7_WORKER_MS 300000u
#define P7_CONTROLLER_SECONDS 420u
#define P7_WATCHDOG_MS 435000u
_Static_assert(P7_WORKER_MS+30000u<P7_CONTROLLER_SECONDS*1000u,"completion and release margin");
_Static_assert(P7_CONTROLLER_SECONDS*1000u+10000u<P7_WATCHDOG_MS,"watchdog follows controller budget");
#endif
