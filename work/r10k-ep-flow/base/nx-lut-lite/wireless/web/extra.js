/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only overview inspired by NXConnectForNX500 (Katsiy, AGPL).
 */
'use strict';
const rvfView=byId('view'),rvfContext=rvfView.getContext('2d');
const packetHeaderBytes=24,maxJPEGBytes=512*1024;
let token='',wanted=false,started=false,request=null,retryTimer=0,retries=0;
let newest=null,drawing=false,displayEpoch=0,lastReceived=0,nextMetrics=0;
let firstReceived=0,lastReceivedAt=0,firstDrawn=0,lastDrawnAt=0;
const receiveTimes=[],drawTimes=[];
const measurements={
 kind:'NX500_STOCK_RVF_PUSH1',started:new Date().toISOString(),frequency_mhz:0,
 native_fps:0,received:0,drawn:0,jpeg_bytes:0,missing_frame_ids:0,
 skipped_before_draw:0,stream_connections:0,dropped:0,rows:[]
};

function rvfHeaders(value=token){return value?{'X-NX-Session':value}:{};}
function rvfCommand(command,session=''){
 return fetch('/api/v1/rvf/'+command,{method:'POST',cache:'no-store',keepalive:true,
   headers:{'X-NX-Request':'rvf',...rvfHeaders(session)}});
}

function stopNative(message){
 wanted=false;
 newest=null;
 clearTimeout(retryTimer);
 if(request)request.abort();
 if(token){const session=token;token='';rvfCommand('stop',session).catch(()=>{});}
 if(message)say(message);
}

async function startNative(){
 if(started)return;
 started=true;wanted=true;
 say('正在連接原廠即時圖傳…');
 try{
  const r=await rvfCommand('start');
  if(!r.ok)throw Error(r.status===429?'另一個瀏覽器已開啟圖傳。':'相機圖傳尚未就緒。');
  const job=await r.json();
  if(!/^[0-9a-f]{32}$/.test(job.session))throw Error('無法確認圖傳連線。');
  token=job.session;
  if(!wanted){stopNative();return;}
  if(!document.hidden)openPush();
 }catch(e){wanted=false;say(e.message+' 請用相機上的 Wi-Fi 鍵重新開啟。');}
}

async function decodeJPEG(blob){
 if(typeof createImageBitmap==='function'){
  try{return await createImageBitmap(blob);}catch{}
 }
 const url=URL.createObjectURL(blob);
 try{return await new Promise((ok,fail)=>{
  const img=new Image();img.onload=()=>ok(img);img.onerror=fail;img.src=url;
 });}finally{URL.revokeObjectURL(url);}
}

function frameRate(count,first,last){
 return count>1&&last>first?(count-1)*1000/(last-first):0;
}

function liveRate(times,now){
 while(times.length&&now-times[0]>3000)times.shift();
 const fps=frameRate(times.length,times[0],times[times.length-1]);
 return fps?fps.toFixed(1):'—';
}

function showRates(now){
 if(now<nextMetrics)return;
 nextMetrics=now+250;
 const mhz=measurements.frequency_mhz;
 const band=mhz?mhz+' MHz':'頻率未知';
 const source=measurements.native_fps?measurements.native_fps.toFixed(1):'—';
 byId('metrics').textContent='720 × 480 · '+band+' · 原廠 '+source+
  ' / 接收 '+liveRate(receiveTimes,now)+' / 繪製 '+liveRate(drawTimes,now)+' fps';
}

async function drawNewest(){
 if(drawing)return;
 drawing=true;
 try{
  while(wanted&&newest){
   const frame=newest;newest=null;
   const begin=performance.now();
   const image=await decodeJPEG(new Blob([frame.jpeg],{type:'image/jpeg'}));
   try{
    if(!wanted||document.hidden||frame.epoch!==displayEpoch)continue;
    if((image.width||image.naturalWidth)!==720||(image.height||image.naturalHeight)!==480)
     throw Error('原廠 JPEG 尺寸不符。');
    rvfContext.drawImage(image,0,0);
   }finally{if(image.close)image.close();}
   const now=performance.now();
   if(!measurements.drawn)firstDrawn=now;
   measurements.drawn++;lastDrawnAt=now;drawTimes.push(now);
   measurements.rows.push({id:frame.id,t:now,received_t:frame.received,bytes:frame.jpeg.length,
    width:720,height:480,source_age_ms:frame.age,frequency_mhz:frame.mhz,
    native_fps:frame.fps,draw_ms:now-begin});
   if(measurements.rows.length>1800){measurements.rows.shift();measurements.dropped++;}
   showRates(now);
  }
 }catch{
  stopNative('無法解碼原廠畫面，圖傳正在停止。');
 }finally{drawing=false;}
}

