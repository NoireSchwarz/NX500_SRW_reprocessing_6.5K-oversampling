/* SPDX-License-Identifier: AGPL-3.0-only. Names/stat only; photo bodies unopened. */
struct lens_photo_seen {char name[384];uint64_t bytes,device,inode;int64_t modified;long modified_ns;unsigned used;};
static struct lens_photo_seen lens_photos[8192];
static void photo_inventory(const char*phase){
 DIR*root=opendir("/mnt/mmc/DCIM");if(!root)return;struct dirent*folder;unsigned count=0;uint64_t begin=now();
 while((folder=readdir(root))&&count<4096){if(folder->d_name[0]=='.'||strchr(folder->d_name,'/'))continue;struct stat sd;
  if(fstatat(dirfd(root),folder->d_name,&sd,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(sd.st_mode))continue;
  char path[384];if(snprintf(path,sizeof path,"/mnt/mmc/DCIM/%s",folder->d_name)>=(int)sizeof path)continue;DIR*d=opendir(path);if(!d)continue;struct dirent*e;
  while((e=readdir(d))&&count<4096){if(e->d_name[0]=='.'||strchr(e->d_name,'/'))continue;struct stat st;
   if(fstatat(dirfd(d),e->d_name,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode))continue;count++;
   char full[384];if(snprintf(full,sizeof full,"%s/%s",path,e->d_name)>=(int)sizeof full)continue;
   unsigned slot=hash_bytes(full,strlen(full))%8192u,steps=0;while(lens_photos[slot].used&&strcmp(lens_photos[slot].name,full)&&steps++<8192)slot=(slot+1)%8192;
   struct lens_photo_seen*p=lens_photos+slot;int same=steps<8192&&p->used&&p->bytes==(uint64_t)st.st_size&&p->modified==(int64_t)st.st_mtime&&p->modified_ns==st.st_mtim.tv_nsec&&p->device==(uint64_t)st.st_dev&&p->inode==(uint64_t)st.st_ino;
   if(steps<8192){strcpy(p->name,full);p->bytes=(uint64_t)st.st_size;p->modified=(int64_t)st.st_mtime;p->modified_ns=st.st_mtim.tv_nsec;p->device=(uint64_t)st.st_dev;p->inode=(uint64_t)st.st_ino;p->used=1;}
   if(same&&!strcmp(phase,"poll"))continue;
   fprintf(events,"{\"event\":\"photo_inventory\",\"phase\":\"%s\",\"mono_ns\":%llu,\"path\":",phase,(unsigned long long)now());quoted(events,full);
   fprintf(events,",\"bytes\":%llu,\"mtime\":%lld,\"mtime_ns\":%ld,\"device\":%llu,\"inode\":%llu,\"body_opened\":false}\n",(unsigned long long)st.st_size,(long long)st.st_mtime,st.st_mtim.tv_nsec,(unsigned long long)st.st_dev,(unsigned long long)st.st_ino);
  }closedir(d);
 }closedir(root);
 fprintf(events,"{\"event\":\"inventory_scan\",\"begin_ns\":%llu,\"end_ns\":%llu,\"files\":%u,\"at_capacity\":%s}\n",(unsigned long long)begin,(unsigned long long)now(),count,count>=4096?"true":"false");
}
