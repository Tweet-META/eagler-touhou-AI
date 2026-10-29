import {AvoidanceAI,Button,Flag} from './avoidance-ai.mjs';
const $=id=>document.getElementById(id),canvas=$('field'),ctx=canvas.getContext('2d');
const keys=new Set(),hazards=[],players=[];
const bounds={left:10,top:10,right:374,bottom:438};
let first,frame=0,paused=false,previous=0,debt=0,lastCost=0,totalCost=0,budgetCount=0,decisions=0;
const capacity=4000;
let density=3,limitedSpawns=0,lastTickCost=0,lastDrawCost=0,displayFrames=0,displaySince=0,displayFps=0,logicSince=0,logicBase=0,logicFps=0;
const clamp=(x,a,b)=>Math.min(b,Math.max(a,x));
const distance2=(x,y,a,b)=>{const dx=b.x-a.x,dy=b.y-a.y,t=clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy||1),0,1);return (x-a.x-t*dx)**2+(y-a.y-t*dy)**2;};
function makePlayer(index,ai=null){return {x:128+index*48,y:370,radius:2,fastSpeed:3.5,slowSpeed:1.7,invulnerableFrames:0,hits:0,ai,result:null};}
function setup(){
  if(!first)return;
  for(const p of players)if(p.ai&&p.ai!==first)p.ai.dispose();
  players.length=0;players.push(makePlayer(0));first.reset();
  const count=+$('seats').value;
  for(let i=0;i<count;i++)players.push(makePlayer(i+1,i?new AvoidanceAI(first.core):first));
  frame=0;hazards.length=0;budgetCount=decisions=0;totalCost=0;limitedSpawns=0;logicSince=0;logicBase=0;debt=0;configure();
}
function configure(){
  $('horizonValue').textContent=$('horizon').value;$('beamValue').textContent=$('beam').value;
  $('densityPenaltyValue').textContent=$('densityPenalty').value;
  $('edgeWeightValue').textContent=$('edgeWeight').value;$('edgeDistanceValue').textContent=$('edgeDistance').value;
  const count=players.length-1;
  for(const p of players)if(p.ai)p.ai.configure({horizon:+$('horizon').value,beamWidth:+$('beam').value,
    edgeWeight:+$('edgeWeight').value,edgeDistance:+$('edgeDistance').value,densityWeight:+$('densityPenalty').value,maxHazards:4096,maxCollisionTests:Math.floor(40000/count),maxTreeVisits:Math.floor(120000/count)});
}
function spawn(h){if(hazards.length<capacity)hazards.push(h);else limitedSpawns++;}
function circle(x,y,angle,speed,radius=3){spawn({shape:'circle',x,y,vx:Math.cos(angle)*speed,vy:Math.sin(angle)*speed,radius});}
function ring(x,y,count,angle,speed,radius=2){for(let i=0;i<count;i++)circle(x,y,angle+i*Math.PI*2/count,speed,radius);}
function emit(){
  const kind=$('pattern').value;
  if((kind==='vortex'||kind==='hell')&&frame%6===0){
    const n=Math.round(48*density),angle=frame*.047;
    ring(105+Math.sin(frame*.013)*45,92,n,angle,2.1,2);
    ring(279-Math.sin(frame*.013)*45,125,n,-angle,2.5,2);
  }
  if((kind==='curtain'||kind==='hell')&&frame%12===0){
    const n=Math.round(32*density),offset=Math.sin(frame*.04)*8;
    for(let i=0;i<n;i++){
      const x=(i+.5)*384/n;
      circle(x,-8,Math.PI/2+.48,2.9,2);
      circle(384-x,-20,Math.PI/2-.48,2.5,2);
      if(frame%24===0)circle(-8,20+i*420/n,offset*.03,3.2,2);
    }
  }
  if(kind==='hell'&&frame%40===0){
    for(let j=0;j<Math.min(32,4*density);j++){
      const x=12+j*360/Math.min(32,4*density),vx=Math.sin(frame*.03+j)*.6;
      spawn({shape:'capsule',x,y:-20,endX:384-x,endY:468,vx,vy:0,endVx:-vx,endVy:0,radius:2,
        activeFrom:25,activeUntil:110,age:0});
      spawn({shape:'box',x,y:-12,vx:Math.sin(j)*1.3,vy:2.4,halfWidth:14,halfHeight:3,angle:j,angularVelocity:.035,radius:0});
    }
  }
  if((kind==='rain'||kind==='mixed')&&frame%24===0){
    const n=12*density;for(let i=0;i<n;i++)circle((i+.5)*384/n,-10,Math.PI/2+Math.sin(frame*.03+i)*.18,2.2,3);
  }
  if((kind==='aimed'||kind==='mixed')&&frame%55===0){
    const target=players[(frame/55|0)%players.length],x=192+Math.sin(frame*.01)*100;
    const a=Math.atan2(target.y-50,target.x-x);
    for(let i=-3*density;i<=3*density;i++)circle(x,50,a+i*.09/density,2.6);
    ring(x,50,20*density,frame*.006,1.6,3);
  }
  if((kind==='lasers'||kind==='mixed')&&frame%120===0){
    const x=65+((frame/120|0)%3)*110,vx=frame%240?-.3:.3;
    // A telegraph is harmless for 45 ticks, then has a finite active lifetime.
    for(let j=0;j<density;j++){const dx=(j-(density-1)/2)*12;spawn({shape:'capsule',x:x+dx,y:-20,endX:x+dx+50,endY:468,vx,vy:0,endVx:vx,endVy:0,radius:3,activeFrom:45,activeUntil:135,age:0});}
  }
  if(kind==='mixed'&&frame%60===0)for(let j=0;j<density;j++){spawn({shape:'box',x:35+((frame*13+j*31)%310),y:-12,vx:Math.sin(frame)*.3,vy:1.8,
    halfWidth:12,halfHeight:3,angle:frame,angularVelocity:.018,radius:0});}
}
function move(p,buttons){
  let x=+!!(buttons&Button.right)-+!!(buttons&Button.left),y=+!!(buttons&Button.down)-+!!(buttons&Button.up);
  if(x&&y){x*=Math.SQRT1_2;y*=Math.SQRT1_2;}
  const speed=buttons&Button.focus?p.slowSpeed:p.fastSpeed;
  p.x=clamp(p.x+x*speed,bounds.left,bounds.right);p.y=clamp(p.y+y*speed,bounds.top,bounds.bottom);
}
function collision(p,old,h,ox,oy){
  if(h.activeFrom>0||h.activeUntil<0)return false;
  if(h.shape==='circle')return distance2(0,0,{x:old.x-ox,y:old.y-oy},{x:p.x-h.x,y:p.y-h.y})<(p.radius+h.radius)**2;
  if(h.shape==='capsule')return distance2(p.x,p.y,{x:h.x,y:h.y},{x:h.endX,y:h.endY})<(p.radius+h.radius)**2;
  const dx=p.x-h.x,dy=p.y-h.y,c=Math.cos(h.angle),s=Math.sin(h.angle);
  return Math.hypot(Math.max(0,Math.abs(dx*c+dy*s)-h.halfWidth),Math.max(0,Math.abs(-dx*s+dy*c)-h.halfHeight))<p.radius;
}
function tick(){
  const tickStart=performance.now(),stress=$('stress').checked;
  if(stress)for(const p of players)p.invulnerableFrames=0;
  emit();const old=players.map(p=>({x:p.x,y:p.y}));
  let human=0;for(const [key,bit] of [['ArrowLeft',Button.left],['ArrowRight',Button.right],['ArrowUp',Button.up],['ArrowDown',Button.down],['Shift',Button.focus]])if(keys.has(key))human|=bit;
  move(players[0],human);
  const start=performance.now();
  for(let i=1;i<players.length;i++){
    const p=players[i];p.ai.begin({player:p,bounds,goal:{x:96+i*56,y:370}});
    for(const h of hazards)p.ai.hazard(h);
    p.result=p.ai.finish();decisions++;if(p.result.flags&Flag.budget)budgetCount++;
    if(!(p.result.flags&(Flag.incomplete|Flag.invalid)))move(p,p.result.buttons);
  }
  lastCost=performance.now()-start;totalCost+=lastCost;
  for(const h of hazards){
    const ox=h.x,oy=h.y;
    h.x+=h.vx;h.y+=h.vy;
    if(h.shape==='capsule'){h.endX+=h.endVx;h.endY+=h.endVy;h.age++;h.activeFrom--;h.activeUntil--;}
    if(h.shape==='box')h.angle+=h.angularVelocity;
    for(let i=0;i<players.length;i++){
      const p=players[i];if(p.invulnerableFrames||p.hitFrame===frame||!collision(p,old[i],h,ox,oy))continue;
      p.hits++;p.hitFrame=frame;p.invulnerableFrames=stress?0:120;
      if(!stress)p.ai?.reset();
    }
  }
  let retained=0;for(const h of hazards)if(!(h.y>485||h.y< -80||h.x< -80||h.x>464||h.activeUntil<0))hazards[retained++]=h;
  hazards.length=retained;
  for(const p of players)if(p.invulnerableFrames>0)p.invulnerableFrames--;
  frame++;
  lastTickCost=performance.now()-tickStart;
  if(frame%15===0){
    const r=players[1].result;
    $('stats').textContent=`时间 ${(frame/60).toFixed(1)} s · 危险物 ${hazards.length} / ${capacity}\n`+
      `显示 ${displayFps.toFixed(0)} FPS · 逻辑 ${logicFps.toFixed(0)} Hz\n倍率 ${density}× · 达容量后未生成 ${limitedSpawns}\n`+
      `白色中弹 ${players[0].hits}\n`+players.slice(1).map((p,i)=>`AI ${i+1} 中弹 ${p.hits}`).join('\n')+
      `\nAI 合计用时 ${lastCost.toFixed(2)} ms / 帧\n平均 ${(totalCost/frame).toFixed(2)} ms / 帧\n`+
      `完整逻辑 ${lastTickCost.toFixed(2)} ms · 绘制 ${lastDrawCost.toFixed(2)} ms\n`+
      `预算用尽 ${budgetCount} / ${decisions} 次决策\n`+
      `AI 1：展开 ${r.expanded} · 判定 ${r.collisionTests}\n已搜 ${r.completedFrames} 帧 · 路径安全 ${r.safeFrames} 帧\n状态位 ${r.flags}`;
  }
}
function render(){
  const drawStart=performance.now();
  ctx.fillStyle='#141f2b';ctx.fillRect(0,0,384,448);ctx.strokeStyle='#253849';ctx.lineWidth=1;
  for(let x=0;x<384;x+=32){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,448);ctx.stroke();}
  ctx.fillStyle='#efb3d7';ctx.beginPath();
  for(const h of hazards)if(h.shape==='circle'){ctx.moveTo(h.x+h.radius+1,h.y);ctx.arc(h.x,h.y,h.radius+1,0,Math.PI*2);}
  ctx.fill();
  for(const h of hazards){
    if(h.shape==='circle')continue;
    ctx.save();ctx.translate(h.x,h.y);
    if(h.shape==='box'){ctx.rotate(h.angle);ctx.fillStyle='#f2b766';ctx.fillRect(-h.halfWidth,-h.halfHeight,h.halfWidth*2,h.halfHeight*2);}
    else{const warning=h.activeFrom>0;ctx.strokeStyle=warning?'#665e91':'#b4a3ff';ctx.lineWidth=warning?1:h.radius*2;ctx.setLineDash(warning?[5,7]:[]);ctx.beginPath();ctx.moveTo(0,0);ctx.lineTo(h.endX-h.x,h.endY-h.y);ctx.stroke();}
    ctx.restore();
  }
  for(let i=0;i<players.length;i++){
    const p=players[i];if(p.invulnerableFrames&&frame%8<4)continue;
    ctx.fillStyle=i?'#87cef2':'#fff';ctx.beginPath();ctx.moveTo(p.x,p.y-8);ctx.lineTo(p.x-5,p.y+6);ctx.lineTo(p.x+5,p.y+6);ctx.closePath();ctx.fill();
    ctx.fillStyle='#162331';ctx.beginPath();ctx.arc(p.x,p.y,p.radius,0,Math.PI*2);ctx.fill();
    if(i){ctx.fillStyle='#87cef2';ctx.font='10px sans-serif';ctx.fillText(`AI ${i}`,p.x+8,p.y+4);}
  }
  if(paused){ctx.fillStyle='#10151cb0';ctx.fillRect(0,0,384,448);ctx.fillStyle='#fff';ctx.font='20px sans-serif';ctx.textAlign='center';ctx.fillText('已暂停',192,224);ctx.textAlign='left';}
  lastDrawCost=performance.now()-drawStart;
}
function animation(now){
  displayFrames++;
  if(!displaySince)displaySince=now;
  if(now-displaySince>=1000){displayFps=displayFrames*1000/(now-displaySince);displayFrames=0;displaySince=now;}
  if(!logicSince){logicSince=now;logicBase=frame;}
  if(now-logicSince>=1000){logicFps=(frame-logicBase)*1000/(now-logicSince);logicSince=now;logicBase=frame;}
  if(previous)debt+=Math.min(4/60,(now-previous)/1000);previous=now;
  if(!paused)while(debt>=1/60){tick();debt-=1/60;}else debt=0;
  render();requestAnimationFrame(animation);
}
function setPaused(value){paused=value;keys.clear();$('pause').textContent=paused?'继续':'暂停';}
window.addEventListener('keydown',e=>{if(document.activeElement!==canvas)return;if(e.key.startsWith('Arrow')||e.key==='Shift'){keys.add(e.key);e.preventDefault();}});
window.addEventListener('keyup',e=>keys.delete(e.key));
window.addEventListener('blur',()=>setPaused(true));document.addEventListener('visibilitychange',()=>{if(document.hidden)setPaused(true);});
$('pause').onclick=()=>setPaused(!paused);$('reset').onclick=()=>setup();
$('pattern').onchange=$('seats').onchange=()=>setup();$('horizon').oninput=$('beam').oninput=()=>configure();
$('density').oninput=()=>{density=+$('density').value;$('densityValue').textContent=`${density}×`;};
$('densityPenalty').oninput=configure;
$('edgeWeight').oninput=$('edgeDistance').oninput=configure;
try{
  first=await AvoidanceAI.load();setup();$('status').textContent='已加载 · C++ / WebAssembly';requestAnimationFrame(animation);
}catch(error){$('status').className='error';$('status').textContent=`启动失败：${error.message}。请先运行 npm run build:web。`;}
