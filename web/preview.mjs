const $ = id => document.getElementById(id);
const games = {
    th08:{name:'永夜抄', characters:['结界组','咏唱组','红魔组','幽冥组','博丽灵梦','八云紫','雾雨魔理沙','爱丽丝','十六夜咲夜','蕾米莉亚','魂魄妖梦','西行寺幽幽子'], shots:1},
    th10:{name:'风神录', characters:['博丽灵梦','雾雨魔理沙'], shots:3},
    th11:{name:'地灵殿', characters:['博丽灵梦','雾雨魔理沙'], shots:3},
    th12:{name:'星莲船', characters:['博丽灵梦','雾雨魔理沙','东风谷早苗'], shots:2},
    th15:{name:'绀珠传', characters:['博丽灵梦','雾雨魔理沙','东风谷早苗','铃仙'], shots:1},
};
const protocol = 'eagler-touhou/1';
let session = null, nextEpoch = 0;
const requests = new Map();
for (const [id, game] of Object.entries(games)) $('title').add(new Option(game.name,id));
const requested = new URL(location.href).searchParams.get('game');
if (games[requested]) $('title').value = requested;
function selections() {
    const game = games[$('title').value];
    for (const id of ['human','partner']) {
        $(id).replaceChildren();
        game.characters.forEach((name,character)=>{
            for(let shot=0;shot<game.shots;++shot)
                $(id).add(new Option(name+(game.shots>1?' '+String.fromCharCode(65+shot):''), character+','+shot));
        });
    }
    $('partner').selectedIndex = game.shots;
}
selections();
$('title').onchange = selections;
function controls(message) {
    return $('game').contentWindow.__eaglerAiControls?.(message);
}
function focusGame() {
    $('game').contentWindow.focus();
    $('game').contentDocument.querySelector('canvas')?.focus();
}
$('focus').onclick = focusGame;
$('enabled').onchange = ()=>controls({action:'enabled',value:$('enabled').checked});
$('team').onchange = ()=>controls({action:'team',value:+$('team').value});
$('strength').onchange = ()=>{controls({action:'preset',value:$('strength').value});syncParameters();};
$('autoBomb').onchange = ()=>controls({action:'autoBomb',value:$('autoBomb').checked});
const parameters = [
    ['horizon','预判帧数',6,24,3],['beam','保留路线',2,16,2],
    ['duration','动作持续帧数',1,12,1],['delay','输入延迟帧数',0,30,1],
    ['switchWeight','切换动作惩罚',0,2,.005],['pickup','收点权重',0,6,.25],
    ['attack','输出站位权重',0,2,.05],['edge','边缘惩罚',0,5,.01],
    ['edgeDistance','边缘生效区域',1,160,1],['danger','危险距离惩罚',0,100,.5],
    ['density','弹幕密度惩罚',0,2,.01],
];
for(const [key,name,min,max,step] of parameters) {
    const label=document.createElement('label'), value=document.createElement('output'), slider=document.createElement('input');
    label.append(name+' ',value,slider);
    slider.type='range';slider.min=min;slider.max=max;slider.step=step;slider.id='param-'+key;value.id='value-'+key;
    slider.oninput=()=>{value.textContent=slider.value;controls({action:'parameter',key,value:+slider.value});};
    $('parameters').append(label);
}
function syncParameters() {
    const result=controls({action:'status'});
    if(!result?.ready)return;
    for(const [key] of parameters){$('param-'+key).value=result.settings[key];$('value-'+key).textContent=result.settings[key];}
}
function rpc(command,fields={}) {
    const current=session, request=crypto.randomUUID();
    return new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>{requests.delete(request);reject(Error(command+' 响应超时'));},90000);
        requests.set(request,{resolve,reject,timer,epoch:current.epoch});
        $('game').contentWindow.postMessage({protocol,game:current.game,epoch:current.epoch,command,request,...fields},location.origin);
    });
}
window.__eaglerPrepareManagedRuntimeDataV1 = async ({game,epoch})=>{
    const current=session;
    if(!current||current.game!==game||current.epoch!==epoch)throw Error('游戏已切换');
    const response=await fetch('./previews/'+game+'/private/game.data');
    if(!response.ok)throw Error('本地游戏资源未准备');
    const buffer=await response.arrayBuffer();
    if(session!==current)throw Error('游戏已切换');
    return {buffer,bytes:buffer.byteLength};
};
function keyboardMapping(current) {
    const target=$('game').contentWindow, held=new Map();
    target.__eaglerKeyboardMapping={
        schema:'eagler-touhou/keyboard-input/1',protocol,game:current.game,epoch:current.epoch,origin:location.origin,
        supports:code=>/^(Arrow|Key|Digit|Shift|Control|F\d|Escape|Enter|Backspace|Tab|Space|Home|End|Page|Numpad)/.test(code),
        map(event,down){if(down)held.set(event.code,event);else held.delete(event.code);return [{...event,down}];},
        clear(){const result=[...held.values()].map(event=>({...event,down:false}));held.clear();return result;},
    };
}
addEventListener('message',async event=>{
    const current=session, message=event.data;
    if(!current||event.source!==$('game').contentWindow||event.origin!==location.origin||
       message?.protocol!==protocol||message.game!==current.game||message.epoch!==current.epoch)return;
    const request=requests.get(message.request);
    if(request){clearTimeout(request.timer);requests.delete(message.request);message.ok?request.resolve(message):request.reject(Error(message.error));return;}
    if(message.event==='error')$('status').textContent=message.error??message.message??'游戏运行错误';
    if(message.event!=='ready'||current.launching)return;
    current.launching=true;
    try {
        keyboardMapping(current);
        const base='./previews/'+current.game+'/private/';
        const resources=[{path:'/msgothic.ttc',url:base+'msgothic.ttc'},{path:'/unifont.otf',url:base+'unifont.otf'},...current.metadata.music];
        await rpc('configure',{language:'ja',music:current.metadata.music.length?'ogg':'none',
            resources,sharedResources:[],options:current.options});
        controls({action:'preset',value:$('strength').value});
        controls({action:'team',value:+$('team').value});
        controls({action:'enabled',value:$('enabled').checked});
        controls({action:'autoBomb',value:$('autoBomb').checked});
        await rpc('launch');
        $('status').textContent='本地合作已启动，你控制 P1，AI 控制 P2。';
        syncParameters();focusGame();
    } catch(error){$('status').textContent=error.message;}
});
$('start').onclick=async()=>{
    try{
        for(const waiter of requests.values()){clearTimeout(waiter.timer);waiter.reject(Error('重新开始'));}
        requests.clear();
        const game=$('title').value;
        const response=await fetch('./previews/'+game+'/preview.json');
        if(!response.ok)throw Error('此作品的本地运行包尚未准备好');
        const metadata=await response.json();
        const loadouts=['human','partner'].map(id=>{const [character,shot]=$(id).value.split(',').map(Number);return {character,shot};});
        session={game,metadata,epoch:++nextEpoch,options:{
            netplayMode:'lan',netplayUrl:'ws://127.0.0.1/?room='+game+'mp-local&run='+nextEpoch,
            netplayPlayer:0,netplayPlayerCount:2,netplayDifficulty:+$('difficulty').value,
            netplaySeed:Math.floor(Math.random()*65536),netplayLoadouts:loadouts,
            netplayInputDelay:0,netplayInputDelayAuto:false,netplayAdonisMode:0,
            netplayPredictionLimit:12,netplayPredictionReserve:2,
        }};
        $('status').textContent='少女祈祷中…';
        $('game').src='./previews/'+game+'/'+game+'.html?hosted=1&managedData=1&runtimeEpoch='+session.epoch+'&runtimeVariant=multiplayer&gameGeneration=local';
    }catch(error){$('status').textContent=error.message;}
};
function key(event,down) {
    if(!session||event.target.closest?.('input,select,textarea,button,summary'))return;
    if(!/^(Arrow|Key|Shift|Control|Escape|Enter|Backspace|Tab|F\d)/.test(event.code))return;
    event.preventDefault();
    const target=$('game').contentWindow;
    const message={protocol,game:session.game,epoch:session.epoch,command:'keyboard',code:event.code,key:event.key,
        keyCode:event.keyCode,location:event.location,repeat:event.repeat,down,inputSource:'physical'};
    if(!target.__eaglerKeyboardInputBridge?.submit(message))target.postMessage(message,location.origin);
}
addEventListener('keydown',event=>key(event,true));
addEventListener('keyup',event=>key(event,false));
addEventListener('blur',()=>$('game').contentWindow.__eaglerKeyboardInputBridge?.clear());
setInterval(()=>{
    const result=controls({action:'status'});
    if(result?.ready)$('metrics').textContent='AI '+(result.active?'正在操作':'等待关卡')+'\n搜索 '+result.ms.toFixed(2)+' ms\n危险物 '+result.hazards;
},500);
