const runtimeFrame=url=>{
  try{
    const u=new URL(url);
    return /^(?:(?:[^/]+\.)?(?:touhouonline\.com|touhou\.vip)|127\.0\.0\.1|localhost)$/.test(u.hostname)&&
      u.searchParams.get('hosted')==='1'&&u.searchParams.get('runtimeVariant')==='multiplayer'&&
      /(?:^|\/)th06(?:-multiplayer)?(?:[/.\-]|$)/.test(u.pathname);
  }catch{return false;}
};
let wasmPromise;
const wasmBase64=()=>wasmPromise??=(async()=>{
  const response=await fetch(chrome.runtime.getURL('runtime/th06.wasm'));
  if(!response.ok)throw Error('AI Wasm 文件缺失，请重新构建扩展');
  const bytes=new Uint8Array(await response.arrayBuffer());
  const chunks=[];
  for(let i=0;i<bytes.length;i+=0x8000)chunks.push(String.fromCharCode(...bytes.subarray(i,i+0x8000)));
  return btoa(chunks.join(''));
})().catch(error=>{wasmPromise=null;throw error;});

async function boot(sender){
  if(!sender.tab||!sender.frameId||!runtimeFrame(sender.url))throw Error('不支持的游戏页面');
  const target=sender.documentId?{tabId:sender.tab.id,documentIds:[sender.documentId]}:
    {tabId:sender.tab.id,frameIds:[sender.frameId]};
  await chrome.scripting.executeScript({target,world:'MAIN',func:()=>{
    if(!globalThis.__eaglerAiLoader?.intercepted||!globalThis.Module?.canvas?.isConnected)
      throw Error('主站运行页尚未准备好画布');
    globalThis.__eaglerAiLoader.phase='准备 AI 内核';
  }});
  const encoded=await wasmBase64();
  await chrome.scripting.executeScript({target,world:'MAIN',files:['prepare-runtime.js']});
  const prepared=await chrome.scripting.executeScript({target,world:'MAIN',func:()=>globalThis.__eaglerAiPrepare()});
  const result=prepared[0]?.result;
  if(!result?.ok)throw Error(result?.error||'无法读取主站已有游戏资源');
  await chrome.scripting.executeScript({target,world:'MAIN',func:base64=>{
    globalThis.__eaglerAiLoader.phase='启动 AI 内核';
    Module.wasmBinary=Uint8Array.from(atob(base64),character=>character.charCodeAt(0));
    const initialized=Module.onRuntimeInitialized;
    Module.onRuntimeInitialized=()=>{
      globalThis.__eaglerAiLoader.phase='内核已启动';
      initialized?.();
    };
    Module.onAbort=reason=>{
      globalThis.__eaglerAiStartupError=String(reason);
      document.getElementById('status').textContent=`AI 运行包错误：${reason}`;
    };
  },args:[encoded]});
  await chrome.scripting.executeScript({target,world:'MAIN',files:['runtime/th06.js']});
  await chrome.scripting.executeScript({target,world:'MAIN',files:['runtime-controls.js']});
  await chrome.action.setBadgeText({tabId:sender.tab.id,text:'AI'});
  return {ok:true};
}

async function control(message){
  const [tab]=await chrome.tabs.query({active:true,currentWindow:true});
  if(!tab?.id)throw Error('请先打开主站');
  const replies=await chrome.scripting.executeScript({target:{tabId:tab.id,allFrames:true},world:'MAIN',
    func:request=>{
      if(globalThis.__eaglerAiStartupError)return {ok:false,error:globalThis.__eaglerAiStartupError};
      if(!globalThis.__eaglerAiControls)return globalThis.__eaglerAiLoader?
        {ok:false,error:`AI 加载中：${globalThis.__eaglerAiLoader.phase}`} : null;
      return globalThis.__eaglerAiControls(request);
    },args:[message]});
  const reply=replies.find(entry=>entry.result)?.result;
  if(!reply)throw Error('请启用扩展后，在主站重新启动已适配的多人游戏');
  return reply;
}

async function reportBootFailure(sender,error){
  if(!sender.tab||!sender.frameId)return;
  const target=sender.documentId?{tabId:sender.tab.id,documentIds:[sender.documentId]}:
    {tabId:sender.tab.id,frameIds:[sender.frameId]};
  try{
    await chrome.scripting.executeScript({target,world:'MAIN',func:message=>{
      const phase=globalThis.__eaglerAiLoader?.phase;
      const detail=phase?`${phase}：${message}`:message;
      globalThis.__eaglerAiStartupError=detail;
      const status=document.getElementById('status');
      if(status)status.textContent=`AI 加载失败：${detail}`;
      console.error('[Eagler AI]',detail);
    },args:[error]});
  }catch{/* The host may already have retired this runtime document. */}
}

chrome.runtime.onMessage.addListener((message,sender,respond)=>{
  if(message?.type!=='boot'&&message?.type!=='control')return;
  if(message.type==='control'&&sender.url!==chrome.runtime.getURL('popup.html'))return;
  (message.type==='boot'?boot(sender):control(message)).then(respond,async error=>{
    const detail=String(error.message||error);
    if(message.type==='boot')await reportBootFailure(sender,detail);
    respond({ok:false,error:detail});
  });
  return true;
});
