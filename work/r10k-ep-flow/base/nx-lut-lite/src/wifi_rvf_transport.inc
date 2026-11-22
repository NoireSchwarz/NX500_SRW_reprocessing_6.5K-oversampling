/* SPDX-License-Identifier: AGPL-3.0-only
 * Local RVF transport. Requests and parsers carry over the real-camera proof.
 */
#define CALLBACK_PORT 7789u
#define CALLBACK_CAP 9216u
#define REGISTER_MS 12000ull
#define RELEASE_MS 4000ull

#define MEDIA_CONTROL_PORT 7676u
#define MEDIA_STREAM_PORT 7679u
#define MODE_PORT 7788u
#define HEADER_CAP 8192u
#define XML_CAP 32768u
#define PROBE_CAP (RVF_FRAME_CAP+65536u+HEADER_CAP)

static int outside_peer, peer_check_error, releasing;
static uint32_t active_state=UINT32_MAX;
static const char *phase;

static int original_active(void){
    return (!stopping||releasing) && physical_usb()==0 &&
        rvf_native_state(rvf.camera,rvf.camera_tick,&active_state) && active_state==2;
}

static void check_peer(void){
    FILE *table=fopen("/proc/net/tcp","r");
    if(!table){peer_check_error=1;return;}
    char line[512];
    struct in_addr address={0};
    if(inet_pton(AF_INET,rvf.ip,&address)!=1){fclose(table);return;}
    while(fgets(line,sizeof line,table)){
        unsigned local,port,remote,remote_port,state;
        if(sscanf(line," %*u: %x:%x %x:%x %x",&local,&port,&remote,&remote_port,&state)!=5)continue;
        if((port==MODE_PORT||port==MEDIA_CONTROL_PORT||port==MEDIA_STREAM_PORT)&&
            (state==1||state==3)&&remote&&remote!=address.s_addr&&
            (ntohl(remote)>>24)!=127)outside_peer=1;
    }
    if(ferror(table))peer_check_error=1;
    fclose(table);
}

static int callback_poll(struct pollfd *,nfds_t,int);
static int io_wait(int fd,short event,unsigned long long deadline){
    while((!stopping||releasing)&&physical_usb()==0){
        unsigned long long now=monotime();
        if(now>=deadline)return 0;
        unsigned long long left=deadline-now;
        struct pollfd p={fd,event,0};
        int ready=callback_poll(&p,1,(int)(left>100?100:left));
        if(ready<0&&errno==EINTR)continue;
        if(ready<0)return 0;
        if(ready)return (p.revents&(event|POLLHUP))!=0;
    }
    return 0;
}

static int local_connect(const struct rvf_url *url,unsigned long long deadline){
    if(monotime()>=deadline||!original_active()||outside_peer||peer_check_error||strcmp(url->ip,rvf.ip)||
        !rvf_port_owned(rvf.camera,rvf.camera_tick,url->port))return -1;
    struct sockaddr_in own={.sin_family=AF_INET},to;
    if(inet_pton(AF_INET,rvf.ip,&own.sin_addr)!=1)return -1;
    to=own;to.sin_port=htons((uint16_t)url->port);
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0)return -1;
    if(bind(fd,(struct sockaddr*)&own,sizeof own))goto fail;
    if(connect(fd,(struct sockaddr*)&to,sizeof to)&&errno!=EINPROGRESS)goto fail;
    if(!io_wait(fd,POLLOUT,deadline))goto fail;
    int error=0;socklen_t size=sizeof error;
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)||error)goto fail;
    struct sockaddr_in source={0};size=sizeof source;
    if(getsockname(fd,(struct sockaddr*)&source,&size)||source.sin_addr.s_addr!=own.sin_addr.s_addr)goto fail;
    return fd;
fail:
    close(fd);return -1;
}

