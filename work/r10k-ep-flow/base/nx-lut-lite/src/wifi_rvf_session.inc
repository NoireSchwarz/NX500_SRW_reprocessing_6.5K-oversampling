/* SPDX-License-Identifier: AGPL-3.0-only */

#define RVF_OWNER WIFI_TMP "/wifi-rvf-owner-rc613"
#define RVF_OWNER_MAGIC 0x4e583613u
#define RVF_PICTURE_MAGIC 0x4a503613u
#define RVF_IDLE_MS 12000ull
#define RVF_JOIN_MS 60000ull
#define RVF_STOP_MS 125000ull
#define RVF_LOG "/mnt/mmc/NXDEMO51/logs/wifi-rvf-rc614.log"

enum { RVF_ENTERING=1,RVF_READY,RVF_EXITING,RVF_EXITED,RVF_UNCONFIRMED,RVF_RADIO_RESTORE };
struct rvf_owner {
    uint32_t magic,checksum,window;
    int32_t pid,camera,phase,ap_up;
    unsigned long long tick,camera_tick;
    char reason[80];
};
static struct rvf_owner native;
static pid_t receiver;
static unsigned long long native_ready_at,native_viewer_seen,native_close_at;
static char native_token[33];
static int rvf_update_read=-1,rvf_update_write=-1;
static unsigned long long rvf_update_event;
static unsigned long long rvf_push_connections,rvf_push_frames,rvf_push_bytes,rvf_push_skipped;
static struct rvf_record rvf_stream_record;
static int rvf_frequency_mhz,rvf_verified_frequency;

static double rvf_rate(const struct rvf_record *r){
    return r->frames>1&&r->last_frame>r->first_frame?
        (r->frames-1)*1000.0/(r->last_frame-r->first_frame):0;
}