function queueFrame(frame){
 if(frame.id<=lastReceived)return;
 if(lastReceived)measurements.missing_frame_ids+=frame.id-lastReceived-1;
 const now=performance.now();
 if(!measurements.received){firstReceived=now;say('即時圖傳中');}
 lastReceived=frame.id;lastReceivedAt=now;retries=0;
 measurements.received++;measurements.jpeg_bytes+=frame.jpeg.length;
 measurements.frequency_mhz=frame.mhz;measurements.native_fps=frame.fps;
 receiveTimes.push(now);
 if(newest)measurements.skipped_before_draw++;
 newest={...frame,received:now,epoch:displayEpoch};
 showRates(now);
 drawNewest();
}

async function readPackets(reader,session,signal,markProgress){
 const header=new Uint8Array(packetHeaderBytes),fields=new DataView(header.buffer);
 let headerUsed=0,frame=null,jpegUsed=0;
 while(wanted&&session===token&&!signal.aborted){
  const {value,done}=await reader.read();
  if(done){
   if(headerUsed||frame)throw Error('圖傳資料中斷。');
   return;
  }
  if(signal.aborted||session!==token)return;
  for(let offset=0;offset<value.length;){
   if(!frame){
    const count=Math.min(packetHeaderBytes-headerUsed,value.length-offset);
    header.set(value.subarray(offset,offset+count),headerUsed);
    headerUsed+=count;offset+=count;
    if(headerUsed!==packetHeaderBytes)continue;
    const size=fields.getUint32(4),id=fields.getUint32(8);
    if(fields.getUint32(0)!==0x4e584a31||size<16||size>maxJPEGBytes||!id)
     throw Error('原廠圖傳資料格式不符。');
    frame={id,age:fields.getUint32(12),mhz:fields.getUint32(16),
     fps:fields.getUint32(20)/1000,jpeg:new Uint8Array(size)};
    headerUsed=0;jpegUsed=0;
   }
   const count=Math.min(frame.jpeg.length-jpegUsed,value.length-offset);
   frame.jpeg.set(value.subarray(offset,offset+count),jpegUsed);
   jpegUsed+=count;offset+=count;
   if(jpegUsed===frame.jpeg.length){
    markProgress();
    queueFrame(frame);frame=null;
   }
  }
 }
}

async function openPush(){
 if(!wanted||!token||document.hidden||request)return;
 clearTimeout(retryTimer);
 const session=token,ctrl=new AbortController();request=ctrl;
 let progress=performance.now();
 const timeout=setInterval(()=>{if(performance.now()-progress>6000)ctrl.abort();},1000);
 let reader=null;
 try{
  const r=await fetch('/api/v1/rvf/stream',{cache:'no-store',signal:ctrl.signal,headers:rvfHeaders(session)});
  if(!wanted||session!==token)return;
  if(r.status===403){stopNative('相機已停止圖傳。');return;}
  if(!r.ok)throw Error('圖傳連線暫時無法使用。');
  if(!r.body||typeof r.body.getReader!=='function'){
   stopNative('此瀏覽器無法接收即時圖傳。');return;
  }
  measurements.stream_connections++;
  reader=r.body.getReader();
  await readPackets(reader,session,ctrl.signal,()=>{progress=performance.now();});
  if(wanted&&session===token&&!ctrl.signal.aborted)stopNative('相機已停止圖傳。');
 }catch{
  if(wanted&&session===token&&!document.hidden&&retries++>=1){
   stopNative('圖傳連線中斷。請用相機 Wi-Fi 鍵重新開啟。');
  }
 }finally{
  clearInterval(timeout);
  ctrl.abort();
  if(reader)reader.releaseLock();
  request=null;
  if(wanted&&token&&!document.hidden)retryTimer=setTimeout(openPush,400);
 }
}
byId('pause').onclick=()=>stopNative('正在停止圖傳與相機熱點…');
byId('downloadStats').onclick=()=>{
 const report={...measurements,
  browser_receive_fps:frameRate(measurements.received,firstReceived,lastReceivedAt),
  browser_draw_fps:frameRate(measurements.drawn,firstDrawn,lastDrawnAt)};
 const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));
 const link=document.createElement('a');link.href=url;link.download='NX500-stock-rvf-'+Date.now()+'.json';
 link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