static int send_request(int fd,const char *request,unsigned long long deadline){
    size_t remaining=strlen(request);
    while(remaining&&io_wait(fd,POLLOUT,deadline)){
        ssize_t count=send(fd,request,remaining,MSG_NOSIGNAL);
        if(count<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(count<=0)return 0;
        request+=count;remaining-=(size_t)count;
    }
    return !remaining;
}

struct reply {int fd,http;size_t used,body;};

static int read_more(struct reply *r,unsigned char *buf,size_t limit,unsigned long long deadline){
    if(r->used==limit)return -1;
    while(io_wait(r->fd,POLLIN,deadline)){
        ssize_t count=recv(r->fd,buf+r->used,limit-r->used,0);
        if(count<0&&(errno==EINTR||errno==EAGAIN))continue;
        if(count<=0)return (int)count;
        r->used+=(size_t)count;return 1;
    }
    return -1;
}

static struct reply open_http(const struct rvf_url *url,const char *request,unsigned char *buf,unsigned long long deadline){
    struct reply r={.fd=local_connect(url,deadline)};
    if(r.fd<0)return r;
    if(!send_request(r.fd,request,deadline))goto fail;
    while(read_more(&r,buf,HEADER_CAP,deadline)>0){
        const unsigned char *end=memmem(buf,r.used,"\r\n\r\n",4);
        if(!end)continue;
        r.body=(size_t)(end-buf)+4;
        char header[HEADER_CAP+1];
        if(memchr(buf,0,r.body))break;
        memcpy(header,buf,r.body);header[r.body]=0;
        r.http=rvf_http_status(header);
        char transfer[80]="";
        rvf_header(header,"Transfer-Encoding",transfer,sizeof transfer);
        if(strcmp(phase,"heartbeat")||r.http!=200)printf("HTTP purpose=%s status=%d header_bytes=%zu body_buffered=%zu chunked=%d\n",
               phase,r.http,r.body,r.used-r.body,*transfer!=0);
        if(*transfer&&(r.http==200||r.http==206))break;
        return r;
    }
fail:
    close(r.fd);r.fd=-1;return r;
}

static int get_xml(const struct rvf_url *url,const char *request,unsigned char *buf){
    unsigned long long deadline=monotime()+4500;
    struct reply r=open_http(url,request,buf,deadline);
    if(r.fd<0)return r.http>=400?r.http:0;
    if(r.http!=200){close(r.fd);return r.http;}
    char h[HEADER_CAP+1],length[40];
    memcpy(h,buf,r.body);h[r.body]=0;
    size_t need=0;int sized=rvf_header(h,"Content-Length",length,sizeof length);
    if(sized){
        char *end;unsigned long count=strtoul(length,&end,10);
        if(*end||count==0||count>XML_CAP-r.body){close(r.fd);return 0;}
        need=r.body+count;
    }
    int result=1;
    while((!sized||r.used<need)&&result>0)result=read_more(&r,buf,XML_CAP,deadline);
    close(r.fd);
    if((sized?r.used!=need:result!=0)||r.used<=r.body||memchr(buf+r.body,0,r.used-r.body))return 0;
    memmove(buf,buf+r.body,r.used-r.body);buf[r.used-r.body]=0;
    return 200;
}

static int discover(struct rvf_url *url){
    struct sockaddr_in own={.sin_family=AF_INET},multicast={.sin_family=AF_INET,.sin_port=htons(1900)};
    if(inet_pton(AF_INET,rvf.ip,&own.sin_addr)!=1||inet_pton(AF_INET,"239.255.255.250",&multicast.sin_addr)!=1)return 0;
    int fd=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0)return 0;
    const unsigned char ttl=1;
    int found=0;
    if(bind(fd,(struct sockaddr*)&own,sizeof own)||
       setsockopt(fd,IPPROTO_IP,IP_MULTICAST_IF,&own.sin_addr,sizeof own.sin_addr)||
       setsockopt(fd,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof ttl))goto done;
    static const char query[]="M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 3\r\nST: ssdp:all\r\n\r\n";
    if(sendto(fd,query,sizeof query-1,0,(struct sockaddr*)&multicast,sizeof multicast)!=(ssize_t)sizeof query-1)goto done;
    unsigned long long until=monotime()+3500;
    while(io_wait(fd,POLLIN,until)){
        struct sockaddr_in source={0};socklen_t size=sizeof source;
        char reply[4096],location[224];
        ssize_t got=recvfrom(fd,reply,sizeof reply-1,0,(struct sockaddr*)&source,&size);
        if(got<=0||source.sin_addr.s_addr!=own.sin_addr.s_addr||memchr(reply,0,(size_t)got))continue;
        reply[got]=0;
        if(rvf_http_status(reply)==200&&rvf_header(reply,"LOCATION",location,sizeof location)&&
           rvf_url_parse(location,rvf.ip,url,0)&&url->port==MEDIA_CONTROL_PORT){found=1;break;}
    }
done:
    close(fd);return found;
}

