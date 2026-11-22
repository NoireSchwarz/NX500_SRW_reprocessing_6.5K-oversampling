/* SPDX-License-Identifier: AGPL-3.0-only. Read-only, bounded DCIM access. */
#ifndef NXL_WIFI_MEDIA_H
#define NXL_WIFI_MEDIA_H
static int wifi_component(const char*s,int dir){
 size_t n=strlen(s);if(!n||n>80||strstr(s,"..")||s[0]=='.')return 0;
 for(size_t i=0;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||s[i]=='.'))return 0;
 if(dir)return n>=4&&s[0]>='1'&&s[0]<='9'&&s[1]>='0'&&s[1]<='9'&&s[2]>='0'&&s[2]<='9';
 const char*p=strrchr(s,'.');return p&&(!strcasecmp(p,".jpg")||!strcasecmp(p,".jpeg")||!strcasecmp(p,".srw")||!strcasecmp(p,".mp4"));
}
static int wifi_decode(const char*s,size_t n,char*out,size_t cap){
 size_t k=0;for(size_t i=0;i<n;i++){
  unsigned ch=(unsigned char)s[i];if(ch=='%'){
   if(i+2>=n)return 0;unsigned v=0;
   for(unsigned j=0;j<2;j++){ch=(unsigned char)s[++i];if(ch>='0'&&ch<='9')ch-='0';else if(ch>='a'&&ch<='f')ch=ch-'a'+10;else if(ch>='A'&&ch<='F')ch=ch-'A'+10;else return 0;v=v*16+ch;}ch=v;
  }
  if(ch<32||ch>126||ch=='\\'||ch=='+'||k+1>=cap)return 0;out[k++]=(char)ch;
 }out[k]=0;return 1;
}
static int wifi_media_open(int root,const char*path){
 char tmp[170];if(strlen(path)>=sizeof tmp)return-1;strcpy(tmp,path);char*p=strchr(tmp,'/');if(!p||strchr(p+1,'/'))return-1;*p++=0;
 if(!wifi_component(tmp,1)||!wifi_component(p,0))return-1;
 int d=openat(root,tmp,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return-1;
 int f=openat(d,p,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);close(d);if(f<0)return-1;
 struct stat st;if(fstat(f,&st)||!S_ISREG(st.st_mode)){close(f);return-1;}return f;
}
static unsigned wifi_u16(const unsigned char*b,int le){return le?(unsigned)b[0]|(unsigned)b[1]<<8:(unsigned)b[0]<<8|b[1];}
static uint32_t wifi_u32(const unsigned char*b,int le){return le?(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24:(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
/* EXIF IFD1 slice only. No image decode, cache, MPF rewrite or ISP access. */
static int wifi_thumbnail(int fd,unsigned char*out,size_t cap,size_t*length){
 unsigned char h[65536];ssize_t got=pread(fd,h,sizeof h,0);if(got<4||h[0]!=255||h[1]!=216)return 0;
 size_t n=(size_t)got,p=2;
 while(p+4<=n){if(h[p++]!=255)return 0;while(p<n&&h[p]==255)p++;if(p>=n)return 0;unsigned marker=h[p++];if(marker==0xda||marker==0xd9)return 0;
  if(marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
  if(p+2>n)return 0;size_t size=wifi_u16(h+p,0);if(size<2||size>n-p)return 0;
  if(marker==0xe1&&size>=16&&!memcmp(h+p+2,"Exif\0\0",6)){
   const unsigned char*t=h+p+8;size_t len=size-8;int le=t[0]=='I'&&t[1]=='I';
   if(!le&&!(t[0]=='M'&&t[1]=='M'))return 0;if(wifi_u16(t+2,le)!=42)return 0;
   uint32_t a=wifi_u32(t+4,le);if(a>len||len-a<2)return 0;unsigned count=wifi_u16(t+a,le),orientation=1;
   if(count>512||(uint64_t)a+2+(uint64_t)count*12+4>len)return 0;
   for(unsigned i=0;i<count;i++){const unsigned char*e=t+a+2+i*12;if(wifi_u16(e,le)==274&&wifi_u16(e+2,le)==3&&wifi_u32(e+4,le)==1){unsigned v=wifi_u16(e+8,le);if(v>=1&&v<=8)orientation=v;}}
   a=wifi_u32(t+a+2+count*12,le);if(!a||a>len||len-a<2)return 0;count=wifi_u16(t+a,le);
   if(count>128||(uint64_t)a+2+(uint64_t)count*12>len)return 0;uint32_t off=0,bytes=0;
   for(unsigned i=0;i<count;i++){const unsigned char*e=t+a+2+i*12;unsigned tag=wifi_u16(e,le);
    if((tag==513||tag==514)&&wifi_u16(e+2,le)==4&&wifi_u32(e+4,le)==1){if(tag==513)off=wifi_u32(e+8,le);else bytes=wifi_u32(e+8,le);}}
   if(bytes<4||bytes>cap||off>len||bytes>len-off||t[off]!=255||t[off+1]!=216||t[off+bytes-2]!=255||t[off+bytes-1]!=217)return 0;
   if(orientation==1){memcpy(out,t+off,bytes);*length=bytes;return 1;}
   if(bytes>cap-36)return 0;
   unsigned char exif[36]={255,225,0,34,'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,1,0,18,1,3,0,1,0,0,0,0,0,0,0,0,0,0,0};exif[28]=(unsigned char)orientation;
   out[0]=255;out[1]=216;memcpy(out+2,exif,36);memcpy(out+38,t+off+2,bytes-2);*length=bytes+36;return 1;
  }p+=size;
 }return 0;
}
#endif
