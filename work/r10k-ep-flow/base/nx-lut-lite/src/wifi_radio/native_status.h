/* SPDX-License-Identifier: AGPL-3.0-only; credential-bearing bytes stay in RAM. */
static void native_stop_status(pid_t child){
 if(child<=1)return;char path[160];snprintf(path,sizeof path,TRIAL_TMP "/native-stop.%ld.status",(long)child);
 char b[2048],reason[96]={0},line[240];struct stat st;int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 if(f<0){note("NATIVE_STOP_STATUS_UNAVAILABLE");return;}int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<(off_t)sizeof b&&read(f,b,(size_t)st.st_size)==st.st_size;int rc=close(f);if(!ok||rc){note("NATIVE_STOP_STATUS_UNAVAILABLE");return;}b[st.st_size]=0;
 if(memchr(b,0,(size_t)st.st_size)||strncmp(b,"NXL_WIFI3\n",10)||b[st.st_size-1]!='\n'){note("NATIVE_STOP_STATUS_INVALID");return;}
 unsigned fields=0;int values[4]={-1,-1,-1,-1},ended=0;char*save=NULL;char*p=strtok_r(b,"\n",&save);
 while((p=strtok_r(NULL,"\n",&save))){if(ended){ok=0;break;}if(!strcmp(p,"end=NXL_WIFI_END3")){ended=1;continue;}
  if(!strncmp(p,"reason=",7)){const char*v=p+7;size_t n=strlen(v);if((fields&1)||!n||n>=sizeof reason||strspn(v,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=n){ok=0;break;}memcpy(reason,v,n+1);fields|=1;continue;}
  const char*keys[]={"ap_valid=","ap_state=","owned=","connected="};for(unsigned i=0;i<4;i++){size_t n=strlen(keys[i]);if(strncmp(p,keys[i],n))continue;char*end;errno=0;long v=strtol(p+n,&end,10);unsigned bit=2u<<i;if((fields&bit)||errno||end==p+n||*end||v<0-(i==1)||v>(i==1?21:1)){ok=0;break;}values[i]=(int)v;fields|=bit;}if(!ok)break;
 }if(!ok||!ended||fields!=31){note("NATIVE_STOP_STATUS_INVALID");return;}
 snprintf(line,sizeof line,"NATIVE_STOP_STATUS reason=%s ap_valid=%d ap_state=%d owned=%d connected=%d",reason,values[0],values[1],values[2],values[3]);note(line);
}
