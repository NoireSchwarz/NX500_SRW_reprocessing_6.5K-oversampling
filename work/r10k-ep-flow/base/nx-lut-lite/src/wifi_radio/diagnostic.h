/* SPDX-License-Identifier: AGPL-3.0-only; bounded ordinary-file reads only. */
static int diagnostic_private(const char*s){return strcasestr(s,"password")||strcasestr(s,"passphrase")||strcasestr(s,"ssid")||strcasestr(s,"psk")||strcasestr(s,"key=");}
static void diagnostic_tail(const char*path,int filtered){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;char line[200];
 if(f<0){snprintf(line,sizeof line,"DIAG_LOG path=%s unavailable=%d",path,errno);note(line);return;}
 if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<=0){close(f);snprintf(line,sizeof line,"DIAG_LOG path=%s empty_or_nonregular=1",path);note(line);return;}
 size_t cap=32768;char*b=malloc(cap+1);if(!b){close(f);return;}off_t off=st.st_size>(off_t)cap?st.st_size-(off_t)cap:0;ssize_t n=pread(f,b,cap,off);close(f);
 if(n<=0){free(b);return;}b[n]=0;char*p=b;if(off){char*end=memchr(p,'\n',(size_t)n);p=end?end+1:b+n;}unsigned rows=0;
 snprintf(line,sizeof line,"DIAG_LOG_BEGIN path=%s last_bytes=%ld filtered=%d",path,(long)n,filtered);note(line);
 while(p<b+n&&rows<160){char*end=memchr(p,'\n',(size_t)(b+n-p));if(!end)end=b+n;char saved=*end;*end=0;int valid=1;for(char*q=p;q<end;q++)if((unsigned char)*q<32&&*q!='\t'){valid=0;break;}
  if(valid&&!diagnostic_private(p)&&(!filtered||strcasestr(p,"wlan")||strcasestr(p,"sd8xxx")||strcasestr(p,"mlan")||strcasestr(p,"sdio")||strcasestr(p,"mmc1")||strcasestr(p,"cfg80211")||strcasestr(p,"firmware")||strcasestr(p,"mobileap")||strcasestr(p,"tether")||strcasestr(p,"net-config"))){printf("RADIO_LOG %s\n",p);rows++;}
  *end=saved;p=end<b+n?end+1:end;
 }snprintf(line,sizeof line,"DIAG_LOG_END path=%s lines=%u",path,rows);note(line);free(b);
}
static void diagnostic_snapshot(const char*tag){
 char saved[sizeof error_reason];memcpy(saved,error_reason,sizeof saved);struct hot_state s;int valid=hot_snapshot(&s,0),power=card_state();char line[300];snprintf(line,sizeof line,"DIAG_SNAPSHOT tag=%s native_valid=%d state=%d owned=%d phase=%d callback=%08x power=%d modules=%d mlan0=%d uap0=%d p2p0=%d",tag,valid,valid?s.state:-1,valid?s.owned:0,valid?s.phase:-1,valid?s.callback:0,power,module_mask(),interface_present("mlan0"),interface_present("uap0"),interface_present("p2p0"));note(line);
 diagnostic_tail("/var/log/wlanplug_log",0);diagnostic_tail("/var/log/messages",1);memcpy(error_reason,saved,sizeof saved);
}
