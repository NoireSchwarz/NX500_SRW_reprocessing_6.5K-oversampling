/* SPDX-License-Identifier: AGPL-3.0-only */
#include <netinet/tcp.h>

/* Each HTTP chunk holds six big-endian words followed by the original JPEG.
 * Fetch exposes arbitrary byte segments after HTTP transfer decoding. */
#define RVF_PUSH_MAGIC 0x4e584a31u
#define RVF_PUSH_HEADER 24u
#define RVF_PUSH_PREFIX 16u

static void rvf_push_response(struct client *c){
    if(!rvf_viewer(c))return;
    for(unsigned i=0;i<WIFI_CLIENTS;i++){
        if(clients[i].rvf_push){
            text_response(c,429,"RVF_STREAM_ALREADY_OPEN");
            return;
        }
    }
    int nodelay=1,send_buffer=32768;
    if(setsockopt(c->fd,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof nodelay)||
       setsockopt(c->fd,SOL_SOCKET,SO_SNDBUF,&send_buffer,sizeof send_buffer)){
        text_response(c,503,"RVF_STREAM_SOCKET_UNAVAILABLE");
        return;
    }
    c->data=malloc(RVF_PUSH_PREFIX+RVF_PUSH_HEADER+RVF_FRAME_CAP+2u);
    if(!c->data){text_response(c,503,"MEMORY_UNAVAILABLE");return;}
    const char response_header[]="HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    memcpy(c->header,response_header,sizeof response_header);
    c->head_len=sizeof response_header-1;
    c->rvf_push=1;
    c->push_event=ULLONG_MAX;
    rvf_tcp_open(c->fd,++rvf_push_connections,monotime());
}

static void rvf_push_updates(short events){
    unsigned char bytes[256];
    ssize_t n=read(rvf_update_read,bytes,sizeof bytes);
    if(n>0)rvf_update_event++;
    if(n==0||(events&(POLLERR|POLLHUP|POLLNVAL))){
        close(rvf_update_read);
        rvf_update_read=-1;
    }
}

static void rvf_push_fill(struct client *c,unsigned long long now){
    if(c->sent<c->length||c->push_event==rvf_update_event||native_close_at)return;
    c->push_event=rvf_update_event;
    if(!rvf_stream_record.updated||now-rvf_stream_record.updated>=500){
        struct rvf_record record;
        if(!rvf_current(&record)||record.state!=RVF_STREAMING)return;
        rvf_stream_record=record;
    }
    const struct rvf_record *r=&rvf_stream_record;
    if(r->updated>now||now-r->updated>4000)return;

    int fd=open(RVF_FRAME,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return;
    struct stat st;
    struct rvf_picture h;
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&read(fd,&h,sizeof h)==sizeof h&&
        h.magic==RVF_PICTURE_MAGIC&&h.id>c->push_id&&h.id<=UINT32_MAX&&
        h.size>=16&&h.size<=RVF_FRAME_CAP&&st.st_size==(off_t)(sizeof h+h.size)&&
        h.when>=r->started&&h.when<=now&&now-h.when<3000&&h.width==720&&h.height==480;
    if(!ok){close(fd);return;}
    size_t payload=RVF_PUSH_HEADER+h.size;
    int prefix=snprintf((char *)c->data,RVF_PUSH_PREFIX,"%zx\r\n",payload);
    if(prefix<=0||(unsigned)prefix>=RVF_PUSH_PREFIX){close(fd);return;}
    unsigned char *body=c->data+prefix;
    double fps=rvf_rate(r);
    uint32_t words[]={htonl(RVF_PUSH_MAGIC),htonl(h.size),htonl((uint32_t)h.id),
        htonl((uint32_t)(now-h.when)),htonl((uint32_t)rvf_frequency_mhz),
        htonl(fps>0&&fps<1000?(uint32_t)(1000*fps+0.5):0)};
    memcpy(body,words,sizeof words);
    ok=read(fd,body+sizeof words,h.size)==(ssize_t)h.size;
    close(fd);
    if(!ok)return;
    memcpy(body+payload,"\r\n",2);
    c->length=(size_t)prefix+payload+2;
    c->sent=0;
    uint32_t skipped=c->push_id?(uint32_t)(h.id-c->push_id-1):0;
    rvf_push_skipped+=skipped;
    rvf_tcp_select(now,(uint32_t)h.id,skipped);
    c->push_id=h.id;
    c->push_payload=h.size;
}

static void rvf_push_send(struct client *c){
    int header=c->head_sent<c->head_len;
    size_t left=header?c->head_len-c->head_sent:c->length-c->sent;
    if(!left)return;
    const void *p=header?(const void *)(c->header+c->head_sent):c->data+c->sent;
    ssize_t n=send(c->fd,p,left,MSG_NOSIGNAL);
    if(n<=0){
        if(n<0&&errno==EAGAIN)rvf_tcp.window.eagain++;
        else if(!n||errno!=EINTR)client_close_with_status(c,"STREAM_SEND_ERROR",n<0?errno:0);
        return;
    }
    c->activity=monotime();
    if(header)c->head_sent+=(size_t)n;
    else{
        if((size_t)n<left)rvf_tcp.window.partial++;
        transfer.body+=(unsigned long long)n;
        c->sent+=(size_t)n;
        if(c->sent==c->length){
            native_viewer_seen=c->activity;
            rvf_tcp_queued(c->activity);
            rvf_push_frames++;
            rvf_push_bytes+=c->push_payload;
        }
    }
}

static void rvf_push_finish(struct client *c){
    static const char end[]="0\r\n\r\n";
    if(c->rvf_push&&c->head_len==c->head_sent&&c->length==c->sent)
        (void)send(c->fd,end,sizeof end-1,MSG_NOSIGNAL);
}
