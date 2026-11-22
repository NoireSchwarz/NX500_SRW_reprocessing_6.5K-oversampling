/* SPDX-License-Identifier: AGPL-3.0-only. Read-only phase indicator; no radio writes. */
'use strict';
(()=>{
 const panel=document.getElementById('abaStatus');
 const names=['未安排對照','等候熱點／傳輸服務','等候實際傳輸','原值對照 A1','暫停 Action 轉交 B','恢復原值對照 A2','對照完成','對照已中止','恢復未確認','對照未執行'];
 const reasons=['','啟動檔無效','程序或介面已改變','傳輸計數不可用','自動速率未確認','原值已關閉 Action 轉交','原值未完整保存','傳輸覆蓋不足','讀回未通過','偵測到其他設定','原值恢復未確認','已停止圖傳','等待逾時'];
 const history=[];let timer=0,stopped=false;
 function valid(s){return s&&s.kind==='NXABA1'&&s.valid===true&&Number.isInteger(s.state)&&s.state>=0&&s.state<=9&&Number.isInteger(s.reason)&&s.reason>=0&&s.reason<reasons.length&&[s.restored,s.changed,...(s.stage_valid||[])].every(v=>v===0||v===1)&&Array.isArray(s.body_bytes)&&s.body_bytes.length===3&&s.body_bytes.every(v=>Number.isSafeInteger(v)&&v>=0)&&Array.isArray(s.net_bytes)&&s.net_bytes.length===3&&s.net_bytes.every(v=>Number.isSafeInteger(v)&&v>=0)&&Array.isArray(s.stage_valid)&&s.stage_valid.length===3&&Number.isSafeInteger(s.elapsed_ms)&&s.elapsed_ms>=0;}
 async function poll(){
  try{
   const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),3500);
   let s;try{const r=await fetch('/api/v1/linktest/status',{cache:'no-store',signal:controller.signal});if(!r.ok)throw Error();s=await r.json();}finally{clearTimeout(timeout);}
   if(!valid(s))throw Error();
   const row={browserMs:performance.now(),...s};history.push(row);if(history.length>240)history.shift();
   let text=names[s.state];
   if(s.state>=3&&s.state<=5){const i=s.state-3;text+=` · ${(s.elapsed_ms/1000).toFixed(0)} 秒 · 有效內容 ${(s.body_bytes[i]/1048576).toFixed(1)} MB`;}
   if(s.state===6)text=s.restored&&s.stage_valid.every(v=>v===1)?'對照完成 · 原值已讀回核驗，可下載測試記錄':'對照結果未完整確認，請保留現場';
   else if(s.reason)text+=' · '+reasons[s.reason];
   if(s.state===7&&s.changed)text+=s.restored?' · 原值已恢復':' · 等候停止時恢復';
   panel.textContent=text;panel.dataset.state=String(s.state);
  }catch(e){panel.textContent='對照狀態暫不可讀，請保持連線；這不代表已完成';panel.dataset.state='unknown';}
  if(!stopped)timer=setTimeout(poll,2000);
 }
 window.nxABAMeasurement=()=>({kind:'NX500_MGMT_ABA_BROWSER1',history:history.map(r=>({...r})),clock:'browserMs uses the same performance.now clock as frame rows; camera milliseconds are not synchronized',meaning:'Application socket body bytes and interface TX bytes; not RF ACK or delivered-pixel proof'});
 window.addEventListener('pagehide',()=>{stopped=true;clearTimeout(timer);});poll();
})();
