#include <eagler/ai/Planner.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace eagler::ai {
namespace {
Vec2 add(Vec2 a,Vec2 b){return {a.x+b.x,a.y+b.y};}
Vec2 sub(Vec2 a,Vec2 b){return {a.x-b.x,a.y-b.y};}
Vec2 mul(Vec2 a,float k){return {a.x*k,a.y*k};}
float dot(Vec2 a,Vec2 b){return a.x*b.x+a.y*b.y;}
float length2(Vec2 a){return dot(a,a);}
bool finite(float x){return std::isfinite(x)&&std::abs(x)<=1000000;}
bool finite(Vec2 p){return finite(p.x)&&finite(p.y);}
float pointSegment2(Vec2 p,Vec2 a,Vec2 b){
    const Vec2 d=sub(b,a);const float dd=length2(d);
    return length2(sub(p,add(a,mul(d,dd>1e-12f?std::clamp(dot(sub(p,a),d)/dd,0.f,1.f):0.f))));
}
float cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
float segments2(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    const Vec2 ab=sub(b,a),cd=sub(d,c);const float denom=cross(ab,cd);
    if(std::abs(denom)>1e-10f){
        const float t=cross(sub(c,a),cd)/denom,u=cross(sub(c,a),ab)/denom;
        if(t>=0&&t<=1&&u>=0&&u<=1)return 0;
    }
    return std::min({pointSegment2(a,c,d),pointSegment2(b,c,d),pointSegment2(c,a,b),pointSegment2(d,a,b)});
}
float rectangleDistance2(Vec2 a,Vec2 b,Vec2 half){
    if((std::abs(a.x)<=half.x&&std::abs(a.y)<=half.y)||
       (std::abs(b.x)<=half.x&&std::abs(b.y)<=half.y))return 0;
    const Vec2 p[]{ {-half.x,-half.y},{half.x,-half.y},{half.x,half.y},{-half.x,half.y} };
    return std::min({segments2(a,b,p[0],p[1]),segments2(a,b,p[1],p[2]),
                     segments2(a,b,p[2],p[3]),segments2(a,b,p[3],p[0])});
}
Vec2 at(const Hazard& h,float t){return add(h.position,add(mul(h.velocity,t),mul(h.acceleration,.5f*t*t)));}
Vec2 endAt(const Hazard& h,float t){return add(h.end,mul(h.endVelocity,t));}
float distance(const Hazard& h,Vec2 from,Vec2 to,int frame,float playerRadius){
    // Clip the movement interval to the hazard's lifetime. Birth at the tick
    // endpoint must not make its position one tick early along the whole path.
    const float lo=std::max(float(frame-1),float(h.activeFrom));
    const float hi=std::min(float(frame),float(h.activeUntil));
    if(lo>hi)return 10000;
    const Vec2 delta=sub(to,from);
    from=add(from,mul(delta,lo-(frame-1)));
    to=add(from,mul(delta,hi-lo));
    const Vec2 a=at(h,lo),b=at(h,hi);
    const float dt=hi-lo;
    if(h.shape==Shape::Circle){
        // Relative segment catches fast bullets crossing between two ticks.
        const float curve=std::sqrt(length2(h.acceleration))*dt*dt*.125f;
        return std::sqrt(pointSegment2({},sub(from,a),sub(to,b)))-h.radius-playerRadius-curve;
    }
    if(h.shape==Shape::Box){
        const float mid=(lo+hi)*.5f,angle=h.angle+h.angularVelocity*mid;
        const float cs=std::cos(angle),sn=std::sin(angle);
        const auto rotate=[&](Vec2 v){return Vec2{v.x*cs+v.y*sn,-v.x*sn+v.y*cs};};
        const float angular=std::sqrt(length2(h.halfSize))*std::min(2.f,std::abs(h.angularVelocity)*dt*.5f);
        const float curve=std::sqrt(length2(h.acceleration))*dt*dt*.125f;
        return std::sqrt(rectangleDistance2(rotate(sub(from,a)),rotate(sub(to,b)),h.halfSize))-playerRadius-angular-curve;
    }
    // Independent endpoint velocities describe moving/deforming laser pieces.
    // Freeze the segment at the middle and enlarge by its endpoint displacement
    // bound. Translation is removed first, so a translating laser is exact.
    const Vec2 ea=endAt(h,lo),eb=endAt(h,hi);
    const Vec2 relative0=sub(ea,a),relative1=sub(eb,b);
    const Vec2 middle=mul(add(relative0,relative1),.5f);
    const float deformation=std::sqrt(length2(sub(relative1,relative0)))*.5f;
    const float curve=std::sqrt(length2(h.acceleration))*dt*dt*.125f;
    return std::sqrt(segments2(sub(from,a),sub(to,b),{},middle))-h.radius-playerRadius-deformation-curve;
}
template<class B> B unite(B a,B b){return {std::min(a.l,b.l),std::min(a.t,b.t),std::max(a.r,b.r),std::max(a.b,b.b)};}
template<class B> float lowerBound2(B b,Vec2 a,Vec2 c){
    const float x=std::max({b.l-std::max(a.x,c.x),std::min(a.x,c.x)-b.r,0.f});
    const float y=std::max({b.t-std::max(a.y,c.y),std::min(a.y,c.y)-b.b,0.f});
    return x*x+y*y;
}
bool valid(const Hazard& h){
    return std::uint32_t(h.shape)<=2&&finite(h.position)&&finite(h.velocity)&&finite(h.acceleration)&&
        finite(h.end)&&finite(h.endVelocity)&&finite(h.halfSize)&&finite(h.radius)&&finite(h.angle)&&finite(h.angularVelocity)&&
        h.radius>=0&&h.halfSize.x>=0&&h.halfSize.y>=0&&finite(h.densityScale)&&h.densityScale>=0;
}
float positive(float x,float fallback,float maximum){return finite(x)&&x>=0?std::min(x,maximum):fallback;}
Vec2 direction(std::uint32_t b){
    float x=float(bool(b&Right))-float(bool(b&Left)),y=float(bool(b&Down))-float(bool(b&Up));
    if(x&&y){x*=.7071067811865475f;y*=.7071067811865475f;}
    return {x,y};
}
float edgeCost(Vec2 p,Bounds b,float range){
    const float x=std::max(0.f,1-std::min(p.x-b.left,b.right-p.x)/range);
    const float y=std::max(0.f,1-std::min(p.y-b.top,b.bottom-p.y)/range);
    return x*x+y*y;
}
}

