/* SPDX-License-Identifier: AGPL-3.0-only. Strict JSON scalar extraction. */
#ifndef NXL_RAW422_POST_JSON_H
#define NXL_RAW422_POST_JSON_H
#include "front_json.h"
struct p49_slice {const char *p;size_t n;};
static int p49_field(const char *text,size_t n,const char *wanted,struct p49_slice *out){
 struct p49_json j={text,text+n,0};p49_jws(&j);if(j.p==j.end||*j.p++!='{')return 0;char names[256][96];unsigned count=0,found=0;
 for(;;){p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}'){if(count)return 0;j.p++;break;}if(count==256||!p49_jstring(&j,names[count],sizeof names[0]))return 0;for(unsigned i=0;i<count;i++)if(!strcmp(names[i],names[count]))return 0;char *key=names[count++];p49_jws(&j);if(j.p==j.end||*j.p++!=':')return 0;p49_jws(&j);const char *start=j.p;if(!p49_jvalue(&j))return 0;if(!strcmp(key,wanted)){out->p=start;out->n=(size_t)(j.p-start);found=1;}p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}'){j.p++;break;}if(*j.p++!=',')return 0;}
 p49_jws(&j);return found&&j.p==j.end;
}
static int p49_field_text(const char *json,size_t n,const char *key,const char *expected){struct p49_slice v;if(!p49_field(json,n,key,&v))return 0;size_t z=strlen(expected);return v.n==z+2&&v.p[0]=='"'&&v.p[z+1]=='"'&&!memcmp(v.p+1,expected,z);}
static int p49_field_uint(const char *json,size_t n,const char *key,unsigned expected){struct p49_slice v;return p49_field(json,n,key,&v)&&p49_junsigned(v.p,v.n,expected);}
static int p49_field_boolean(const char *json,size_t n,const char *key,int expected){struct p49_slice v;if(!p49_field(json,n,key,&v))return 0;const char *p=expected?"true":"false";size_t z=expected?4:5;return v.n==z&&!memcmp(v.p,p,z);}

#endif
