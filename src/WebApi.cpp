#include <eagler/ai/Planner.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <type_traits>

using namespace eagler::ai;
static_assert(std::is_standard_layout<Hazard>::value&&sizeof(Hazard)==18*4,"Web hazard ABI");
static_assert(offsetof(Hazard,position)==4&&offsetof(Hazard,radius)==52&&offsetof(Hazard,activeFrom)==64,"Web hazard offsets");
namespace {
struct Context {
    Planner planner;
    World world;
    std::array<Hazard,Planner::MaxHazards> hazards;
    std::array<Pickup,Planner::MaxPickups> pickups;
    std::array<AttackTarget,Planner::MaxTargets> targets;
    std::array<float,12> output{};
};
// Independent local teammates. The frontend owns an instance for each seat.
std::array<std::unique_ptr<Context>,8> contexts;
Context* get(int handle){return handle>=1&&handle<=int(contexts.size())?contexts[handle-1].get():nullptr;}
}
extern "C" {
int ai_abi_version(){return 2;}
int ai_create(){for(int i=0;i<int(contexts.size());++i)if(!contexts[i]){contexts[i]=std::make_unique<Context>();return i+1;}return 0;}
void ai_destroy(int handle){if(get(handle))contexts[handle-1].reset();}
void ai_reset(int handle){if(auto* c=get(handle))c->planner.reset();}
void ai_settings(int handle,int horizon,int beam,int step,int hazards,int tests,int visits,int fast,int fire,
                 float margin,float range,float danger,float goal,float edge,float movement,float switching,float merge){
    if(auto* c=get(handle)){
        Settings s;s.horizon=horizon;s.beamWidth=beam;s.actionFrames=step;s.maxHazards=hazards;
        s.maxCollisionTests=std::uint32_t(std::max(1,tests));s.maxTreeVisits=std::uint32_t(std::max(1,visits));
        s.allowFast=fast!=0;s.fire=fire!=0;s.safetyMargin=margin;s.proximityRange=range;
        s.dangerWeight=danger;s.goalWeight=goal;s.edgeWeight=edge;s.movementWeight=movement;s.switchWeight=switching;s.mergeDistance=merge;
        c->planner.configure(s);
    }
}
void ai_begin(int handle,int active,float x,float y,float radius,float fast,float slow,int invulnerability,
              float left,float top,float right,float bottom,float goalX,float goalY,int incomplete){
    if(auto* c=get(handle)){
        c->world={};c->world.active=active!=0;c->world.incomplete=incomplete!=0;
        c->world.player={{x,y},radius,fast,slow,invulnerability};c->world.bounds={left,top,right,bottom};
        c->world.goal={goalX,goalY};c->world.hazards=c->hazards.data();
        c->world.pickups=c->pickups.data();c->world.targets=c->targets.data();
    }
}
void ai_objective_weights(int handle,float pickup,float approach,float attack){if(auto* c=get(handle)){
    auto s=c->planner.settings();s.pickupWeight=pickup;s.pickupApproachWeight=approach;s.attackWeight=attack;c->planner.configure(s);
}}
int ai_objective(int handle,int kind,float x,float y,float vx,float vy,float size,float value){
    auto* c=get(handle);if(!c)return 0;
    if(kind==0&&c->world.pickupCount<c->pickups.size()){
        c->pickups[c->world.pickupCount++]={{x,y},{vx,vy},size,value};return 1;
    }
    if(kind==1&&c->world.targetCount<c->targets.size()){
        c->targets[c->world.targetCount++]={{x,y},{vx,vy},size,value};return 1;
    }
    return 0;
}
Hazard* ai_hazards(int handle){auto* c=get(handle);return c?c->hazards.data():nullptr;}
void ai_count(int handle,int count,int incomplete){if(auto* c=get(handle)){
    c->world.hazardCount=std::size_t(std::clamp(count,0,Planner::MaxHazards));
    c->world.incomplete=c->world.incomplete||incomplete||count<0||count>Planner::MaxHazards;
}}
int ai_step(int handle){
    auto* c=get(handle);if(!c)return 0;
    const auto r=c->planner.decide(c->world);
    c->output={float(r.buttons),float(r.flags),r.destination.x,r.destination.y,r.cost,r.clearance,float(r.safeFrames),
        float(r.stats.collisionTests),float(r.stats.treeVisits),float(r.stats.expanded),float(r.stats.indexedHazards),float(r.stats.completedFrames)};
    return 1;
}
const float* ai_result(int handle){auto* c=get(handle);return c?c->output.data():nullptr;}
}