Planner::Planner(Settings settings){
    entries_.reserve(MaxHazards);order_.reserve(MaxHazards);tree_.reserve(MaxHazards*2);
    beam_.reserve(MaxBeam);next_.reserve(MaxBeam);candidates_.reserve(MaxBeam*17);
    configure(settings);
}
void Planner::configure(Settings s){
    s.horizon=std::clamp(s.horizon,1,MaxHorizon);s.beamWidth=std::clamp(s.beamWidth,1,MaxBeam);
    s.actionFrames=std::clamp(s.actionFrames,1,6);s.maxHazards=std::clamp(s.maxHazards,1,MaxHazards);
    s.maxCollisionTests=std::clamp(s.maxCollisionTests,1u,1000000u);
    s.maxTreeVisits=std::clamp(s.maxTreeVisits,1u,3000000u);
    s.safetyMargin=positive(s.safetyMargin,.45f,16);
    s.proximityRange=std::max(1.f,positive(s.proximityRange,24,128));
    s.dangerWeight=positive(s.dangerWeight,10,100);s.goalWeight=positive(s.goalWeight,.004f,10);
    s.edgeWeight=positive(s.edgeWeight,.12f,100);s.movementWeight=positive(s.movementWeight,.002f,10);
    s.switchWeight=positive(s.switchWeight,.025f,10);s.edgeDistance=std::max(1.f,positive(s.edgeDistance,24,256));
    s.mergeDistance=positive(s.mergeDistance,3,64);config_=s;
    config_.pickupWeight=positive(s.pickupWeight,1.5f,100);
    config_.pickupApproachWeight=positive(s.pickupApproachWeight,.02625f,10);
    config_.attackWeight=positive(s.attackWeight,.2f,10);
    config_.densityWeight=positive(s.densityWeight,.1f,10);
}
int Planner::build(int begin,int end){
    const int id=int(tree_.size());tree_.push_back({});
    Aabb box=entries_[order_[begin]].bounds;
    for(int i=begin+1;i<end;++i)box=unite(box,entries_[order_[i]].bounds);
    tree_[id].bounds=box;tree_[id].begin=begin;tree_[id].end=end;
    if(end-begin>8){
        const bool x=box.r-box.l>box.b-box.t;const int mid=(begin+end)/2;
        std::nth_element(order_.begin()+begin,order_.begin()+mid,order_.begin()+end,[&](int a,int b){
            const auto& p=entries_[a].bounds;const auto& q=entries_[b].bounds;
            const float ca=x?p.l+p.r:p.t+p.b,cb=x?q.l+q.r:q.t+q.b;
            return ca==cb?a<b:ca<cb;
        });
        tree_[id].left=build(begin,mid);tree_[id].right=build(mid,end);
    }
    return id;
}
bool Planner::clearance(Vec2 from,Vec2 to,int frame,float radius,Statistics& stats,float& margin,float& density) const {
    margin=config_.proximityRange;density=0;if(tree_.empty())return true;
    std::array<int,32> stack{};int count=1;stack[0]=0;
    while(count){
        if(stats.treeVisits>=config_.maxTreeVisits)return false;
        ++stats.treeVisits;const auto& n=tree_[stack[--count]];
        // Keep searching the density neighborhood until its capped penalty is
        // saturated, then resume nearest-only pruning. Zero weight is free.
        const float range=std::max(config_.densityWeight>0&&density<8?32.f:0.f,margin)+radius;
        if(lowerBound2(n.bounds,from,to)>range*range)continue;
        if(n.left>=0){stack[count++]=n.right;stack[count++]=n.left;continue;}
        for(int i=n.begin;i<n.end;++i){
            const auto& h=entries_[order_[i]].hazard;
            if(frame<h.activeFrom||frame-1>h.activeUntil)continue;
            if(stats.collisionTests>=config_.maxCollisionTests)return false;
            ++stats.collisionTests;const float d=distance(h,from,to,frame,radius);
            margin=std::min(margin,d);
            if(config_.densityWeight>0&&h.densityScale>0&&frame>=h.activeFrom&&frame<=h.activeUntil)
                density=std::min(8.f,density+std::min(1.f,h.densityScale)*std::max(0.f,1-std::max(0.f,d)/32));
            if(margin<config_.safetyMargin)return true;
        }
    }
    return true;
}
Result Planner::decide(const World& w){
    Result result;result.destination=w.player.position;
    if(!w.active){result.flags=Inactive;previous_=0;return result;}
    const auto& p=w.player;const auto& c=config_;const auto b=w.bounds;
    if(!finite(p.position)||!finite(p.radius)||p.radius<0||!finite(p.fastSpeed)||p.fastSpeed<0||
       !finite(p.slowSpeed)||p.slowSpeed<0||!finite(w.goal)||!finite(b.left)||!finite(b.top)||
       !finite(b.right)||!finite(b.bottom)||b.left>b.right||b.top>b.bottom||
       p.position.x<b.left||p.position.x>b.right||p.position.y<b.top||p.position.y>b.bottom||
       (w.hazardCount&&!w.hazards)||(w.pickupCount&&!w.pickups)||(w.targetCount&&!w.targets)){
        result.flags=InvalidWorld;previous_=0;return result;
    }
    result.buttons=(c.fire?Fire:0u)|Focus;
    const auto pickupCount=std::min<std::size_t>(w.pickupCount,MaxPickups);
    const auto targetCount=std::min<std::size_t>(w.targetCount,MaxTargets);
    const bool objectives=(pickupCount&&(c.pickupWeight>0||c.pickupApproachWeight>0))||
        (targetCount&&c.fire&&c.attackWeight>0);
    const auto attackScore=[&](Vec2 pos,int time){
        float best=-std::numeric_limits<float>::infinity();
        if(c.fire)for(std::size_t j=0;j<targetCount;++j){
            const auto& enemy=w.targets[j];
            if(!finite(enemy.position)||!finite(enemy.velocity)||!finite(enemy.halfWidth)||enemy.halfWidth<0||!finite(enemy.value)||enemy.value<=0)continue;
            const Vec2 target=add(enemy.position,mul(enemy.velocity,float(time)));
            const float gap=std::max(0.f,std::abs(pos.x-target.x)-enemy.halfWidth);
            const float aboveGap=std::max(0.f,target.y+16-pos.y);
            const bool firingColumn=pos.y>target.y&&gap==0;
            // A common distance baseline on BOTH sides prevents an above-target
            // zero score from beating a distant below-target negative score.
            // Only actual below-and-aligned positions get the output bonus.
            const float score=(firingColumn?1.f:0.f)-gap/160-aboveGap/80;
            best=std::max(best,std::min(enemy.value,4.f)*score);
        }
        return std::isfinite(best)?best:0.f;
    };
    const auto itemPotential=[&](Vec2 pos,int time,std::uint32_t collected){
        float best=-std::numeric_limits<float>::infinity();
        for(std::size_t j=0;j<pickupCount;++j){
            const auto& item=w.pickups[j];if(collected&(1u<<j))continue;
            if(!finite(item.position)||!finite(item.velocity)||!finite(item.value)||item.value<=0||!finite(item.radius)||item.radius<0)continue;
            const Vec2 location=add(item.position,mul(item.velocity,float(time)));
            const float distance=std::max(0.f,std::sqrt(length2(sub(pos,location)))-item.radius);
            // A negative potential is meaningful: keeping it beyond 160 px
            // provides an approach gradient across the whole playfield.
            best=std::max(best,std::min(item.value,10.f)*(160-distance));
        }
        return std::isfinite(best)?best:0.f;
    };
    if(w.incomplete||w.hazardCount>MaxHazards)result.flags|=IncompleteWorld;
    entries_.clear();order_.clear();tree_.clear();
    const float reach=std::max(p.fastSpeed,p.slowSpeed)*c.horizon+p.radius+std::max(c.proximityRange,c.densityWeight>0?32.f:0.f);
    for(std::size_t i=0;i<std::min<std::size_t>(w.hazardCount,MaxHazards);++i){
        const auto& h=w.hazards[i];if(!valid(h)){result.flags|=IncompleteWorld;continue;}
        if(h.activeFrom>c.horizon||h.activeUntil<0||h.activeFrom>h.activeUntil)continue;
        const float lo=float(std::max(0,h.activeFrom)),hi=float(std::min(c.horizon,h.activeUntil));
        const Vec2 start=at(h,lo),end=at(h,hi);
        Aabb box{std::min(start.x,end.x),std::min(start.y,end.y),std::max(start.x,end.x),std::max(start.y,end.y)};
        const auto include=[&](Vec2 v){box=unite(box,Aabb{v.x,v.y,v.x,v.y});};
        if(h.acceleration.x){const float t=-h.velocity.x/h.acceleration.x;if(t>lo&&t<hi)include(at(h,t));}
        if(h.acceleration.y){const float t=-h.velocity.y/h.acceleration.y;if(t>lo&&t<hi)include(at(h,t));}
        if(h.shape==Shape::Capsule){include(endAt(h,lo));include(endAt(h,hi));}
        const float extent=h.shape==Shape::Box?std::sqrt(length2(h.halfSize)):h.radius;
        box.l-=extent;box.t-=extent;box.r+=extent;box.b+=extent;
        if(lowerBound2(box,p.position,p.position)>reach*reach)continue;
        if(entries_.size()>=std::size_t(c.maxHazards)){result.flags|=IncompleteWorld;continue;}
        entries_.push_back({h,box});
    }
    result.stats.indexedHazards=std::uint32_t(entries_.size());
    order_.resize(entries_.size());std::iota(order_.begin(),order_.end(),0);
    if(!entries_.empty())build(0,int(entries_.size()));
    constexpr std::uint32_t dirs[]{0,Left,Right,Up,Down,Left|Up,Right|Up,Left|Down,Right|Down};
    std::array<std::uint32_t,17> actions{};int actionCount=0;
    for(auto d:dirs)actions[actionCount++]=Focus|d;
    if(c.allowFast)for(int i=1;i<9;++i)actions[actionCount++]=dirs[i];
    const auto previous=std::find(actions.begin(),actions.begin()+actionCount,previous_);
    if(previous!=actions.begin()+actionCount)std::rotate(actions.begin(),previous,previous+1);
    beam_.clear();Route initial;initial.p=initial.firstPosition=p.position;initial.last=previous_;initial.margin=c.proximityRange;beam_.push_back(initial);
    Route fallback=beam_.front();int fallbackTime=0;bool exhausted=false;
    for(int elapsed=0;elapsed<c.horizon;){
        candidates_.clear();const int span=std::min(c.actionFrames,c.horizon-elapsed);
        for(const auto& parent:beam_){
            for(int ai=0;ai<actionCount;++ai){
                Route r=parent;const auto action=actions[ai];r.ordinal=++result.stats.expanded;
                if(action!=r.last)r.cost+=c.switchWeight;
                r.last=action;if(!elapsed)r.first=action;
                const Vec2 move=mul(direction(action),action&Focus?p.slowSpeed:p.fastSpeed);
                bool safe=true;
                for(int f=1;f<=span;++f){
                    Vec2 to=add(r.p,move);to.x=std::clamp(to.x,b.left,b.right);to.y=std::clamp(to.y,b.top,b.bottom);
                    float margin=c.proximityRange,density=0;
                    if(elapsed+f>p.invulnerableFrames){
                        if(!clearance(r.p,to,elapsed+f,p.radius,result.stats,margin,density)){exhausted=true;safe=false;break;}
                        if(margin<c.safetyMargin){safe=false;break;}
                    }
                    r.margin=std::min(r.margin,margin);
                    r.cost+=c.dangerWeight/(std::max(.1f,margin)+2);
                    r.cost+=c.densityWeight*density;
                    // The idle goal must not pull against collection/attack.
                    if(!objectives)r.cost+=c.goalWeight*(std::abs(to.x-w.goal.x)+std::abs(to.y-w.goal.y));
                    r.cost+=c.edgeWeight*edgeCost(to,b,c.edgeDistance);
                    r.cost+=c.movementWeight*(std::abs(to.x-r.p.x)+std::abs(to.y-r.p.y));
                    r.cost+=c.pickupApproachWeight*(itemPotential(r.p,elapsed+f-1,r.collected)-itemPotential(to,elapsed+f,r.collected));
                    for(std::size_t j=0;j<pickupCount;++j){
                        const auto& item=w.pickups[j];const auto bit=1u<<j;
                        if((r.collected&bit)||!finite(item.position)||!finite(item.velocity)||!finite(item.radius)||item.radius<0||!finite(item.value)||item.value<=0)continue;
                        const Vec2 a=sub(r.p,add(item.position,mul(item.velocity,float(elapsed+f-1))));
                        const Vec2 z=sub(to,add(item.position,mul(item.velocity,float(elapsed+f))));
                        if(pointSegment2({},a,z)<=item.radius*item.radius){
                            r.cost-=c.pickupWeight*std::min(item.value,10.f);r.collected|=bit;
                        }
                    }
                    const float attack=attackScore(to,elapsed+f);
                    // Potential shaping rewards immediate alignment progress,
                    // overcoming action-switch cost without duplicating damage.
                    r.cost+=8*c.attackWeight*(attackScore(r.p,elapsed+f-1)-attack);
                    r.cost-=c.attackWeight*attack;
                    r.p=to;if(!elapsed&&f==1)r.firstPosition=to;
                    const int time=elapsed+f;
                    if(time>fallbackTime||(time==fallbackTime&&r.cost<fallback.cost)){fallback=r;fallbackTime=time;}
                }
                if(safe)candidates_.push_back(r);
                if(exhausted)break;
            }
            if(exhausted)break;
        }
        if(exhausted){result.flags|=BudgetExhausted;break;}
        if(candidates_.empty()){result.flags|=NoSafePath;break;}
        std::sort(candidates_.begin(),candidates_.end(),[](const Route& a,const Route& b){return a.cost==b.cost?a.ordinal<b.ordinal:a.cost<b.cost;});
        next_.clear();
        for(const auto& candidate:candidates_){
            bool duplicate=false;
            for(const auto& selected:next_){
                // Keep first-input diversity: two nearby endpoints may require
                // different immediate actions to survive the next replanning.
                if(candidate.first==selected.first&&candidate.collected==selected.collected&&length2(sub(candidate.p,selected.p))<c.mergeDistance*c.mergeDistance){duplicate=true;break;}
            }
            if(!duplicate)next_.push_back(candidate);
            if(int(next_.size())==c.beamWidth)break;
        }
        beam_.swap(next_);elapsed+=span;result.stats.completedFrames=elapsed;
        if(elapsed==c.horizon){fallback=beam_.front();fallbackTime=elapsed;}
    }
    if(fallbackTime){
        result.buttons=fallback.first|(c.fire?Fire:0u);result.destination=fallback.firstPosition;
        result.cost=fallback.cost;result.clearance=fallback.margin;result.safeFrames=fallbackTime;
        previous_=fallback.first;
    }else{result.flags|=NoSafePath;previous_=Focus;}
    return result;
}
} // namespace eagler::ai
