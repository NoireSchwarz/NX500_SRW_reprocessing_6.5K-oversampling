/* SPDX-License-Identifier: AGPL-3.0-only */
struct p13_irq_guard {uint32_t address,words[8];};
static const struct p13_irq_guard p13_irq_guards[2]={
{0x804bb89c,{0xe92d40f0u,0xe3a04a01u,0xe3424082u,0xe59430d8u,0xe24dd00cu,0xe58d3004u,0xe3a03000u,0xe58d3000u}},
{0x804bb8d4,{0xe59d1004u,0xe5963010u,0xe59d2000u,0xe0013003u,0xe0133215u,0xa00000bu,0xe59d3000u,0xe0873103u}}
};
