import {AvoidanceAI,Button,Flag} from './avoidance-ai.mjs';

// Title integration seam: snapshot extraction and native button meanings belong
// to the game. This class does not assume a TH12 memory address or sprite ABI.
export class LocalAiSeat {
  constructor(ai,adapter,{seat=1,enabled=false}={}) {
    this.ai=ai;this.adapter=adapter;this.seat=seat;this.enabled=enabled;this.session=null;
    this.mapping=Object.entries(Button).map(([name,value])=>[value,adapter.buttons[name]??0]);
  }
  setEnabled(enabled){this.enabled=!!enabled;this.ai.reset();}
  // Call immediately BEFORE the title's logical input commit, alongside local
  // human input. The result then follows the usual recording/rollback path.
  beforeTick(context) {
    const active=this.enabled&&context.localSession===true&&!context.networked&&!context.replay&&
      !context.paused&&!context.dialogue&&context.playing;
    const session=context.generation??0;
    if(session!==this.session){this.ai.reset();this.session=session;}
    if(!active){this.adapter.release?.(this.seat);return null;}
    this.ai.begin(this.adapter.world(this.seat,context));
    // Emit into the reusable bridge rather than allocate per-bullet JS objects.
    this.adapter.hazards(this.seat,context,this.ai);
    this.adapter.objectives?.(this.seat,context,this.ai);
    const result=this.ai.finish();
    if(result.flags&(Flag.incomplete|Flag.invalid|Flag.inactive)){
      this.adapter.release?.(this.seat);this.adapter.unavailable?.(this.seat,result);return result;
    }
    let held=0;
    for(const [value,bit] of this.mapping)if(result.buttons&value)held|=bit;
    // Commit only this AI seat. Never OR AI movement into the human's lane.
    this.adapter.commit(this.seat,held,context);
    return result;
  }
  dispose(){this.adapter.release?.(this.seat);this.ai.dispose();}
}

export {AvoidanceAI};