static int media_url(const char *s,struct rvf_url *url){
    static const char *const paths[]={"/livestream.avi","/livestream_mjpg.avi"};
    for(unsigned i=0;i<sizeof paths/sizeof*paths;i++){
        char expected[224];
        snprintf(expected,sizeof expected,"http://%s:%u%s",rvf.ip,MEDIA_STREAM_PORT,paths[i]);
        if(strcmp(s,expected))continue;
        strcpy(url->ip,rvf.ip);strcpy(url->path,paths[i]);url->port=MEDIA_STREAM_PORT;return 1;
    }
    return 0;
}

struct identity {char interface[IF_NAMESIZE],ip[32],mac[18],upper[18];};
struct callback {
    int listener,client;
    struct in_addr own;
    unsigned calls,rejected;
    size_t used;
    unsigned long long expires;
    char request[CALLBACK_CAP+1];
    char mode[12],location[224];
};
static struct callback callback={.listener=-1,.client=-1};

static int identity_for(const char *iface,struct identity *id){
    char ip[32],again[32];
    if(!interface_ip(iface,ip))return 0;
    uint32_t address;
    if(inet_pton(AF_INET,ip,&address)!=1)return 0;
    address=ntohl(address);
    if((address>>24)!=10&&(address>>20)!=0xac1&&(address>>16)!=0xc0a8)return 0;

    int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(fd<0)return 0;
    struct ifreq r={0};
    snprintf(r.ifr_name,sizeof r.ifr_name,"%s",iface);
    int ok=ioctl(fd,SIOCGIFHWADDR,&r)==0&&r.ifr_hwaddr.sa_family==1;
    close(fd);
    if(!ok||!interface_ip(iface,again)||strcmp(ip,again))return 0;

    const unsigned char *mac=(const unsigned char*)r.ifr_hwaddr.sa_data;
    unsigned any=0;
    for(unsigned i=0;i<6;i++)any|=mac[i];
    if(!any||(mac[0]&1))return 0;
    snprintf(id->interface,sizeof id->interface,"%s",iface);
    strcpy(id->ip,ip);
    snprintf(id->mac,sizeof id->mac,"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(id->upper,sizeof id->upper,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return 1;
}

static int identity_still_present(const struct identity *id){
    struct identity current;
    return identity_for(id->interface,&current)&&!strcmp(id->ip,current.ip)&&!strcmp(id->mac,current.mac);
}

static void close_callback_client(void){
    if(callback.client>=0)close(callback.client);
    callback.client=-1;callback.used=0;
}

static void close_callback(void){
    close_callback_client();
    if(callback.listener>=0)close(callback.listener);
    callback.listener=-1;
}

static int start_callback(const struct identity *id){
    struct sockaddr_in own={.sin_family=AF_INET,.sin_port=htons(CALLBACK_PORT)};
    if(inet_pton(AF_INET,id->ip,&own.sin_addr)!=1)return 0;
    callback.own=own.sin_addr;
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0)return 0;
    int reuse=1;
    if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)||
       bind(fd,(struct sockaddr*)&own,sizeof own)||listen(fd,2)){close(fd);return 0;}
    callback.listener=fd;
    printf("CALLBACK_LISTEN ip=%s port=%u\n",id->ip,CALLBACK_PORT);
    return 1;
}

