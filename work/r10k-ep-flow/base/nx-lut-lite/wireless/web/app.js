/* SPDX-License-Identifier: AGPL-3.0-only */
'use strict';
const byId=id=>document.getElementById(id);
const say=message=>{byId('status').textContent=message;};
let tab='',folder='',next=-1,galleryGeneration=0;

async function loadGallery(dir,offset=0){
 const generation=offset?galleryGeneration:++galleryGeneration;if(!offset){byId('cards').replaceChildren();folder=dir;byId('folderName').textContent=dir||'DCIM';}
 byId('more').hidden=true;say('正在讀取相簿…');
 try{const r=await fetch('/api/v1/gallery/list?dir='+encodeURIComponent(dir)+'&offset='+offset,{cache:'no-store'});if(!r.ok)throw Error();const list=await r.json();if(generation!==galleryGeneration||tab!=='gallery')return;
  for(const item of list.items){
   const card=document.createElement('div');card.className='card';
   if(!dir){const button=document.createElement('button');button.className='folder';button.textContent=item.name;button.onclick=()=>loadGallery(item.name);card.append(button);}
   else{const path=encodeURIComponent(dir+'/'+item.name),url='/file?f='+path;
    if(/\.jpe?g$/i.test(item.name)){const preview=document.createElement('a');preview.href=url;preview.target='_blank';preview.rel='noopener';const img=document.createElement('img');img.loading='lazy';img.decoding='async';img.alt=item.name;img.src='/thumb?f='+path;img.onerror=()=>{img.removeAttribute('src');img.alt='無內嵌預覽';};preview.append(img);card.append(preview);}
    const label=document.createElement('span');label.textContent=item.name+' · '+(item.bytes/1048576).toFixed(1)+' MB';const download=document.createElement('a');download.href=url;download.download=item.name;download.textContent='下載原檔';card.append(label,download);
   }byId('cards').append(card);
  }next=list.next;byId('more').hidden=next<0;byId('empty').hidden=byId('cards').children.length>0;say(dir?'無線相簿 · '+dir:'請選擇照片資料夾');
 }catch(e){if(generation===galleryGeneration)say('相簿讀取失敗，請確認相機連線後重試');}
}

function showMedia(name){
 tab=name;
 byId('pageTabs').hidden=false;
 byId('gallery').hidden=name!=='gallery';
 byId('info').hidden=name!=='info';
 byId('galleryTab').classList.toggle('active',name==='gallery');
 byId('infoTab').classList.toggle('active',name==='info');
 if(name==='gallery')loadGallery('');
 else{refreshOverview();say('相機資訊');}
}
byId('galleryTab').onclick=()=>showMedia('gallery');
byId('infoTab').onclick=()=>showMedia('info');
byId('infoRefresh').onclick=()=>refreshOverview();
byId('folders').onclick=()=>loadGallery('');
byId('refresh').onclick=()=>loadGallery(folder);
byId('more').onclick=()=>{if(next>=0)loadGallery(folder,next);};

async function landingReady(){
 const query=new URLSearchParams(location.search).get('view');
 let entry=query;
 if(entry!=='rvf'&&entry!=='media'){
  try{
   const r=await fetch('/api/v1/camera/hello',{cache:'no-store'});
   const info=await r.json();
   entry=info.service==='rvf'?'rvf':'media';
  }catch{return say('找不到相機。請先連上相機 Wi-Fi，再掃描網頁碼。');}
 }
 if(entry==='rvf'){
  byId('title').textContent='NX500 原廠即時圖傳';
  byId('live').hidden=false;
  startNative();
 }else{
  byId('title').textContent='NX500 相簿與相機資訊';
  showMedia('gallery');
 }
}
