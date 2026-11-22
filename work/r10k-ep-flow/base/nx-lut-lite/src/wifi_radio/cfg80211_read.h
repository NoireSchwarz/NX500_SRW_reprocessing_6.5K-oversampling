/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only Generic Netlink queries. No SET, scan, vendor or transmit request.
 * Use the PHY selected by uap0's interface identity, not an arbitrary wiphy.
 */
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
struct cfg_attr{const unsigned char*p;size_t n;unsigned type;};
struct cfg_cursor{const unsigned char*p;size_t n;};
static int cfg_next(struct cfg_cursor*c,struct cfg_attr*a){if(!c->n)return 0;if(c->n<4)return-1;uint16_t h[2];memcpy(h,c->p,4);if(h[0]<4||h[0]>c->n)return-1;size_t step=(h[0]+3u)&~3u;if(step>c->n){if(h[0]!=c->n)return-1;step=c->n;}a->type=h[1]&0x3fffu;a->p=c->p+4;a->n=h[0]-4;c->p+=step;c->n-=step;return 1;}
static int cfg_u32(const struct cfg_attr*a,uint32_t*v){if(a->n!=4)return 0;memcpy(v,a->p,4);return 1;}
struct cfg_seen{uint32_t family,ifindex,wiphy;int have_family,have_if,have_phy,have_bands,have_reg;unsigned present,allowed,vht,count;};
static int cfg_family(const unsigned char*p,size_t n,struct cfg_seen*s){struct cfg_cursor c={p,n};struct cfg_attr a;int r;while((r=cfg_next(&c,&a))>0)if(a.type==CTRL_ATTR_FAMILY_ID){uint16_t v;if(s->have_family||a.n!=2)return 0;memcpy(&v,a.p,2);if(v<GENL_MIN_ID)return 0;s->family=v;s->have_family=1;}return r==0&&s->have_family;}
static int cfg_iface(const unsigned char*p,size_t n,struct cfg_seen*s){struct cfg_cursor c={p,n};struct cfg_attr a;int r,have_index=0;while((r=cfg_next(&c,&a))>0){uint32_t v;if(a.type==NL80211_ATTR_IFINDEX){if(have_index||!cfg_u32(&a,&v)||v!=s->ifindex)return 0;have_index=1;}else if(a.type==NL80211_ATTR_WIPHY){if(s->have_if||!cfg_u32(&a,&v))return 0;s->wiphy=v;s->have_if=1;}else if(a.type==NL80211_ATTR_IFNAME){if(a.n!=5||memcmp(a.p,"uap0\0",5))return 0;}}return r==0&&have_index&&s->have_if;}
static int cfg_freqs(const struct cfg_attr*list,unsigned*present,unsigned*allowed,unsigned*count){struct cfg_cursor listc={list->p,list->n};struct cfg_attr item;int r;
 while((r=cfg_next(&listc,&item))>0){struct cfg_cursor c={item.p,item.n};struct cfg_attr a;uint32_t mhz=0;int q,have=0,disabled=0,no_ir=0,no_ibss=0,radar=0,no80=0;
  while((q=cfg_next(&c,&a))>0){if(a.type==NL80211_FREQUENCY_ATTR_FREQ){if(have||!cfg_u32(&a,&mhz))return 0;have=1;}
   else if(a.type==NL80211_FREQUENCY_ATTR_DISABLED||a.type==NL80211_FREQUENCY_ATTR_NO_IR||a.type==4||a.type==NL80211_FREQUENCY_ATTR_RADAR||a.type==NL80211_FREQUENCY_ATTR_NO_80MHZ){if(a.n)return 0;if(a.type==NL80211_FREQUENCY_ATTR_DISABLED)disabled=1;else if(a.type==NL80211_FREQUENCY_ATTR_RADAR)radar=1;else if(a.type==NL80211_FREQUENCY_ATTR_NO_80MHZ)no80=1;else if(a.type==4)no_ibss=1;else no_ir=1;}}
  if(q<0||!have||mhz<2300||mhz>7125||++*count>512)return 0;char line[180];snprintf(line,sizeof line,"CFG_FREQUENCY mhz=%u disabled=%d no_ir=%d no_ibss=%d radar=%d no80=%d",mhz,disabled,no_ir,no_ibss,radar,no80);note(line);
  if(mhz==5220u||mhz==5240u){unsigned bit=1u<<((mhz-5220)/20);if(*present&bit)return 0;*present|=bit;if(!disabled&&!no_ir&&!no_ibss&&!radar)*allowed|=bit;}
 }return r==0;
}
static int cfg_bands(const struct cfg_attr*bands,struct cfg_seen*s){struct cfg_cursor c={bands->p,bands->n};struct cfg_attr band;int r;unsigned bands_count=0;
 while((r=cfg_next(&c,&band))>0){if(++bands_count>8)return 0;struct cfg_cursor b={band.p,band.n};struct cfg_attr a,freq={0};int q,have_freq=0,vht=0;
  while((q=cfg_next(&b,&a))>0){if(a.type==NL80211_BAND_ATTR_FREQS){if(have_freq)return 0;freq=a;have_freq=1;}else if(a.type==NL80211_BAND_ATTR_VHT_CAPA){if(vht||a.n!=4)return 0;vht=1;}}
  if(q<0||!have_freq)return 0;unsigned present=0,allowed=0;if(!cfg_freqs(&freq,&present,&allowed,&s->count)||(s->present&present))return 0;s->present|=present;s->allowed|=allowed;if(vht)s->vht|=present;
 }return r==0&&bands_count>0;
}
static int cfg_phy(const unsigned char*p,size_t n,struct cfg_seen*s){struct cfg_cursor c={p,n};struct cfg_attr a,bands={0};int r,have=0;uint32_t phy=0;
 while((r=cfg_next(&c,&a))>0){if(a.type==NL80211_ATTR_WIPHY){if(s->have_phy||!cfg_u32(&a,&phy)||phy!=s->wiphy)return 0;s->have_phy=1;}else if(a.type==NL80211_ATTR_WIPHY_BANDS){if(have)return 0;bands=a;have=1;}}
 if(r<0||!s->have_phy||!have||!cfg_bands(&bands,s))return 0;s->have_bands=1;char line[180];snprintf(line,sizeof line,"CFG_PHY ifindex=%u wiphy=%u present=%u allowed=%u vht=%u frequencies=%u",s->ifindex,s->wiphy,s->present,s->allowed,s->vht,s->count);note(line);return 1;
}
/* The pinned Linux 3.5 serializer has HT attrs 3..6 and no VHT attrs,
 * also in NX1. Absence cannot be an 11ac rejection. The DE rules and actual
 * enabled channels remain mandatory. No SET_REG or custom regulatory IOCTL. */