window.addEventListener('pagehide',()=>stopNative());
document.addEventListener('visibilitychange',()=>{
 if(document.hidden){
  clearTimeout(retryTimer);newest=null;displayEpoch++;
  receiveTimes.length=drawTimes.length=0;
  if(request)request.abort();
 }else if(wanted&&token)openPush();
});

function infoRow(label,value){const tr=document.createElement('tr'),th=document.createElement('th'),td=document.createElement('td');th.textContent=label;td.textContent=value===null||value===undefined||value===''?'未知／未提供':String(value);tr.append(th,td);return tr;}
async function refreshOverview(){
 const start=Date.now();byId('infoRefresh').disabled=true;
 try{const r=await fetch('/api/v1/camera/info',{cache:'no-store'});if(!r.ok)throw Error();const i=await r.json();
  const number=n=>Number.isFinite(n)&&n>=0;
  const bytes=n=>number(n)?(n/1073741824).toFixed(2)+' GiB':null;
  const time=number(i.camera_epoch_seconds)?new Date(i.camera_epoch_seconds*1000).toISOString():null;
  const drift=time?((i.camera_epoch_seconds*1000-(start+Date.now())/2)/1000).toFixed(1)+' 秒（相機時間戳 − 此裝置）':null;
  const rows=[['機型',i.model],['原廠韌體',i.fw_ver],['模組版本',i.version],['電量',number(i.battery_percent)?i.battery_percent+'%':null],['連線頻率',i.frequency_mhz>0?i.frequency_mhz+' MHz':null],['相機 IP',i.ip],['MAC',i.mac],['本次核心運行時間',number(i.uptime_seconds)?Math.floor(i.uptime_seconds)+' 秒':null],['相機系統時間（UTC）',time],['時差估計',drift],['SD 可用／總量',i.sd_valid?bytes(i.sd_available_bytes)+' / '+bytes(i.sd_total_bytes):null],['圖傳模組',i.transport]];
  byId('infoRows').replaceChildren(...rows.map(r=>infoRow(...r)));byId('infoNote').textContent='時差估計包含網路延遲。';
 }catch(e){byId('infoNote').textContent='資訊讀取失敗，請確認相機仍連線。';}finally{byId('infoRefresh').disabled=false;}
}

landingReady();

const player = byId('viewPlayer');
if(new URLSearchParams(location.search).get('view') === 'rvf')document.body.classList.add('rvf-page');
const fullscreenButton = byId('viewFullscreen');
let fullscreenBusy = false;

function fullscreenElement(){
 return document.fullscreenElement || document.webkitFullscreenElement;
}

function updateFullscreenButton(){
 const expanded = fullscreenElement() === player || player.classList.contains('view-expanded');
 fullscreenButton.textContent = expanded ? '退出全螢幕' : '全螢幕';
 fullscreenButton.setAttribute('aria-pressed', String(expanded));
}

function setExpandedView(expanded){
 player.classList.toggle('view-expanded', expanded);
 document.body.classList.toggle('view-fixed', expanded);
 updateFullscreenButton();
}

fullscreenButton.addEventListener('click', async ()=>{
 if(fullscreenBusy)return;
 fullscreenBusy = true;
 try{
  if(fullscreenElement() === player){
   const exit = document.exitFullscreen || document.webkitExitFullscreen;
   if(exit){
    try{await exit.call(document);}catch{}
   }
  }else if(player.classList.contains('view-expanded')){
   setExpandedView(false);
  }else{
   const enter = player.requestFullscreen || player.webkitRequestFullscreen;
   if(enter && document.fullscreenEnabled !== false){
    try{
     await enter.call(player);
     return;
    }catch{}
   }
   setExpandedView(true);
  }
 }finally{
  fullscreenBusy = false;
  updateFullscreenButton();
 }
});

document.addEventListener('fullscreenchange', updateFullscreenButton);
document.addEventListener('webkitfullscreenchange', updateFullscreenButton);
document.addEventListener('keydown', event=>{
 if(event.key === 'Escape' && player.classList.contains('view-expanded'))setExpandedView(false);
});
