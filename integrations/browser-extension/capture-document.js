(() => {
  if(window===window.top)return;
  const url=new URL(location.href);
  if(url.searchParams.get('hosted')!=='1'||url.searchParams.get('runtimeVariant')!=='multiplayer')return;
  const match=url.pathname.match(/\/runtime\/(th(?:08|10|11|12|15))\/multiplayer\/(?:([a-f0-9]{64})\/)?[^/]+\.html$/);
  if(match){
    const [,game,generation]=match;
    if(!generation&&!['localhost','127.0.0.1'].includes(url.hostname))return;
    const prefix=`runtime/modules/${game}/`;
    const bridge=prefix+(generation?`bridge-${generation}.mjs`:'bridge-local.mjs');
    const resources=(chrome.runtime.getManifest().web_accessible_resources??[]).flatMap(entry=>entry.resources);
    if(!resources.includes(bridge))return;
    const module=resources.find(file=>file.startsWith(prefix)&&/\/(?:th\d+(?:-sdl)?|core)\.mjs$/.test(file));
    if(!module)return;
    const install=()=>{
      const map=document.createElement('script');
      map.type='importmap';
      map.textContent=JSON.stringify({imports:{
        [new URL(module.split('/').pop(),url).href]:chrome.runtime.getURL(bridge),
      }});
      document.documentElement.prepend(map);
    };
    if(document.documentElement)install();
    else {
      const observer=new MutationObserver(()=>{
        if(!document.documentElement)return;
        observer.disconnect();
        install();
      });
      observer.observe(document,{childList:true});
    }
    return;
  }
  if(!/(?:^|\/)th06(?:-multiplayer)?(?:[/.\-]|$)/.test(url.pathname))return;
  let requested=false;
  window.addEventListener('message',event=>{
    if(requested||event.source!==window||event.origin!==location.origin||
       event.data?.protocol!=='eagler-ai-extension/1'||event.data.event!=='runtime-intercepted')return;
    requested=true;
    chrome.runtime.sendMessage({type:'boot'},response=>{
      const error=chrome.runtime.lastError?.message||response?.error;
      if(error){
        const status=document.getElementById('status');
        if(status)status.textContent=`AI 接管运行包加载失败：${error}。可关闭扩展后重新启动游戏。`;
        console.error('[Eagler AI]',error);
      }
    });
  });
})();
