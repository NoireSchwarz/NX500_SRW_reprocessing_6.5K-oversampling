/* SPDX-License-Identifier: AGPL-3.0-only. Small strict JSON reader for root
 * producer fields. Parses complete JSON, rejecting duplicate root keys. */
#ifndef P49_FRONT_JSON_H
#define P49_FRONT_JSON_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
struct p49_json {const char *p,*end;unsigned depth;};
static void p49_jws(struct p49_json *j){while(j->p<j->end&&(*j->p==' '||*j->p=='\n'||*j->p=='\r'||*j->p=='\t'))j->p++;}
static int p49_jhex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int p49_jstring(struct p49_json *j,char *out,size_t cap){
 if(j->p==j->end||*j->p++!='"')return 0;size_t n=0;
 while(j->p<j->end){unsigned c=(unsigned char)*j->p++;if(c=='"'){if(out){if(n>=cap)return 0;out[n]=0;}return 1;}if(c<32)return 0;
  if(c=='\\'){if(j->p==j->end)return 0;c=(unsigned char)*j->p++;switch(c){case '"':case '\\':case '/':break;case 'b':c=8;break;case 'f':c=12;break;case 'n':c=10;break;case 'r':c=13;break;case 't':c=9;break;case 'u':{if(j->end-j->p<4)return 0;unsigned u=0;for(unsigned k=0;k<4;k++){int h=p49_jhex(*j->p++);if(h<0)return 0;u=(u<<4)|(unsigned)h;}/* Root keys are ASCII; other strings need no decoding. */if(out&&u>127)return 0;c=u;break;}default:return 0;}}
  if(out){if(n+1>=cap||c==0||c>127)return 0;out[n++]=(char)c;}
 }return 0;
}
static int p49_jnumber(struct p49_json *j){const char *p=j->p;if(p<j->end&&*p=='-')p++;if(p==j->end)return 0;if(*p=='0')p++;else{if(*p<'1'||*p>'9')return 0;do{p++;}while(p<j->end&&*p>='0'&&*p<='9');}if(p<j->end&&*p=='.'){p++;const char *s=p;while(p<j->end&&*p>='0'&&*p<='9')p++;if(p==s)return 0;}if(p<j->end&&(*p=='e'||*p=='E')){p++;if(p<j->end&&(*p=='+'||*p=='-'))p++;const char *s=p;while(p<j->end&&*p>='0'&&*p<='9')p++;if(p==s)return 0;}j->p=p;return 1;}
static int p49_jvalue(struct p49_json *j){
 p49_jws(j);if(j->p==j->end||j->depth++>20)return 0;char c=*j->p;
 if(c=='"'){if(!p49_jstring(j,NULL,0))return 0;}
 else if(c=='{'||c=='['){int object=c=='{';j->p++;p49_jws(j);char close=object?'}':']';if(j->p<j->end&&*j->p==close)j->p++;else for(;;){if(object){if(!p49_jstring(j,NULL,0))return 0;p49_jws(j);if(j->p==j->end||*j->p++!=':')return 0;}if(!p49_jvalue(j))return 0;p49_jws(j);if(j->p==j->end)return 0;if(*j->p==close){j->p++;break;}if(*j->p++!=',')return 0;p49_jws(j);}}
 else if(j->end-j->p>=4&&!memcmp(j->p,"true",4))j->p+=4;
 else if(j->end-j->p>=5&&!memcmp(j->p,"false",5))j->p+=5;
 else if(j->end-j->p>=4&&!memcmp(j->p,"null",4))j->p+=4;
 else if(!p49_jnumber(j))return 0;
 j->depth--;return 1;
}
struct p49_front_fields {uint32_t nonce;unsigned have_nonce,have_exit,have_version,have_cc,have_reloaded,have_format,have_wb,have_wb_schema,bindings;int cc,reloaded;};
struct p49_front_expected {const char *source,*y,*uv;unsigned width,height,stride;};
static int p49_jhash(const char *text,size_t length,const char *want){return length==66&&text[0]=='"'&&text[65]=='"'&&!memcmp(text+1,want,64);}
static int p49_junsigned(const char *text,size_t length,unsigned want){uint64_t v=0;if(!length)return 0;for(size_t i=0;i<length;i++){if(text[i]<'0'||text[i]>'9')return 0;v=v*10+(unsigned)(text[i]-'0');if(v>UINT32_MAX)return 0;}return v==want;}
static int p49_front_json(const char *text,size_t n,uint32_t nonce,unsigned cc_mode,const struct p49_front_expected *expected){
 struct p49_json j={text,text+n,0};p49_jws(&j);if(j.p==j.end||*j.p++!='{')return 0;
 char names[256][96];unsigned count=0;struct p49_front_fields f;memset(&f,0,sizeof f);
 for(;;){p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}'){if(count)return 0;j.p++;break;}if(count==256||!p49_jstring(&j,names[count],sizeof names[0]))return 0;for(unsigned i=0;i<count;i++)if(!strcmp(names[i],names[count]))return 0;char *key=names[count++];p49_jws(&j);if(j.p==j.end||*j.p++!=':')return 0;p49_jws(&j);const char *start=j.p;if(!p49_jvalue(&j))return 0;size_t length=(size_t)(j.p-start);
  if(!strcmp(key,"version")){f.have_version=1;const char want[]="\"0.3.6-demo50\"";if(length!=sizeof want-1||memcmp(start,want,sizeof want-1))return 0;}
  else if(!strcmp(key,"exit")){f.have_exit=1;if(length!=1||*start!='0')return 0;}
  else if(!strcmp(key,"nonce")){f.have_nonce=1;uint64_t v=0;if(!length)return 0;for(size_t k=0;k<length;k++){if(start[k]<'0'||start[k]>'9')return 0;v=v*10+(unsigned)(start[k]-'0');if(v>UINT32_MAX)return 0;}f.nonce=(uint32_t)v;}
  else if(!strcmp(key,"source_cc_enabled")){f.have_cc=1;if(length==4&&!memcmp(start,"true",4))f.cc=1;else if(length==5&&!memcmp(start,"false",5))f.cc=0;else return 0;}
  else if(!strcmp(key,"source_cc_reloaded")){if(f.have_reloaded)return 0;f.have_reloaded=1;if(length==4&&!memcmp(start,"true",4))f.reloaded=1;else if(length==5&&!memcmp(start,"false",5))f.reloaded=0;else return 0;}
  else if(!strcmp(key,"format")){f.have_format=1;const char want[]="\"NXIPCOWNED1\"";if(length!=sizeof want-1||memcmp(start,want,sizeof want-1))return 0;}
  else if(!strcmp(key,"source_wb_enabled")){f.have_wb=1;if(length!=4||memcmp(start,"true",4))return 0;}
  else if(!strcmp(key,"wb_packet_schema")){f.have_wb_schema=1;if(length!=1||*start!='2')return 0;}
  else if(!strcmp(key,"source_sha256")){f.bindings|=1u;if(!p49_jhash(start,length,expected->source))return 0;}
  else if(!strcmp(key,"luma_sha256")){f.bindings|=2u;if(!p49_jhash(start,length,expected->y))return 0;}
  else if(!strcmp(key,"chroma_sha256")){f.bindings|=4u;if(!p49_jhash(start,length,expected->uv))return 0;}
  else if(!strcmp(key,"output_width")){f.bindings|=8u;if(!p49_junsigned(start,length,expected->width))return 0;}
  else if(!strcmp(key,"output_height")){f.bindings|=16u;if(!p49_junsigned(start,length,expected->height))return 0;}
  else if(!strcmp(key,"plane_stride")){f.bindings|=32u;if(!p49_junsigned(start,length,expected->stride))return 0;}
  p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}'){j.p++;break;}if(*j.p++!=',')return 0;
 }
 p49_jws(&j);return j.p==j.end&&f.have_nonce&&f.have_exit&&f.have_version&&f.have_format&&f.have_wb&&f.have_wb_schema&&f.have_cc&&f.have_reloaded&&f.bindings==63u&&f.nonce==nonce&&(unsigned)f.cc==cc_mode&&(unsigned)f.reloaded==cc_mode;
}
#endif
