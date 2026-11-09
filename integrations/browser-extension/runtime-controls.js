(() => {
  const presets={
    strong:{horizon:6,beam:2,duration:1,delay:0},
    stronger:{horizon:12,beam:8,duration:1,delay:0},
    capable:{horizon:6,beam:2,duration:3,delay:1},
    clumsy:{horizon:6,beam:2,duration:6,delay:1,switchWeight:.1},
    weak:{horizon:6,beam:2,duration:12,delay:2,switchWeight:2,pickup:6,attack:2,edge:0,danger:0,density:0}
  };
  const settings={enabled:false,strength:'strong',team:0,horizon:6,beam:2,duration:1,delay:0,
    switchWeight:.025,pickup:3,attack:.2,edge:1,edgeDistance:80,danger:10,density:.1,autoBomb:true};
  let initialized=false,dirty=true;
  const runtime=()=>globalThis.__eaglerAiModule??globalThis.Module;
  const symbol=name=>{
    const core=runtime();
    return core?.['_eagler_ai_'+name]??core?.['_th06_ai_'+name];
  };
  const call=(name,...args)=>symbol(name)?.(...args);
  const ready=()=>typeof symbol('metric')==='function';
  function syncStrength(){
    const matches=preset=>Object.entries(preset).every(([key,value])=>settings[key]===value);
    if(presets[settings.strength]&&matches(presets[settings.strength]))return;
    settings.strength=Object.entries(presets).find(([,preset])=>matches(preset))?.[0]??'custom';
  }
  function apply(){
    if(!ready())return;
    call('follow_attack',+(settings.strength==='weak'));
    call('team_mode',settings.team);
    call('configure',settings.horizon,settings.beam,40000);
    call('action_duration',settings.duration);
    call('input_delay',settings.delay);
    call('switch_weight',settings.switchWeight);
    call('objectives',settings.pickup,settings.attack);
    call('edges',settings.edge,settings.edgeDistance);
    call('danger',settings.danger);call('density',settings.density);
    call('auto_bomb',+settings.autoBomb);
    call('enable',+settings.enabled);
    initialized=true;dirty=false;
  }
  function snapshot(){
    if(ready()&&initialized&&!dirty)settings.enabled=!!call('metric',0);
    const configuredSeat=runtime()?.eaglerOptions?.netplayPlayer;
    return {ok:true,ready:ready(),settings:{...settings},active:ready()&&!!call('metric',1),
      seat:Number.isInteger(configuredSeat)&&configuredSeat>=0?configuredSeat:ready()?call('metric',29):null,ms:ready()?call('metric',2):0,
      hazards:ready()?call('metric',4):0};
  }
  globalThis.__eaglerAiControls=request=>{
    if(request.action==='preset'){
      const preset=presets[request.value];if(!preset)return {ok:false,error:'未知实力档位'};
      settings.strength=request.value;Object.assign(settings,preset);dirty=true;apply();
    }else if(request.action==='enabled'){
      settings.enabled=!!request.value;dirty=true;apply();
    }else if(request.action==='team'){
      if(request.value!==0&&request.value!==1)return {ok:false,error:'未知配合方式'};
      settings.team=request.value;dirty=true;apply();
    }else if(request.action==='parameter'){
      const limits={horizon:[6,24],beam:[2,16],duration:[1,12],delay:[0,30],switchWeight:[0,2],
        pickup:[0,6],attack:[0,2],edge:[0,5],edgeDistance:[1,160],danger:[0,100],density:[0,2]};
      const bounds=limits[request.key];const value=Number(request.value);
      if(!bounds||!Number.isFinite(value)||value<bounds[0]||value>bounds[1])return {ok:false,error:'参数超出范围'};
      settings[request.key]=value;syncStrength();dirty=true;apply();
    }
    return snapshot();
  };
  const badge=document.createElement('button');
  badge.style.cssText='position:fixed;right:8px;top:8px;z-index:2147483647;padding:6px 10px;background:#172638dc;color:#fff;border:1px solid #7790aa;border-radius:5px;font:12px sans-serif;cursor:pointer';
  badge.title='只接管自己的联机座位；点击切换，扩展图标可选择实力';
  badge.onclick=()=>{
    globalThis.__eaglerAiControls({action:'enabled',value:!settings.enabled});
    runtime()?.canvas?.focus();
  };
  document.body.append(badge);
  setInterval(()=>{
    if(dirty)apply();
    const state=snapshot();
    badge.textContent=!state.ready?'AI 加载中':`${settings.enabled?'AI 接管':'手动操作'}${globalThis.__eaglerAiModule?'':' · F8'}`;
  },250);
})();