static void rvf_stage(const char *name,int a,int b,int c){
    if(physical_usb()!=0)return;
    int fd=open(RVF_LOG,O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return;
    char line[180];
    int n=snprintf(line,sizeof line,WIFI_VERSION " session=%ld_%llu ms=%llu stage=%s a=%d b=%d c=%d\n",
        (long)native.pid,native.tick,monotime(),name,a,b,c);
    if(n>0&&(size_t)n<sizeof line)(void)write(fd,line,(size_t)n);
    close(fd);
}

static int rvf_owner_read(struct rvf_owner *owner){
    return wire_read(RVF_OWNER,owner,sizeof *owner,RVF_OWNER_MAGIC)&&
        owner->phase>=RVF_ENTERING&&owner->phase<=RVF_RADIO_RESTORE&&
        !owner->reason[79]&&strspn(owner->reason,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==strlen(owner->reason);
}

static int rvf_blocks_hotspot(void){
    struct stat st;
    if(lstat(RVF_OWNER,&st))return errno!=ENOENT;
    struct rvf_owner owner;
    return !rvf_owner_read(&owner)||owner.phase!=RVF_EXITED;
}

static int rvf_owner_write(int phase,const char *reason){
    native.phase=phase;
    snprintf(native.reason,sizeof native.reason,"%s",reason);
    fprintf(stderr,"RVF_OWNER phase=%d reason=%s\n",phase,reason);
    return wire_write(RVF_OWNER,&native,sizeof native,RVF_OWNER_MAGIC);
}

/* LISTEN mask for 7788, 7676, 7679, 7789, 8181. Read errors return -1. */
static int rvf_listeners(int include_web){
    FILE *f=fopen("/proc/net/tcp","r");
    if(!f)return -1;
    char line[512];int any=0;
    while(fgets(line,sizeof line,f)){
        unsigned ip,port,peer,rport,state;
        if(sscanf(line," %*u: %x:%x %x:%x %x",&ip,&port,&peer,&rport,&state)!=5||state!=10)continue;
        if(port==MODE_PORT)any|=1;
        else if(port==MEDIA_CONTROL_PORT)any|=2;
        else if(port==MEDIA_STREAM_PORT)any|=4;
        else if(port==CALLBACK_PORT)any|=8;
        else if(include_web&&port==WIFI_PORT)any|=16;
    }
    if(ferror(f))any=-1;
    if(fclose(f))any=-1;
    return any;
}

static int rvf_ap_up(void){
    int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(fd<0)return -1;
    struct ifreq ap={0};strcpy(ap.ifr_name,"uap0");
    int state=ioctl(fd,SIOCGIFFLAGS,&ap)==0?!!(ap.ifr_flags&IFF_UP):(errno==ENODEV||errno==ENXIO?0:-1);
    close(fd);return state;
}

static int rvf_native_idle(const struct rvf_owner *owner){
    uint32_t gui,mode;
    char ip[32];
    return nxl_rvf_ui_observe(owner->camera,owner->camera_tick,&gui,&mode)&&
        !mode&&gui&&gui!=NXL_RVF_GUI_MOBILE&&gui!=NXL_RVF_GUI_VIEWFINDER&&
        rvf_listeners(1)==0&&!interface_ip("uap0",ip);
}

static int rvf_ui_command(int command){
    struct nxl_rvf_ui_request req={
        .owner=native.pid,.camera=native.camera,.command=command,.window=native.window,
        .owner_tick=native.tick,.camera_tick=native.camera_tick,.issued=monotime()
    };
    return nxl_rvf_ui_write(&req);
}

static int rvf_snapshot_publish(const unsigned char *p,size_t n){
    char temp[160];snprintf(temp,sizeof temp,RVF_FRAME ".%ld",(long)getpid());
    int fd=open(temp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    struct rvf_picture h={RVF_PICTURE_MAGIC,(uint32_t)n,rvf.width,rvf.height,rvf.frames,monotime()};
    int ok=write(fd,&h,sizeof h)==sizeof h&&write(fd,p,n)==(ssize_t)n;
    if(close(fd))ok=0;
    if(ok)ok=!rename(temp,RVF_FRAME);
    if(!ok)unlink(temp);
    if(ok&&rvf_update_write>=0){
        const unsigned char changed=1;
        (void)write(rvf_update_write,&changed,1);
    }
    return ok;
}

static int rvf_soap_local(const struct rvf_url *u,const struct identity *id,unsigned char *buf,int heartbeat){
    const char *action=heartbeat?"HeartBeat":"GetInfomation";
    const char *args=heartbeat?"<Duration>5</Duration>":"<GPSINFO>UNKNOWN</GPSINFO>";
    char body[512],request[1300];
    int n=snprintf(body,sizeof body,"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:%s xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">%s</u:%s></s:Body></s:Envelope>",action,args,action);
    snprintf(request,sizeof request,"POST %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: SEC_RVF_ML_%s\r\nAccess-Method: manual\r\nSOAPACTION: \"urn:schemas-upnp-org:service:ContentDirectory:1#%s\"\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",u->path,u->ip,u->port,id->mac,action,n,body);
    phase=heartbeat?"heartbeat":"get_information";
    if(!heartbeat)return get_xml(u,request,buf);
    /* A heartbeat is acknowledged by the original HTTP status. Its optional
     * XML is unused, so it cannot hold up frame reception until EOF. */
    struct reply r=open_http(u,request,buf,monotime()+2200);
    if(r.fd>=0)close(r.fd);
    return r.fd<0?0:r.http;
}

static int rvf_mime_observation(const unsigned char *buf,size_t header_bytes){
    if(!header_bytes||header_bytes>HEADER_CAP)return 0;
    char head[HEADER_CAP+1],type[80];
    memcpy(head,buf,header_bytes);head[header_bytes]=0;
    if(!rvf_header(head,"Content-Type",type,sizeof type))return 0;
    if(!strcasecmp(type,"video/x-avi"))return 1;
    if(!strcasecmp(type,"video/x-avi \n"))return 2;
    return 3;
}

static const char *rvf_consume(const struct rvf_url *url,const struct rvf_url *control,
        const struct identity *id,unsigned char *buf){
    char request[620];
    snprintf(request,sizeof request,"GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: SEC_RVF_ML_%s\r\nAccess-Method: manual\r\nConnection: close\r\n\r\n",url->path,url->ip,url->port,id->mac);
    phase="stream";
    struct reply r=open_http(url,request,buf,monotime()+8000);
    rvf_stage("STREAM_HTTP_MIME_SOCKET",r.http,rvf_mime_observation(buf,r.body),r.fd>=0);
    if(r.fd<0)return "STOCK_STREAM_UNAVAILABLE";
    const char *result="STOCK_STREAM_HTTP_UNSUPPORTED";
    if(r.http==401||r.http==403)result="STOCK_STREAM_DENIED";
    if(r.http!=200&&r.http!=206)goto close_stream;
    /* The original HandleHeader can append a bare LF to the MIME value.
     * Trust the verified original listener and validate its AVI and JPEG
     * bytes before publication. The MIME value above is diagnostic only. */
    unsigned char *heartbeat=malloc(HEADER_CAP+1);
    if(!heartbeat){result="STOCK_STREAM_MEMORY_UNAVAILABLE";goto close_stream;}
    size_t used=r.used-r.body;memmove(buf,buf+r.body,used);
    size_t offset=0;int have_avi=0,optional_pad=0;
    unsigned long long now=monotime(),begin=now,next_beat=now+1500,next_gate=now,next_publish=0;
    result="STOCK_STREAM_ENDED";
    while(!stopping){
        now=monotime();
        if(now>=next_gate){
            check_peer();
            if(outside_peer||peer_check_error){result="STOCK_PEER_OR_TABLE_CHANGED";break;}
            if(!original_active()||!wire_process(rvf.server,rvf.server_tick,RVF_EXE))break;
            next_gate=now+500;
        }
        if(now-(rvf.frames?rvf.last_frame:begin)>6000){result="STOCK_JPEG_TIMEOUT";break;}
        if(now>=next_beat){
            if(rvf_soap_local(control,id,heartbeat,1)!=200){result="STOCK_HEARTBEAT_FAILED";break;}
            next_beat=monotime()+1500;
        }
        if(!have_avi){
            struct rvf_avi a;
            int valid=rvf_avi_header(buf,used,&a,&offset);
            if(valid<0||offset>65536||(!valid&&used>65536)){result="STOCK_AVI_UNSUPPORTED";break;}
            if(valid){
                rvf.width=a.width;rvf.height=a.height;rvf.usec=a.usec;strcpy(rvf.codec,a.codec);
                memmove(buf,buf+offset,used-offset);used-=offset;have_avi=1;
                printf("AVI codec=%s width=%u height=%u nominal_usec=%u\n",a.codec,a.width,a.height,a.usec);
            }
        }
        while(have_avi&&used&&!stopping){
            if(optional_pad){optional_pad=0;if(!buf[0]){memmove(buf,buf+1,--used);continue;}}
            if(used<8)break;
            uint32_t size=rvf_le32(buf+4);
            if(!size||size>RVF_FRAME_CAP){result="STOCK_AVI_CHUNK_INVALID";goto finish;}
            if(used<8+size)break;
            if(!memcmp(buf,"00dc",4)||!memcmp(buf,"00db",4)){
                if(!rvf_jpeg(buf+8,size,rvf.width,rvf.height)){result="STOCK_JPEG_INVALID";goto finish;}
                now=monotime();if(!rvf.frames)rvf.first_frame=now;
                rvf.frames++;rvf.last_frame=now;rvf.bytes+=size;rvf.state=RVF_STREAMING;
                if(!rvf_snapshot_publish(buf+8,size)){result="STOCK_JPEG_PUBLICATION_FAILED";goto finish;}
            }else if(memcmp(buf,"JUNK",4)&&memcmp(buf,"01wb",4)){result="STOCK_AVI_CHUNK_UNSUPPORTED";goto finish;}
            memmove(buf,buf+size+8,used-size-8);used-=size+8;optional_pad=size&1u;
        }
        if(rvf.frames&&monotime()>=next_publish){
            if(!rvf_publish("STOCK_RVF_STREAMING")){result="RVF_JOURNAL_FAILED";break;}
            next_publish=monotime()+800;
        }
        if(used==PROBE_CAP){result="STOCK_AVI_LIMIT";break;}
        struct pollfd poller={r.fd,POLLIN,0};
        int ready=callback_poll(&poller,1,100);
        if(ready<0&&errno!=EINTR)break;
        if(ready<=0)continue;
        ssize_t size=recv(r.fd,buf+used,PROBE_CAP-used,0);
        if(size<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(size<=0)break;
        used+=(size_t)size;
    }
finish:
    if(stopping)result="STOCK_RVF_STOP_REQUESTED";
    free(heartbeat);
close_stream:
    close(r.fd);return result;
}

static const char *rvf_media(const struct identity *id,const struct confirmation *ack,unsigned char *buf){
    struct rvf_url desc,stream;
    if((!rvf_url_parse(ack->location,id->ip,&desc,0)||desc.port!=MEDIA_CONTROL_PORT)&&
       (!rvf_url_parse(callback.location,id->ip,&desc,0)||desc.port!=MEDIA_CONTROL_PORT)){
        phase="discover";
        if(!discover(&desc)){
            strcpy(desc.ip,id->ip);desc.port=MEDIA_CONTROL_PORT;strcpy(desc.path,"/smp_2_");
        }
    }
    char request[840],url[224];
    snprintf(request,sizeof request,"GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: SEC_RVF_ML_%s\r\nAccess-Method: manual\r\nConnection: close\r\n\r\n",desc.path,desc.ip,desc.port,id->mac);
    phase="description";
    int status=get_xml(&desc,request,buf);
    if(status==401||status==403)return "STOCK_DESCRIPTION_DENIED";
    if(status!=200||!rvf_control_path((char*)buf,desc.path,sizeof desc.path))return "STOCK_DESCRIPTION_UNAVAILABLE";
    status=rvf_soap_local(&desc,id,buf,0);
    if(status==401||status==403)return "STOCK_INFORMATION_DENIED";
    if(status!=200||!rvf_xml_unescape((char*)buf))return "STOCK_INFORMATION_UNAVAILABLE";
    if((!rvf_xml_value((char*)buf,"QualityHighUrl",url,sizeof url)||!media_url(url,&stream))&&
       (!rvf_xml_value((char*)buf,"QualityHighMjpgUrl",url,sizeof url)||!media_url(url,&stream)))return "STOCK_STREAM_URL_UNVERIFIED";
    return rvf_consume(&stream,&desc,id,buf);
}

static int rvf_receive(const struct identity *id){
    memset(&rvf,0,sizeof rvf);
    rvf.pid=getpid();rvf.tick=proc_ticks(rvf.pid);rvf.camera=native.camera;rvf.camera_tick=native.camera_tick;
    rvf.server=native.pid;rvf.server_tick=native.tick;rvf.started=monotime();rvf.state=RVF_STARTING;
    strcpy(rvf.ip,id->ip);
    if(!rvf_publish("STOCK_REGISTERING"))return 1;
    unsigned char *buffer=malloc(PROBE_CAP+1);
    if(!buffer)return 1;
    const char *reason="STOCK_PEER_OR_CALLBACK_UNAVAILABLE";
    check_peer();
    if(!stopping&&!outside_peer&&!peer_check_error&&original_active()&&stock_ports()==1&&start_callback(id)){
        struct confirmation ack=mode_request(id,0,buffer);
        rvf_stage("REGISTER",ack.status,ack.server,!strcasecmp(ack.operation,"RVF"));
        reason=ack.status==401||ack.status==403?"STOCK_REGISTRATION_DENIED":"STOCK_REGISTRATION_UNCONFIRMED";
        if(ack.status==200&&ack.server&&*ack.operation){
            rvf.dirty=1;rvf.requests=1;
            if(rvf_publish("STOCK_REGISTERED")){
                reason="STOCK_MEDIA_LISTENERS_UNCONFIRMED";
                unsigned long long deadline=monotime()+8000;
                while(original_active()&&monotime()<deadline){
                    check_peer();
                    if(outside_peer||peer_check_error){reason="STOCK_PEER_OR_TABLE_CHANGED";break;}
                    if((stock_ports()&6)==6&&(!strcasecmp(ack.operation,"RVF")||!strcasecmp(callback.mode,"RVF"))){
                        reason=rvf_media(id,&ack,buffer);break;
                    }
                    callback_poll(NULL,0,150);
                }
            }else reason="RVF_JOURNAL_FAILED";
            releasing=1;
            check_peer();
            if(!outside_peer&&!peer_check_error&&original_active()){
                struct confirmation bye=mode_request(id,1,buffer);
                if(bye.status==200&&bye.server)rvf.requests|=2;
            }
        }
    }
    close_callback();free(buffer);unlink(RVF_FRAME);
    rvf.state=rvf.frames&&stopping?RVF_DONE:RVF_FAILED;
    rvf_publish(reason);
    printf("RVF_RECEIVER frames=%llu bytes=%llu byebye=%d callbacks=%u rejected=%u\n",rvf.frames,rvf.bytes,(rvf.requests&2)!=0,callback.calls,callback.rejected);
    return rvf.state!=RVF_DONE;
}

static int rvf_preflight(uint32_t window){
    if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 0;}
    struct stat st;char ip[32];int ap_up=rvf_ap_up();
    if(!lstat(HOT_OWNER,&st)||errno!=ENOENT||service_pid()||ap_up<0||interface_ip("uap0",ip)||rvf_listeners(1)!=0){
        why="ORIGINAL_NETWORK_BUSY";return 0;
    }
    struct rvf_owner old;
    if(!lstat(RVF_OWNER,&st)&&!rvf_owner_read(&old)){why="RVF_JOURNAL_FAILED";return 0;}
    if(rvf_owner_read(&old)&&old.phase!=RVF_EXITED){
        uint32_t gui,mode;
        if(!nxl_rvf_ui_observe(old.camera,old.camera_tick,&gui,&mode)||mode||
           gui==NXL_RVF_GUI_MOBILE||gui==NXL_RVF_GUI_VIEWFINDER){why="RVF_RECOVERY_UNCONFIRMED";return 0;}
    }
    for(unsigned i=0;i<sizeof rvf_originals/sizeof *rvf_originals;i++){
        if(!hot_fingerprint(rvf_originals[i].path,rvf_originals[i].bytes,rvf_originals[i].fnv)){
            why="ORIGINAL_RVF_BINARY_MISMATCH";return 0;
        }
    }
    uint32_t gui,mode;
    if(window<=1||!hot_camera(&rvf.camera,&rvf.camera_tick)||
       !nxl_rvf_ui_observe(rvf.camera,rvf.camera_tick,&gui,&mode)||mode||!gui||
       gui==NXL_RVF_GUI_MOBILE||gui==NXL_RVF_GUI_VIEWFINDER){why="ORIGINAL_GUI_NOT_READY";return 0;}
    native=(struct rvf_owner){.window=window,.pid=getpid(),.tick=proc_ticks(getpid()),
        .camera=rvf.camera,.camera_tick=rvf.camera_tick,.ap_up=-1};
    if(!native.tick||!rvf_owner_write(RVF_ENTERING,"RVF_PREPARING_RADIO")){why="RVF_JOURNAL_FAILED";return 0;}
    return 1;
}

static int rvf_wait_receiver(void){
    unsigned long long began=monotime(),deadline=began+42000,registered=0;
    int old_mode=-2,old_ports=-2,old_ip=-2;
    while(!stopping&&physical_usb()==0&&monotime()<deadline){
        if(!receiver){
            uint32_t mode;struct identity id;
            int active=rvf_native_state(native.camera,native.camera_tick,&mode)?(int)mode:-1;
            int ports=active==2?stock_ports():0;
            int ap=active==2&&identity_for("uap0",&id);
            if(active!=old_mode||ports!=old_ports||ap!=old_ip){
                rvf_stage("ORIGINAL_MODE_LISTENERS_IP",active,ports,ap);
                old_mode=active;old_ports=ports;old_ip=ap;
            }
            if(active==2&&ports==1&&ap){
                int updates[2];
                if(pipe2(updates,O_NONBLOCK|O_CLOEXEC)){why="RVF_FRAME_NOTIFY_UNAVAILABLE";return 0;}
                receiver=fork();
                if(!receiver){
                    close(updates[0]);
                    rvf_update_write=updates[1];
                    int rc=rvf_receive(&id);
                    close(rvf_update_write);fflush(stdout);fflush(stderr);_exit(rc);
                }
                close(updates[1]);
                if(receiver>0)rvf_update_read=updates[0];else close(updates[0]);
                if(receiver<0){receiver=0;why="RVF_RECEIVER_FORK_FAILED";return 0;}
                registered=monotime();deadline=registered+36000;
            }
        }else{
            struct rvf_record current;
            if(rvf_record_read(&current)&&current.pid==receiver&&current.server==native.pid&&
               current.server_tick==native.tick&&current.started>=registered-50){
                if(current.state==RVF_STREAMING&&current.frames&&current.updated+3000>=monotime()&&
                   wire_process(receiver,current.tick,RVF_EXE)){
                    rvf_stage("FIRST_JPEG",(int)current.width,(int)current.height,1);return 1;
                }
                if(current.state==RVF_FAILED){rvf=current;why=rvf.reason;return 0;}
            }
            int status;pid_t end=waitpid(receiver,&status,WNOHANG);
            if(end==receiver){receiver=0;why="RVF_RECEIVER_ENDED_BEFORE_READY";return 0;}
        }
        poll(NULL,0,160);
    }
    why=stopping?"RVF_STOP_REQUESTED":old_mode!=2?"ORIGINAL_RVF_MODE_NOT_ENTERED":
        !registered?"ORIGINAL_RVF_PORT_OR_IP_NOT_READY":"RVF_READY_TIMEOUT";
    return 0;
}

static void rvf_log_result(const char *reason,int restored,int radio_restored){
    if(physical_usb()!=0)return;
    int fd=open(RVF_LOG,O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return;
    struct rvf_record current={0};
    if(!rvf_record_read(&current)||current.server!=native.pid||current.server_tick!=native.tick)memset(&current,0,sizeof current);
    char line[680];
    int n=snprintf(line,sizeof line,WIFI_VERSION " session=%ld_%llu epoch=%lld reason=%s stream=%s frames=%llu size=%ux%u codec=%s byebye=%d original_ports_hotspot_restored=%d radio_restored=%d frequency_mhz=%d native_fps=%.3f web_connections=%llu web_frames=%llu web_jpeg_bytes=%llu web_frame_id_gaps=%llu\n",
        (long)native.pid,native.tick,(long long)time(NULL),reason,current.reason,current.frames,current.width,current.height,current.codec,
        (current.requests&2)!=0,restored,radio_restored,rvf_verified_frequency,rvf_rate(&current),
        rvf_push_connections,rvf_push_frames,rvf_push_bytes,rvf_push_skipped);
    if(n>0&&(size_t)n<sizeof line)(void)write(fd,line,(size_t)n);
    close(fd);
}

static int rvf_restore_native(void){
    (void)rvf_owner_write(RVF_EXITING,"RVF_RESTORING");
    unlink(NXL_RVF_UI_REQUEST);
    if(receiver>0){
        kill(receiver,SIGTERM);
        unsigned long long until=monotime()+6300;
        pid_t ended=0;
        while((ended=waitpid(receiver,NULL,WNOHANG))==0&&monotime()<until)poll(NULL,0,50);
        if(ended==0){kill(receiver,SIGKILL);while(waitpid(receiver,NULL,0)<0&&errno==EINTR){}}
        receiver=0;
    }
    if(rvf_update_read>=0){close(rvf_update_read);rvf_update_read=-1;}
    unlink(RVF_FRAME);
    unsigned long long since=0,end=monotime()+30000;
    int exit_sent=0,restored=0;
    int last_gui=-2,last_mode=-2,last_ports=-2,last_ip=-2,last_up=-2;
    while(monotime()<end){
        uint32_t gui,mode;char ip[32];
        if(physical_usb()==0&&nxl_rvf_ui_observe(native.camera,native.camera_tick,&gui,&mode)){
            int ports=rvf_listeners(1),has_ip=interface_ip("uap0",ip),up=rvf_ap_up();
            if(last_gui!=(int)gui||last_mode!=(int)mode||last_ports!=ports){
                rvf_stage("RESTORE_GUI_MODE_PORTS",(int)gui,(int)mode,ports);
                last_gui=(int)gui;last_mode=(int)mode;last_ports=ports;
            }
            if(last_ip!=has_ip||last_up!=up){rvf_stage("RESTORE_AP_IP_UP_BASELINE",has_ip,up,native.ap_up);last_ip=has_ip;last_up=up;}
            int camera_menu=gui==NXL_RVF_GUI_MOBILE||gui==NXL_RVF_GUI_VIEWFINDER;
            if(camera_menu&&!exit_sent){exit_sent=rvf_ui_command(NXL_RVF_UI_EXIT);if(exit_sent)rvf_stage("RESTORE_MENU_REQUEST",(int)gui,(int)mode,1);}
            /* The radio lease may have brought uap0 up before the menu opened.
             * A down interface also confirms native release in that case. */
            if(!mode&&!camera_menu&&ports==0&&!has_ip&&up>=0&&
               (native.ap_up<0||up==0||up==native.ap_up)){
                if(!since)since=monotime();
                if(monotime()-since>=1800){restored=1;break;}
            }else since=0;
        }else{
            since=0;
            if(last_gui!=-1){rvf_stage("RESTORE_STATE_UNAVAILABLE",-1,-1,-1);last_gui=-1;}
        }
        poll(NULL,0,180);
    }
    unlink(NXL_RVF_UI_REQUEST);
    return restored;
}