static int cfg_reg(const unsigned char*p,size_t n,struct cfg_seen*s){
 struct cfg_cursor c={p,n};struct cfg_attr a,rules={0};int r,alpha=0,have=0;
 while((r=cfg_next(&c,&a))>0){if(a.type==NL80211_ATTR_REG_ALPHA2){if(alpha||a.n!=3||memcmp(a.p,"CN\0",3))return 0;alpha=1;}else if(a.type==NL80211_ATTR_REG_RULES){if(have)return 0;rules=a;have=1;}}
 if(r<0||!alpha||!have)return 0;struct cfg_cursor list={rules.p,rules.n};struct cfg_attr item;unsigned count=0,low=0,base=0;
 while((r=cfg_next(&list,&item))>0){if(++count>32)return 0;struct cfg_cursor rc={item.p,item.n};struct cfg_attr v;uint32_t words[6]={0};unsigned seen=0;int q;
  while((q=cfg_next(&rc,&v))>0){if(v.type>=1&&v.type<=6){unsigned bit=1u<<(v.type-1);if((seen&bit)||!cfg_u32(&v,&words[v.type-1]))return 0;seen|=bit;}}
  if(q<0||seen!=63)return 0;uint32_t flags=words[0],start=words[1],end=words[2],bw=words[3],gain=words[4],eirp=words[5];
  char line[220];snprintf(line,sizeof line,"CFG_REG_RULE start_khz=%u end_khz=%u bandwidth_khz=%u flags=%u ant_gain_raw=%u eirp_raw=%u",start,end,bw,flags,gain,eirp);note(line);
  if(start==2402000&&end==2482000&&bw==40000&&flags==0&&gain==2000&&eirp==0)base++;
  else if(start==5150000&&end==5250000&&bw==80000&&flags==8&&gain==10000&&eirp==0)low++;
  else return 0;
 }
 if(r<0||count!=2||base!=1||low!=1)return 0;s->have_reg=1;note("CFG_CN_LOW_RULE_CONFIRMED indoor_only=1 no_other_5GHz_band=1");return 1;
}
static int cfg_reply(int f,uint32_t seq,unsigned family,unsigned command,int kind,struct cfg_seen*s,unsigned long long end){
 unsigned char*b=malloc(65536);if(!b)return 0;int ok=0;
 while(monotime()<end){struct pollfd p={f,POLLIN,0};int timeout=(int)(end-monotime());if(timeout<0)break;int r=poll(&p,1,timeout);if(r<0&&errno==EINTR)continue;if(r<=0||!(p.revents&POLLIN))break;
  struct sockaddr_nl from;struct iovec iov={b,65536};struct msghdr msg;memset(&from,0,sizeof from);memset(&msg,0,sizeof msg);msg.msg_name=&from;msg.msg_namelen=sizeof from;msg.msg_iov=&iov;msg.msg_iovlen=1;ssize_t n=recvmsg(f,&msg,MSG_DONTWAIT);
  if(n<0&&errno==EINTR)continue;if(n<16||msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC)||msg.msg_namelen!=sizeof from||from.nl_family!=AF_NETLINK||from.nl_pid)break;
  size_t at=0;int bad=0,found=0;
  while(at<(size_t)n){if((size_t)n-at<sizeof(struct nlmsghdr)){bad=1;break;}struct nlmsghdr h;memcpy(&h,b+at,sizeof h);if(h.nlmsg_len<16||h.nlmsg_len>(size_t)n-at||h.nlmsg_seq!=seq||(h.nlmsg_pid&&h.nlmsg_pid!=(uint32_t)getpid())||(h.nlmsg_flags&(NLM_F_MULTI|NLM_F_DUMP_INTR))){bad=1;break;}
   if(h.nlmsg_type==NLMSG_ERROR){int err;if(h.nlmsg_len<20){bad=1;break;}memcpy(&err,b+at+16,4);char line[100];snprintf(line,sizeof line,"CFG_NETLINK_ERROR step=%d code=%d",kind,err);note(line);if(err){bad=1;break;}}
   else {if(found||h.nlmsg_type!=family||h.nlmsg_len<20||b[at+16]!=command){bad=1;break;}const unsigned char*payload=b+at+20;size_t bytes=h.nlmsg_len-20;found=kind==0?cfg_family(payload,bytes,s):kind==1?cfg_iface(payload,bytes,s):kind==2?cfg_phy(payload,bytes,s):kind==3?cfg_reg(payload,bytes,s):0;if(!found){if(kind==3){char line[610];size_t used=(size_t)snprintf(line,sizeof line,"CFG_REG_PARSE_FAILED bytes=%zu raw_head_hex=",bytes);for(size_t j=0;j<bytes&&j<256&&used+3<sizeof line;j++)used+=(size_t)snprintf(line+used,sizeof line-used,"%02x",payload[j]);note(line);}bad=1;break;}}
   size_t step=(h.nlmsg_len+3u)&~3u;if(step>(size_t)n-at){if(h.nlmsg_len!=(size_t)n-at){bad=1;break;}step=h.nlmsg_len;}at+=step;
  }if(!bad&&found)ok=1;break;
 }free(b);return ok;
}
static int cfg_request(int f,uint32_t seq,unsigned family,unsigned command,unsigned attr,const void*data,size_t n){
 unsigned char b[128];if(n>100)return 0;memset(b,0,sizeof b);struct nlmsghdr h;struct genlmsghdr g;memset(&h,0,sizeof h);memset(&g,0,sizeof g);h.nlmsg_len=(uint32_t)(20+((4+n+3)&~3u));h.nlmsg_type=(uint16_t)family;h.nlmsg_flags=NLM_F_REQUEST;h.nlmsg_seq=seq;h.nlmsg_pid=(uint32_t)getpid();g.cmd=(uint8_t)command;g.version=1;memcpy(b,&h,16);memcpy(b+16,&g,4);uint16_t a[2]={(uint16_t)(4+n),(uint16_t)attr};memcpy(b+20,a,4);memcpy(b+24,data,n);struct sockaddr_nl to;memset(&to,0,sizeof to);to.nl_family=AF_NETLINK;return sendto(f,b,h.nlmsg_len,MSG_DONTWAIT,(struct sockaddr*)&to,sizeof to)==(ssize_t)h.nlmsg_len;
}
static int cfg_reg_request(int f,uint32_t seq,unsigned family){
 unsigned char b[20]={0};struct nlmsghdr h;struct genlmsghdr g;memset(&h,0,sizeof h);memset(&g,0,sizeof g);h.nlmsg_len=20;h.nlmsg_type=(uint16_t)family;h.nlmsg_flags=NLM_F_REQUEST;h.nlmsg_seq=seq;h.nlmsg_pid=(uint32_t)getpid();g.cmd=NL80211_CMD_GET_REG;g.version=1;memcpy(b,&h,16);memcpy(b+16,&g,4);struct sockaddr_nl to;memset(&to,0,sizeof to);to.nl_family=AF_NETLINK;return sendto(f,b,sizeof b,MSG_DONTWAIT,(struct sockaddr*)&to,sizeof to)==sizeof b;
}
static int cfg80211_ready(void){
 struct cfg_seen s;memset(&s,0,sizeof s);int ifd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(ifd<0)return failed("CFG_INTERFACE_SOCKET_FAILED");struct ifreq ir;memset(&ir,0,sizeof ir);strcpy(ir.ifr_name,"uap0");int ok=!ioctl(ifd,SIOCGIFINDEX,&ir);close(ifd);if(!ok||ir.ifr_ifindex<=0)return failed("CFG_INTERFACE_INDEX_FAILED");s.ifindex=(uint32_t)ir.ifr_ifindex;
 int f=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_GENERIC);if(f<0)return failed("CFG_NETLINK_UNAVAILABLE");struct sockaddr_nl own;memset(&own,0,sizeof own);own.nl_family=AF_NETLINK;own.nl_pid=(uint32_t)getpid();if(bind(f,(struct sockaddr*)&own,sizeof own)){close(f);return failed("CFG_NETLINK_BIND_FAILED");}
 uint32_t seq=((uint32_t)getpid()<<4)|1u;unsigned long long end=monotime()+2500;
 ok=cfg_request(f,seq,GENL_ID_CTRL,CTRL_CMD_GETFAMILY,CTRL_ATTR_FAMILY_NAME,"nl80211",8)&&cfg_reply(f,seq,GENL_ID_CTRL,CTRL_CMD_NEWFAMILY,0,&s,end);seq++;
 if(ok)ok=cfg_request(f,seq,s.family,NL80211_CMD_GET_INTERFACE,NL80211_ATTR_IFINDEX,&s.ifindex,4)&&cfg_reply(f,seq,s.family,NL80211_CMD_NEW_INTERFACE,1,&s,end);seq++;
 if(ok)ok=cfg_request(f,seq,s.family,NL80211_CMD_GET_WIPHY,NL80211_ATTR_WIPHY,&s.wiphy,4)&&cfg_reply(f,seq,s.family,NL80211_CMD_NEW_WIPHY,2,&s,end);seq++;
 if(ok)ok=cfg_reg_request(f,seq,s.family)&&cfg_reply(f,seq,s.family,NL80211_CMD_GET_REG,3,&s,end);close(f);
 if(!ok)return failed("CFG_PHY_OR_CN_RULE_QUERY_INCOMPLETE");if(s.present!=3u||s.allowed!=s.present)return failed("CFG_CN_PROFILE_CHANNEL_DISABLED_OR_RESTRICTED");if(!s.have_reg)return failed("CFG_CN_PROFILE_RULE_NOT_CONFIRMED");
 note(s.vht==s.present?"CFG_VHT_DECLARED":"CFG_VHT_NOT_REPORTED_BY_PINNED_LINUX_3_5 not_a_hardware_capability_result=1");note("CFG_CN_PROFILE_CHANNELS_READY actual_AP_frequency_and_width_not_yet_measured=1");return 1;
}
