/* SPDX-License-Identifier: AGPL-3.0-only. Separate monotonic timing; no wire ABI. */
#ifndef NX_R6_TIMING_H
#define NX_R6_TIMING_H
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
static FILE *volatile r6_timing_stream;
static uint64_t r6_timing_origin;
static uint64_t r6_timing_ns(void){
#if defined(__arm__)
 /* The original camera kernel exposes the ARM time32 syscall. New musl's
  * public timespec uses time64, unlike the original worker's glibc. Never
  * pass either libc structure directly to this shared kernel ABI. */
 struct {int32_t tv_sec,tv_nsec;} t;
 _Static_assert(sizeof t==8,"original ARM kernel timespec ABI");
#if defined(SYS_clock_gettime32)
 if(syscall(SYS_clock_gettime32,CLOCK_MONOTONIC,&t))return 0;
#elif defined(SYS_clock_gettime)
 if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;
#else
#error Original ARM clock_gettime syscall number is required
#endif
#else
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
#endif
 if(t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return 0;
 return (uint64_t)t.tv_sec*1000000000ULL+(uint32_t)t.tv_nsec;}
static void r6_timing_set(FILE *f){r6_timing_stream=f;r6_timing_origin=r6_timing_ns();}
static void r6_timing(const char *stage,const char *edge,uint64_t amount,int ok){FILE *stream=r6_timing_stream;if(!stream)return;uint64_t now=r6_timing_ns();unsigned clock_available=r6_timing_origin&&now>=r6_timing_origin;uint64_t relative=clock_available?now-r6_timing_origin:0;fprintf(stream,"R6_TIMING phase=%s edge=%s monotonic_ns=%llu relative_ns=%llu clock_available=%u amount=%llu ok=%d\n",stage,edge,(unsigned long long)now,(unsigned long long)relative,clock_available,(unsigned long long)amount,ok);fflush(stream);}
#endif
