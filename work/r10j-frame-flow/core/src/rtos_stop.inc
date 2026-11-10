/* SPDX-License-Identifier: AGPL-3.0-only. Use original async stop entry, then
 * original eventflag wrappers with a bounded wait. No infinite Drive wait call.
 * This validates a fresh native completion notification, not a global ISP lease.
 */
typedef int32_t (*call5)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef int32_t (*call8)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
static int32_t event_ref(uint32_t output){return ((call5)0x804dbbf0u)((uint32_t)"NXL38D",(uint32_t)"stop_gate",1,P39_STOP_HOLDER,output);}
static int stop_handle(uint32_t object,uint32_t id){return *ptr(P39_STOP_HOLDER)==object&&*ptr(object)==id;}
static int32_t stop_gate(void){
 volatile uint32_t *s=ptr(ctx.cap+P39_STOP_OFFSET);s[0]=P39_STOP_MAGIC;s[1]=3;s[2]=ctx.nonce;s[3]=1;s[4]=P39_STOP_HOLDER;s[7]=P39_STOP_PATTERN;s[8]=P39_STOP_TIMEOUT;s[29]=ctx.cap+P39_STOP_COMMAND_OFFSET;
 for(unsigned i=9;i<=14;i++)s[i]=0xffffffffu;s[19]=s[23]=s[27]=0xdeadcafeu;
 for(unsigned i=0;i<14;i++)s[32+i]=*ptr(p39_stop_addresses[i]);
 uint32_t object=*ptr(P39_STOP_HOLDER);s[5]=object;
 if(object<0x81000000u||object>=0x93fff000u||(object&3u))return 120;
 uint32_t id=*ptr(object);s[6]=id;if(!id||id>65535)return 120;
 s[9]=(uint32_t)event_ref((uint32_t)(s+16));
 if((int32_t)s[9]||s[19]!=0xdeadcafeu||!stop_handle(object,id))return 120;
 /* T_RFLG is exinf,wtsk,flgptn. Pinned OEM GetCurStatus reads word2; the
  * native `flg` CLI prints word1 as WID and word2 as PTN. No waiting client
  * may have its notification reset or consumed by this experiment. */
 if(s[17])return 121;
 /* Record real controller state BEFORE consuming the old notification.
  * Flag0 cannot use controlLiveviewStop: that entry returns-1 without posting.
  * Use its original lower asynchronous stop command, still requiring a NEW
  * event and all idle checks. Never force the flag or accept an old token. */
 s[46]=*ptr(0x8142aab8u);s[47]=*(volatile unsigned char*)0x8142aae8u;
 if(!(s[46]&1u)||s[47]>1u)return 130;
 uint32_t params=*ptr(0x8142aadcu),queue=*ptr(0x8142aae0u);
 if(params<0x81000000u||params>=0x93fff000u||(params&3u)||queue<0x81000000u||queue>=0x93fff000u||(queue&3u))return 130;
 if(!s[47]){
  if((s[18]&P39_STOP_PATTERN)!=P39_STOP_PATTERN||!idle())return 130;
  /* Refuse special movie/drive cleanup branches rather than bypass them. */
  if((uint32_t)native(0x80071eb4u,params,70,0)==0x00460001u||native(0x80071e3cu,params,309,0)==1)return 130;
 }
 if(*(volatile unsigned char*)0x8142aae8u!=s[47])return 130;
 s[10]=(uint32_t)((call5)0x804dbb08u)((uint32_t)"NXL38D",(uint32_t)"stop_gate",2,P39_STOP_HOLDER,P39_STOP_PATTERN);
 if((int32_t)s[10])return 122;
 s[11]=(uint32_t)event_ref((uint32_t)(s+20));
 if((int32_t)s[11]||s[21]||(s[22]&P39_STOP_PATTERN)||s[23]!=0xdeadcafeu||!stop_handle(object,id))return 122;
 s[3]=2;barrier();
 /* Original controlLiveviewStop checks object+4 length==0, then posts
  * internal native command0x01030002 with payloadNULL,length0. */
 s[62]=s[47]?0x8005b36cu:0x800ce7d4u;barrier();
 s[12]=(uint32_t)native(s[62],s[47]?s[29]:queue,0,0);s[63]=1;barrier();
 if((int32_t)s[12])return 123;s[3]=3;barrier();
 s[13]=(uint32_t)((call8)0x804dba00u)((uint32_t)"NXL38D",(uint32_t)"stop_gate",3,P39_STOP_HOLDER,0,P39_STOP_PATTERN,(uint32_t)(s+28),P39_STOP_TIMEOUT);
 if((int32_t)s[13]||(s[28]&P39_STOP_PATTERN)!=P39_STOP_PATTERN)return 124;
 s[3]=4;s[14]=(uint32_t)event_ref((uint32_t)(s+24));
 s[31]=stop_handle(object,id);
 for(unsigned i=0;i<14;i++)s[48+i]=*ptr(p39_stop_addresses[i]);
 if((int32_t)s[14]||s[25]||(s[26]&P39_STOP_PATTERN)!=P39_STOP_PATTERN||s[27]!=0xdeadcafeu||!s[31])return 125;
 s[3]=5;s[15]=idle();if(!s[15])return 126;
 s[3]=6;barrier();return 0;
}
