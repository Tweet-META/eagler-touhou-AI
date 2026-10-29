// Thin Wasm bridge. Run once per logical game tick, not once per rendered frame.
export const Button=Object.freeze({fire:1,focus:2,left:4,right:8,up:16,down:32});
export const Flag=Object.freeze({budget:1,incomplete:2,noSafePath:4,invalid:8,inactive:16});
export const defaults=Object.freeze({horizon:12,beamWidth:8,actionFrames:3,maxHazards:2048,
  maxCollisionTests:40000,maxTreeVisits:120000,allowFast:true,fire:true,safetyMargin:.45,
  proximityRange:24,dangerWeight:10,goalWeight:.004,edgeWeight:1,edgeDistance:80,movementWeight:.002,switchWeight:.025,mergeDistance:3,
  pickupWeight:3,pickupApproachWeight:.0525,attackWeight:.2,densityWeight:.1});
const shapes=Object.freeze({circle:0,box:1,capsule:2});
export class AvoidanceAI {
  static async load({moduleURL=new URL('./runtime/avoidance.mjs',import.meta.url),...options}={}) {
    const {default:create}=await import(String(moduleURL));
    return new AvoidanceAI(await create(),options);
  }
  constructor(core,options={}) {
    if(core._ai_abi_version()!==3)throw new Error('Unsupported avoidance AI ABI; rebuild the Web module');
    this.core=core;this.handle=core._ai_create();
    if(!this.handle)throw new Error('At most eight AI instances may share one module');
    const address=core._ai_hazards(this.handle);
    this.floats=new Float32Array(core.HEAPF32.buffer,address,4096*19);
    this.ints=new Int32Array(core.HEAP32.buffer,address,4096*19);
    this.resultView=new Float32Array(core.HEAPF32.buffer,core._ai_result(this.handle),12);
    this.count=0;this.overflow=false;
    this.settings={...defaults};this.configure(options);
    this.output={buttons:0,flags:0,x:0,y:0,cost:0,clearance:0,safeFrames:0,
      collisionTests:0,treeVisits:0,expanded:0,indexedHazards:0,completedFrames:0};
  }
  check(){if(!this.handle)throw new Error('AI instance has been disposed');}
  configure(options={}) {
    this.check();const s={...this.settings,...options};
    for(const key of Object.keys(defaults))if(typeof defaults[key]==='number'&&!Number.isFinite(s[key]))throw new TypeError(`Invalid AI setting: ${key}`);
    this.settings=s;this.core._ai_settings(this.handle,s.horizon,s.beamWidth,s.actionFrames,s.maxHazards,
      s.maxCollisionTests,s.maxTreeVisits,+s.allowFast,+s.fire,s.safetyMargin,s.proximityRange,s.dangerWeight,
      s.goalWeight,s.edgeWeight,s.movementWeight,s.switchWeight,s.mergeDistance);
    this.core._ai_objective_weights(this.handle,s.pickupWeight,s.pickupApproachWeight,s.attackWeight);
    this.core._ai_density(this.handle,s.densityWeight);
    this.core._ai_edges(this.handle,s.edgeWeight,s.edgeDistance);
  }
  reset(){this.check();this.core._ai_reset(this.handle);}
  // begin/add/finish avoids creating a JS array of every bullet each tick.
  begin(world) {
    this.check();const p=world.player,b=world.bounds,g=world.goal??p;
    if(!p||!b)throw new TypeError('player and bounds are required');
    this.count=0;this.overflow=false;
    this.core._ai_begin(this.handle,world.active!==false,p.x,p.y,p.radius,p.fastSpeed,p.slowSpeed,
      p.invulnerableFrames??0,b.left,b.top,b.right,b.bottom,g.x,g.y,!!world.incomplete);
  }
  hazard(h) {
    this.check();if(this.count===4096){this.overflow=true;return false;}
    const shape=typeof h.shape==='number'?h.shape:shapes[h.shape??'circle'];
    const a=this.count++*19,f=this.floats,i=this.ints;
    i[a]=shape??-1;f[a+1]=h.x;f[a+2]=h.y;f[a+3]=h.vx??0;f[a+4]=h.vy??0;
    f[a+5]=h.ax??0;f[a+6]=h.ay??0;f[a+7]=h.endX??0;f[a+8]=h.endY??0;
    f[a+9]=h.endVx??0;f[a+10]=h.endVy??0;f[a+11]=h.halfWidth??0;f[a+12]=h.halfHeight??0;
    f[a+13]=h.radius??0;f[a+14]=h.angle??0;f[a+15]=h.angularVelocity??0;
    i[a+16]=h.activeFrom??0;i[a+17]=h.activeUntil??24;f[a+18]=h.densityScale??1;return true;
  }
  pickup(item){this.check();return !!this.core._ai_objective(this.handle,0,item.x,item.y,item.vx??0,item.vy??0,item.radius??8,item.value??1);}
  target(enemy){this.check();return !!this.core._ai_objective(this.handle,1,enemy.x,enemy.y,enemy.vx??0,enemy.vy??0,enemy.halfWidth??16,enemy.value??1);}
  finish() {
    this.check();this.core._ai_count(this.handle,this.count,+this.overflow);this.core._ai_step(this.handle);
    const a=this.resultView,r=this.output;
    r.buttons=a[0];r.flags=a[1];r.x=a[2];r.y=a[3];r.cost=a[4];r.clearance=a[5];r.safeFrames=a[6];
    r.collisionTests=a[7];r.treeVisits=a[8];r.expanded=a[9];r.indexedHazards=a[10];r.completedFrames=a[11];
    return r; // Reused; copy explicitly if retaining history.
  }
  decide(world) {this.begin(world);for(const h of world.hazards??[])this.hazard(h);for(const item of world.pickups??[])this.pickup(item);for(const enemy of world.targets??[])this.target(enemy);return this.finish();}
  dispose(){if(this.handle){this.core._ai_destroy(this.handle);this.handle=0;}}
}