/* Recognize only the genuine stock mode names and the already pinned URL form. */
static void record_callback(const char *header,const char *body){
    static const char *const tags[]={"CurrentMode","currentmode","DefaultURL","defaulturl","DescriptionURL","descriptionurl"};
    char mode[40]="",location[224]="";
    for(unsigned i=0;i<2&&!*mode;i++)rvf_xml_value(body,tags[i],mode,sizeof mode);
    if(!*mode)rvf_header(header,"Operation-Mode",mode,sizeof mode);
    if(strcasecmp(mode,"RVF")&&strcasecmp(mode,"IDLE")&&strcasecmp(mode,"BYEBYE"))return;
    strcpy(callback.mode,mode);
    callback.location[0]=0;
    rvf_header(header,"Default-URL",location,sizeof location);
    for(unsigned i=2;i<sizeof tags/sizeof*tags&&!*location;i++)rvf_xml_value(body,tags[i],location,sizeof location);
    struct rvf_url desc;
    if(rvf_url_parse(location,rvf.ip,&desc,0)&&desc.port==MEDIA_CONTROL_PORT)strcpy(callback.location,location);
    printf("CALLBACK mode=%s description_advertised=%d\n",callback.mode,*callback.location!=0);
}

static void callback_read(void){
    if(callback.client<0)return;
    char *request=callback.request;
    ssize_t size=recv(callback.client,request+callback.used,CALLBACK_CAP-callback.used,MSG_DONTWAIT);
    if(size<0&&(errno==EAGAIN||errno==EINTR))return;
    if(size<=0){close_callback_client();return;}
    if(memchr(request+callback.used,0,(size_t)size)){close_callback_client();return;}
    callback.used+=(size_t)size;
    request[callback.used]=0;
    char *end=strstr(request,"\r\n\r\n");
    if(!end){if(callback.used>=HEADER_CAP)close_callback_client();return;}
    size_t header_size=(size_t)(end-request)+4;
    if(header_size>HEADER_CAP){close_callback_client();return;}
    const char *first=strstr(request,"\r\n");
    if(!first||first-request>95){close_callback_client();return;}
    char request_line[96];
    size_t line_size=(size_t)(first-request);
    memcpy(request_line,request,line_size);request_line[line_size]=0;
    char method[9],path[32],version[12],extra;
    if(sscanf(request_line,"%8s %31s %11s %c",method,path,version,&extra)!=3||
       (strcmp(method,"NOTIFY")&&strcmp(method,"POST"))||strcmp(path,"/eventCallback")||
       (strcmp(version,"HTTP/1.1")&&strcmp(version,"HTTP/1.0"))){close_callback_client();return;}

    char header[HEADER_CAP+1],len[16],transfer[32];
    memcpy(header,request,header_size);header[header_size]=0;
    if(!rvf_header(header,"Content-Length",len,sizeof len)||
       rvf_header(header,"Transfer-Encoding",transfer,sizeof transfer)){close_callback_client();return;}
    char *tail;unsigned long bytes=strtoul(len,&tail,10);
    if(*tail||bytes>CALLBACK_CAP-header_size){close_callback_client();return;}
    if(callback.used<header_size+bytes)return;
    if(callback.used!=header_size+bytes||strstr(request+header_size,"<!")){close_callback_client();return;}
    record_callback(header,request+header_size);
    static const char response[]="HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    ssize_t sent=send(callback.client,response,sizeof response-1,MSG_NOSIGNAL|MSG_DONTWAIT);
    if(sent!=(ssize_t)sizeof response-1)puts("CALLBACK reply_incomplete=1");
    close_callback_client();
}

