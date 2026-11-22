/* SPDX-License-Identifier: AGPL-3.0-only */

static int rvf_credentials(const struct rvf_owner *owner,struct hot_state *ap){
    if(hot_snapshot(ap,1)&&ap->pid==owner->camera&&ap->tick==owner->camera_tick&&
       ap->ready&&ap->ssid[0]&&(ap->security==0||(ap->security==1&&ap->password[0])))return 1;
    memset(ap,0,sizeof *ap);ap->security=-1;
    uint32_t mode;char ip[32];
    if(!rvf_native_state(owner->camera,owner->camera_tick,&mode)||mode!=2||!interface_ip("uap0",ap->ip))return 0;
    struct {
        char name[IF_NAMESIZE];
        union {struct {void *data;uint16_t length,flags;} point;unsigned char pad[16];} u;
    } iw={0};
    strcpy(iw.name,"uap0");
    int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(fd<0)return 0;
    /* SIOCGIWESSID and SIOCGIWENCODE from the Linux wireless UAPI. */
    iw.u.point.data=ap->ssid;iw.u.point.length=sizeof ap->ssid;
    int ok=ioctl(fd,0x8b1b,&iw)==0&&iw.u.point.flags&&hot_ascii(ap->ssid,sizeof ap->ssid,1);
    unsigned char encoding[32];
    iw.u.point.data=encoding;iw.u.point.length=sizeof encoding;iw.u.point.flags=0;
    ok=ok&&ioctl(fd,0x8b2b,&iw)==0&&(iw.u.point.flags&0x8000u)!=0;
    close(fd);
    if(!ok||!interface_ip("uap0",ip)||strcmp(ip,ap->ip)||
       !rvf_native_state(owner->camera,owner->camera_tick,&mode)||mode!=2)return 0;
    ap->ready=1;ap->security=0;ap->state=2;
    return 1;
}

static int rvf_menu_status(pid_t expected){
    struct rvf_owner owner={0};
    int have=rvf_owner_read(&owner);
    if(expected&&(!have||owner.pid!=expected))return 1;
    int owned=have&&owner.phase!=RVF_EXITED;
    int live=owned&&wire_process(owner.pid,owner.tick,RVF_EXE);
    struct hot_state ap={0};
    int ready=live&&owner.phase==RVF_READY&&service_pid()==owner.pid&&rvf_credentials(&owner,&ap);
    const char *reason=have?owner.reason:"RVF_IDLE";
    if(owned&&!live)reason="RVF_RESTORE_UNCONFIRMED";
    char url[80]="",ssid[66]="",password[128]="";
    if(ready){snprintf(url,sizeof url,"http://%s:%d/",ap.ip,WIFI_PORT);hot_hex(ap.ssid,ssid);hot_hex(ap.password,password);}
    printf("NXL_WIFI3\nrunning=%d\nconnected=%d\nip=%s\nurl=%s\nreason=%s\nmode=hotspot\nap_valid=%d\nap_state=%d\nowned=%d\nssid_hex=%s\npassword_hex=%s\nsecurity=%s\nend=NXL_WIFI_END3\n",
        ready,ready,ready?ap.ip:"",url,reason,ready,ready?ap.state:-1,owned,ssid,password,
        !ready?"unknown":ap.security==0?"none":"wpa2");
    return 0;
}

static int rvf_service_lock(void){
    int fd=open(WIFI_TMP "/wifi-rvf-owner.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}
    return fd;
}

static int rvf_radio_command(const char *action,unsigned timeout){
    static const char radio[]="/opt/usr/nx-lut-lite/bin/nxl-wifi-radio";
    pid_t child=fork();
    if(child<0)return 0;
    if(!child){execl(radio,radio,action,(char *)NULL);_exit(127);}
    unsigned long long until=monotime()+timeout;
    int status;
    for(;;){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child)return WIFEXITED(status)&&!WEXITSTATUS(status);
        if(result<0&&errno!=EINTR)return 0;
        if(monotime()>=until){kill(child,SIGTERM);return 0;}
        poll(NULL,0,60);
    }
}

static int rvf_finish(const char *reason,int srw,int radio_requested){
    int original_restored=rvf_restore_native(),radio_restored=!radio_requested;
    if(srw>=0)close(srw);
    if(original_restored&&radio_requested&&rvf_owner_write(RVF_RADIO_RESTORE,"RVF_RESTORING")){
        radio_restored=rvf_radio_command("--stop-rvf",32000);
        rvf_stage("RADIO_RESTORE",radio_restored,0,0);
    }
    int restored=original_restored&&radio_restored;
    int journal=rvf_owner_write(restored?RVF_EXITED:RVF_UNCONFIRMED,
        restored?reason:"RVF_RESTORE_UNCONFIRMED");
    rvf_log_result(reason,original_restored,radio_restored);
    rvf_tcp_save((long)native.pid,native.tick,rvf_verified_frequency);
    return restored&&journal?0:1;
}

static int rvf_confirm_frequency(void){
    unsigned long long until=monotime()+3000;
    do{
        rvf_frequency_mhz=wifi_frequency_mhz("uap0");
        if(wifi_frequency_is_24ghz(rvf_frequency_mhz))break;
        poll(NULL,0,180);
    }while(!stopping&&physical_usb()==0&&monotime()<until);
    rvf_stage("ACTIVE_FREQUENCY",rvf_frequency_mhz,2400,0);
    if(!wifi_frequency_is_24ghz(rvf_frequency_mhz))return 0;
    rvf_stage("ACTIVE_PROFILE_STOCK_24",1,rvf_frequency_mhz,0);
    rvf_verified_frequency=rvf_frequency_mhz;
    return 1;
}

