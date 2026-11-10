/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_BROWSER_H
#define NXL_BROWSER_H
static int total_count,view[512],category,strength=100,active_strength=100;
static int favorite_count,strength_support;
static char favorites[512][81],cursors[8][81],ui_path[1024];
static char srw_lut_id[81]="off";
static unsigned srw_gamma_mode=1;
static const char *group_ids[]={"all","favorites","portrait","cinema","landscape","mono","retro","other"};
static const char *group_names[]={"全部","收藏","人像","电影","风景","黑白","复古","其他"};
static const struct nxl_look_info *info_for(const char *id) {
    for(unsigned i=0;i<sizeof nxl_look_infos/sizeof nxl_look_infos[0];++i)
        if(!strcmp(id,nxl_look_infos[i].id))return &nxl_look_infos[i];
    return &nxl_unknown_info;
}
static const char *selected_id(void) {
    return selected>0 && selected<=count?names[view[selected-1]]:"";
}
static int favorite_index(const char *id) {
    for(int i=0;i<favorite_count;++i)if(!strcmp(id,favorites[i]))return i;
    return -1;
}
static int strength_ok(int s) {return s==25 || s==50 || s==75 || s==100;}
static void browser_remember(void) {
    strcpy(cursors[category],selected==0?"#off":selected==count+1?"#video":selected_id());
}
static void browser_rebuild(const char *wanted) {
    char target[81];snprintf(target,sizeof target,"%s",wanted?wanted:"");count=0;
    for(int pass=0;pass<(category==0?2:1);++pass)for(int i=0;i<total_count;++i) {
        int fav=favorite_index(names[i])>=0;
        const struct nxl_look_info *info=info_for(names[i]);
        if(category==0 && fav!=(pass==0))continue;
        if(category==1 && !fav)continue;
        if(category>=2 && strcmp(info->group,group_ids[category]))continue;
        view[count++]=i;
    }
    selected=count?1:0;
    if(!strcmp(target,"#off"))selected=0;
    else if(!strcmp(target,"#video"))selected=count+1;
    else for(int i=0;i<count;++i)if(!strcmp(target,names[view[i]]))selected=i+1;
}
static void browser_load(void) {
    const char *dir=getenv("NX_LUT_UI_STATE");
    if(!dir || dir[0]!='/' || strlen(dir)>1000)dir="/mnt/mmc/nx-lut-lite-ui";
    snprintf(ui_path,sizeof ui_path,"%s",dir);
    char path[1100],line[200],id[81];int value,n=0;
    snprintf(path,sizeof path,"%s/settings.txt",ui_path);
    FILE *f=fopen(engine,"r");
    if(f) {
        for(int i=0;i<64 && fgets(line,sizeof line,f);++i)
            if(!strcmp(line,"# NX_LUT_STRENGTH_PRESETS_V1\n"))strength_support=1;
        fclose(f);
    }
    const char *root=getenv("NX_LUT_ROOT");
    snprintf(path,sizeof path,"%s/bin/nxl-tablecheck.arm",root?root:"");
    if(access(path,X_OK))strength_support=0;
    snprintf(path,sizeof path,"%s/settings.txt",ui_path);
    f=fopen(path,"r");if(!f)return;
    if(!fgets(line,sizeof line,f) || strcmp(line,"NX_LUT_UI_V1\n")) {fclose(f);return;}
    while(n++<4096 && fgets(line,sizeof line,f)) {
        if(sscanf(line,"group %d",&value)==1 && value>=0 && value<8)category=value;
        else if(sscanf(line,"strength %d",&value)==1 && strength_ok(value))strength=value;
        else if(sscanf(line,"cursor %d %80s",&value,id)==2 && value>=0 && value<8 &&
                (name_ok(id) || !strcmp(id,"#off") || !strcmp(id,"#video")))strcpy(cursors[value],id);
        else if(sscanf(line,"favorite %80s",id)==1 && name_ok(id) && favorite_index(id)<0 && favorite_count<512)
            strcpy(favorites[favorite_count++],id);
        else if(sscanf(line,"srw_lut %80s",id)==1 && name_ok(id))strcpy(srw_lut_id,id);
        else if(sscanf(line,"srw_gamma %d",&value)==1 && value>=0 && value<=2)srw_gamma_mode=(unsigned)value;
    }
    fclose(f);
}
static int browser_save(void) {
    browser_remember();
    if(mkdir(ui_path,0700)<0 && errno!=EEXIST)return 0;
    char path[1100];snprintf(path,sizeof path,"%s/settings.txt",ui_path);
    /* The menu lock serializes saves. Avoid repeated SD leaf replacement. */
    int fd=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);if(fd<0)return 0;struct stat st;
#if defined(__arm__) && defined(__GLIBC__)
    extern int __fxstat(int,int,struct stat*);int regular=!__fxstat(3,fd,&st)&&S_ISREG(st.st_mode);
#else
    int regular=!fstat(fd,&st)&&S_ISREG(st.st_mode);
#endif
    if(!regular||ftruncate(fd,0)){close(fd);return 0;}FILE *f=fdopen(fd,"w");if(!f){close(fd);return 0;}
    fprintf(f,"NX_LUT_UI_V1\ngroup %d\nstrength %d\n",category,strength);
    for(int i=0;i<8;++i)if(cursors[i][0])fprintf(f,"cursor %d %s\n",i,cursors[i]);
    for(int i=0;i<favorite_count;++i)fprintf(f,"favorite %s\n",favorites[i]);
    fprintf(f,"srw_lut %s\n",srw_lut_id);
    fprintf(f,"srw_gamma %u\n",srw_gamma_mode);
    int failed=ferror(f);if(fflush(f)||fsync(fileno(f)))failed=1;if(fclose(f))failed=1;
    if(failed)return 0;
    return 1;
}
static int browser_favorite(void) {
    const char *id=selected_id();if(!id[0])return 0;
    char keep[81];strcpy(keep,id);int index=favorite_index(id);
    if(index<0) {
        if(favorite_count==512)return -1;
        strcpy(favorites[favorite_count++],id);
    } else {memmove(favorites+index,favorites+index+1,(unsigned)(favorite_count-index-1)*81);--favorite_count;}
    if(!browser_save()) {
        if(index<0)--favorite_count;else strcpy(favorites[favorite_count++],keep);
        return -1;
    }
    browser_rebuild(keep);return 1;
}
static int browser_has_strengths(void) {
    const char *id=selected_id(),*looks=getenv("NX_LUT_LOOKS");
    if(!id[0] || !strength_support)return 0;
    if(!looks || looks[0]!='/')looks="/mnt/mmc/looks";
    char path[1200];
    const char *files[]={"source.bin","25.bin","25.fnv","50.bin","50.fnv","75.bin","75.fnv"};
    for(unsigned i=0;i<sizeof files/sizeof files[0];++i) {
        snprintf(path,sizeof path,"%s/nxl-strengths/%s/%s",looks,id,files[i]);
        if(access(path,R_OK))return 0;
    }
    return 1;
}
static int effective_strength(void) {return browser_has_strengths()?strength:100;}
static void browser_group(int next) {
    browser_remember();category=(next+8)%8;browser_rebuild(cursors[category]);
}
#endif