static int callback_poll(struct pollfd *network,nfds_t count,int ms){
    if(callback.listener<0)return poll(network,count,ms);
    struct pollfd jobs[3]={
        {count?network->fd:-1,count?network->events:0,0},
        {callback.listener,POLLIN,0},{callback.client,POLLIN,0},
    };
    int rc=poll(jobs,3,ms);
    if(rc<0)return rc;
    if(callback.client>=0&&monotime()>=callback.expires)close_callback_client();
    if(callback.client>=0&&jobs[2].revents)callback_read();
    if(callback.client<0&&(jobs[1].revents&POLLIN)){
        struct sockaddr_in peer={0};socklen_t size=sizeof peer;
        int fd=accept4(callback.listener,(struct sockaddr*)&peer,&size,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(fd>=0){
            if(peer.sin_family!=AF_INET||peer.sin_addr.s_addr!=callback.own.s_addr){
                callback.rejected++;close(fd);
            }else{
                callback.calls++;
                callback.client=fd;callback.expires=monotime()+2400;
                callback_read();
            }
        }
    }
    if(!count)return 0;
    network->revents=jobs[0].revents;
    return network->revents!=0;
}

struct confirmation {int status,server;char operation[20],location[224];};

static struct confirmation mode_request(const struct identity *id,int release,unsigned char *buffer){
    struct confirmation ack={0};
    struct rvf_url url={.port=MODE_PORT,.path="/mode/control"};
    strcpy(url.ip,id->ip);
    const char *request_kind=release?"byebye":"alive";
    phase=release?"byebye":"register";
    if(!identity_still_present(id))return ack;
    char request[1024];
    int n=snprintf(request,sizeof request,
        "HEAD %s HTTP/1.1\r\nHost: %s:%u\r\nAccept-Encoding: identity\r\n"
        "User-Agent: SEC_MODE_%s\r\nAccess-Method: manual\r\nConnection: close\r\n"
        "NTS: %s\r\nContent-Length: 0\r\nHOST-Address: %s\r\nHOST-Mac: %s\r\n"
        "HOST-port: %u\r\nHOST-PNumber: none\r\nCALLBACK: <http://%s:%u/eventCallback>\r\n\r\n",
        url.path,url.ip,url.port,id->upper,request_kind,id->ip,id->mac,CALLBACK_PORT,id->ip,CALLBACK_PORT);
    if(n<0||(size_t)n>=sizeof request)return ack;
    struct reply r=open_http(&url,request,buffer,monotime()+(release?RELEASE_MS:REGISTER_MS));
    ack.status=r.http;
    if(r.fd>=0){
        char h[HEADER_CAP+1],server[42],operation[20];
        memcpy(h,buffer,r.body);h[r.body]=0;close(r.fd);
        ack.server=rvf_header(h,"Server",server,sizeof server)&&!strcasecmp(server,"SEC Mode Server");
        if(rvf_header(h,"Operation-Mode",operation,sizeof operation)&&
           (!strcasecmp(operation,"RVF")||!strcasecmp(operation,"IDLE")))strcpy(ack.operation,operation);
        rvf_header(h,"Default-URL",ack.location,sizeof ack.location);
    }
    printf("MODE request=%s http=%d stock_server=%d operation=%s\n",request_kind,ack.status,ack.server,
           *ack.operation?ack.operation:"unknown");
    return ack;
}

static int stock_ports(void){
    return (rvf_port_owned(rvf.camera,rvf.camera_tick,MODE_PORT)?1:0)|
           (rvf_port_owned(rvf.camera,rvf.camera_tick,MEDIA_CONTROL_PORT)?2:0)|
           (rvf_port_owned(rvf.camera,rvf.camera_tick,MEDIA_STREAM_PORT)?4:0);
}

