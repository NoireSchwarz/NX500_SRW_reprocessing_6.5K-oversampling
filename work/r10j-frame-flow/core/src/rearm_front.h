/* SPDX-License-Identifier: AGPL-3.0-only.
 * Resource retirement and permission to render are separate observations.
 * The normal renderer still requires every original front_readiness field.
 */
#ifndef NX51_REARM_FRONT_H
#define NX51_REARM_FRONT_H
#include "front_readiness.h"

static int r7_front_retired(const struct fr_snapshot *s,unsigned *needs_initialization){
 if(needs_initialization)*needs_initialization=0;
 if(!s||s->read_mask!=0xffffu||!s->stable_pair)return 0;
 for(unsigned i=0;i<FR_FIELDS;i++)if(!s->field[i].readable)return 0;
 if(!s->failure_mask){
  for(unsigned i=0;i<FR_FIELDS;i++)if(!s->field[i].passed)return 0;
  return 1;
 }
 /* On the supplied rebooted-camera snapshots the Still task, counters,
  * original objects and EP child are all idle; only the owner-ready byte
  * is 0. That byte must become 1 before rendering, but it is not evidence
  * of a reference to the previous allocation. Keep all other predicates. */
 if(s->failure_mask!=(1u<<9)||(s->field[9].value&255u)||s->field[9].passed)return 0;
 for(unsigned i=0;i<FR_FIELDS;i++)if(i!=9&&!s->field[i].passed)return 0;
 if(needs_initialization)*needs_initialization=1;
 return 1;
}
#endif