static int rvf_serve(const char *root,const char *window){
    setvbuf(stdout,NULL,_IOLBF,0);
    char *end;errno=0;unsigned long value=strtoul(window,&end,10);
    if(errno||!*window||*end||value<=1||value>UINT32_MAX)return 1;
    int lock=rvf_service_lock();
    if(lock<0){fputs("ORIGINAL_NETWORK_BUSY\n",stderr);return 1;}
    int srw=run_guard();
    if(srw<0){close(lock);fputs("SRW_BUSY\n",stderr);return 1;}
    if(!rvf_preflight((uint32_t)value)){
        rvf_stage(why,0,0,0);
        fprintf(stderr,"RVF_PREFLIGHT %s\n",why);close(srw);close(lock);return 1;
    }
    int prepared=!stopping&&rvf_radio_command("--stock-ready",2500);
    rvf_stage("STOCK_RADIO_READY",prepared,stopping,0);
    native.ap_up=rvf_ap_up();
    int camera_ready=prepared&&native.ap_up>=0&&rvf_native_idle(&native)&&
        rvf_owner_write(RVF_ENTERING,"RVF_OPENING_ORIGINAL_GUI");
    if(!prepared)why="RVF_STOCK_RADIO_UNCONFIRMED";
    else if(!camera_ready)why="ORIGINAL_GUI_NOT_READY";
    if(camera_ready)rvf_stage("OPEN_UI",1,0,0);
    int ready=0;
    if(!stopping&&camera_ready&&rvf_ui_command(NXL_RVF_UI_OPEN)&&rvf_wait_receiver()){
        if(!rvf_confirm_frequency())why="RVF_STOCK_24_UNCONFIRMED";
        else {
            struct hot_state ap;
            unsigned long long until=monotime()+5000;
            while(!stopping&&physical_usb()==0&&monotime()<until){
                if(rvf_credentials(&native,&ap)){ready=1;break;}
                poll(NULL,0,180);
            }
            rvf_stage("HOTSPOT_QR_INPUT",ready,ready?ap.security:-1,0);
            if(!ready)why="RVF_HOTSPOT_IDENTITY_UNCONFIRMED";
        }
        if(ready&&!stopping){
            int probed=rvf_radio_command("--read-rvf-power",2200);
            rvf_stage("RF_POWER_PROBE",probed,rvf_verified_frequency,0);
        }
        if(ready&&!stopping){
            if(rvf_owner_write(RVF_READY,"RVF_RUNNING"))serve(root);
            else why="RVF_JOURNAL_FAILED";
        }
    }
    if(stopping)why="RVF_STOP_REQUESTED";
    else if(!ready&&!strcmp(why,"STOPPED"))why="RVF_OPEN_REQUEST_FAILED";
    int rc=rvf_finish(why,srw,0);
    close(lock);return rc;
}

static int rvf_menu_stop(void){
    struct rvf_owner owner;
    if(!rvf_owner_read(&owner)){
        struct stat st;return !lstat(RVF_OWNER,&st)||errno!=ENOENT;
    }
    if(owner.phase==RVF_EXITED)return 0;
    if(wire_process(owner.pid,owner.tick,RVF_EXE)){
        kill(owner.pid,SIGTERM);
        unsigned long long deadline=monotime()+RVF_STOP_MS;
        while(wire_process(owner.pid,owner.tick,RVF_EXE)&&monotime()<deadline)poll(NULL,0,150);
        if(wire_process(owner.pid,owner.tick,RVF_EXE))return 1;
        if(!rvf_owner_read(&owner))return 1;
        return owner.phase!=RVF_EXITED;
    }
    /* If the supervisor ended during exit, let the physical Wi-Fi action
     * finish that same owned camera session. Check the owner around the lock. */
    int lock=rvf_service_lock();
    if(lock<0)return 1;
    struct rvf_record current;
    if(!rvf_owner_read(&owner)||wire_process(owner.pid,owner.tick,RVF_EXE)||
       (rvf_record_read(&current)&&current.server==owner.pid&&current.server_tick==owner.tick&&
        wire_process(current.pid,current.tick,RVF_EXE))){close(lock);return 1;}
    native=owner;native.pid=getpid();native.tick=proc_ticks(native.pid);
    int rc=rvf_finish("RVF_STOP_RECOVERED",-1,1);
    close(lock);return rc;
}

static const char *rvf_web_end(unsigned long long now){
    if(native_close_at&&now>=native_close_at)return "RVF_BROWSER_CLOSED";
    if(!native_viewer_seen&&now-native_ready_at>=RVF_JOIN_MS)return "RVF_NO_BROWSER";
    if(native_viewer_seen&&now-native_viewer_seen>=RVF_IDLE_MS)return "RVF_BROWSER_TIMED_OUT";
    int exit_status;
    if(receiver&&waitpid(receiver,&exit_status,WNOHANG)==0)return NULL;
    receiver=0;
    struct rvf_record record;
    if(rvf_record_read(&record)&&record.server==native.pid&&record.server_tick==native.tick&&record.reason[0]){
        rvf=record;return rvf.reason;
    }
    return "RVF_RECEIVER_EXITED";
}
