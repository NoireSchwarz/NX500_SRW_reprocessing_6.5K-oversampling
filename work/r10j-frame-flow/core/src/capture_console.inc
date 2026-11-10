/* SPDX-License-Identifier: AGPL-3.0-only. CID182 is capture CLI, not root CLI.
 * Only fixed `st cap` commands. No RTOS CAP address/pointer is transmitted.
 */
static unsigned native_packet_reads,console_outputs_confirmed,console_text_bytes;
static int send_packet(const char *text){
 unsigned off=0;size_t len=strlen(text);
 if(!p33_command_allowed(text)||len>=128||!safe_call()||!p39_packet_extent(packet_count,(unsigned)len,&off))return 0;
 for(unsigned i=0;i<=len;i++){expected[off+i]=(unsigned char)text[i];cap_alias[off+i]=(unsigned char)text[i];}
#if defined(__arm__)
 __asm__ volatile("dsb sy\n\tdmb sy":::"memory");
#endif
 /* Verify the exact bytes through the original process's alias before MCB
  * copies them. Destination is our controlled shell-message scratch area. */
 const uint32_t scratch=receive_buffer+48;unsigned char observed[128];struct alloc_regs r;
 trace_phase="native_cli_packet_readback";
 if(!invoke3(libc_base+ALLOC_MEMCPY,scratch,response[0]+off,(uint32_t)len+1,3000)||!get_regs(&r)||r.r[0]!=scratch||!read_process(scratch,observed,len+1))return 0;
 int matched=!memcmp(observed,text,len+1);
 fprintf(command_log,"NATIVE_PACKET_READBACK packet=%u bytes=%u match=%u hex=",packet_count,(unsigned)len+1,(unsigned)matched);
 for(unsigned i=0;i<=len;i++)fprintf(command_log,"%02x",observed[i]);fputc('\n',command_log);fflush(command_log);
 if(!matched)return 0;native_packet_reads++;
 trace_phase="capture_console_send";
 if(!invoke4(mcb_base+P33_MCB_SEND,182,0,(uint32_t)len,response[0]+off,3000)||!get_regs(&r)||r.r[0])return 0;
 fprintf(command_log,"SEND packet=%u bytes=%u text=%s\n",packet_count,(unsigned)len,text);fflush(command_log);packet_count++;send_calls++;return safe_call();
}
static int capture_console_text(uint32_t before,uint32_t *tail,unsigned *bytes,unsigned *text_bytes){
 uint32_t after=volatile_word(console_map+4),first=0,second=0;
 if(!p33_ring_span(before,after,P33_CONSOLE_CAPACITY,&first,&second))return 0;
 unsigned total=first+second;
 for(unsigned i=0;i<first;i++)console_raw[i]=console_map[8+before+i];
 for(unsigned i=0;i<second;i++)console_raw[first+i]=console_map[8+i];
 if(volatile_word(console_map+4)!=after)return -1;size_t normalized=0;
 if(!p33_normalize_console(console_raw,total,(char*)console_text,sizeof console_text,&normalized))return 0;
 *tail=after;*bytes=total;*text_bytes=(unsigned)normalized;return 1;
}
static int console_command(enum p33_command which,const char *dir,const char *name){
 const char *text=p33_command_text(which);if(!text||!safe_call())return 0;
 uint32_t before=volatile_word(console_map+4),last=before,tail=before;unsigned bytes=0,text_bytes=0;
 if(before>=P33_CONSOLE_CAPACITY)return 0;
 char path[1200],raw_path[1210];snprintf(path,sizeof path,"%s/%s",dir,name);snprintf(raw_path,sizeof raw_path,"%s.raw",path);FILE *f=fopen(path,"wx");if(!f)return 0;FILE *raw=fopen(raw_path,"wx");if(!raw){fclose(f);return 0;}
 int ok=send_packet(text),complete=0;const uint64_t start=ns();uint64_t changed=start;
 const int needs_text=which==P33_MON_STATUS||which==P33_MON_SHOW||which==P33_SEQ_STATUS;
 while(ok&&!stop_requested&&ns()-start<5000000000ULL){
  if(!safe_call()){ok=0;break;}
  int got=capture_console_text(before,&tail,&bytes,&text_bytes);if(!got){ok=0;break;}if(got<0){pause_ms(10);continue;}
  if(tail!=last){last=tail;changed=ns();}
  int recognized=which==P33_MON_STATUS?p33_monitor_state((const char*)console_text)>=0:
    which==P33_MON_SHOW?(strstr((const char*)console_text,"IPC Parameter")&&strstr((const char*)console_text,"Base Info")&&strstr((const char*)console_text,"Parameters")):
    which==P33_SEQ_STATUS?bytes>0:1;
  /* An empty mutating-command response is not execution evidence. The caller
   * always follows START/CLEAN/STOP with a fresh, recognized STATUS query. */
  if(recognized&&ns()-changed>=200000000ULL&&(needs_text||ns()-start>=350000000ULL)){complete=1;break;}
  pause_ms(10);
 }
 if(fwrite(console_text,1,text_bytes,f)!=text_bytes||fflush(f)||fsync(fileno(f)))ok=0;
 if(fwrite(console_raw,1,bytes,raw)!=bytes||fflush(raw)||fsync(fileno(raw)))ok=0;
 if(fclose(f))ok=0;if(fclose(raw))ok=0;
 console_bytes+=bytes;console_text_bytes+=text_bytes;if(ok&&complete&&needs_text)console_outputs_confirmed++;
 fprintf(command_log,"CAPTURE_OUTPUT command=%u before=%u after=%u bytes=%u text_bytes=%u complete=%u requires_text=%u elapsed_ms=%u\n",(unsigned)which,before,tail,bytes,text_bytes,(unsigned)complete,(unsigned)needs_text,ms());fflush(command_log);
 return ok&&complete;
}
