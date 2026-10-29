// Compiled into an isolated TH06 normal runtime. Reads the live collision
// objects; only the returned logical input word enters authoritative state.
#include "AvoidanceHook.hpp"
#include <eagler/ai/Planner.hpp>
#include "Player.hpp"
#include "EnemyManager.hpp"
#include "ItemManager.hpp"
#include "Gui.hpp"
#include "Supervisor.hpp"
#ifdef TH_ENABLE_THPRAC
#include "PracticeRuntime.hpp"
#endif
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define AI_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define AI_EXPORT
#endif
#if defined(TH_ENABLE_MULTIPLAYER_GAMEPLAY) || defined(TH_ENABLE_NETPLAY)
#error This experimental adapter is for the normal local TH06 runtime.
#endif
namespace {
using namespace eagler::ai;
Planner planner;
std::array<Hazard,Planner::MaxHazards> hazards;
std::array<Pickup,Planner::MaxPickups> pickups;
std::array<AttackTarget,Planner::MaxTargets> targets;
std::size_t count=0;
bool enabled=true,wasActive=false,overflow=false,f8=false;
u32 lastFrame=std::numeric_limits<u32>::max(),lastStage=0;
std::uint16_t cached=0;
Result result;
double elapsedMs=0;
std::uint64_t decisions=0;
int active=0;
void add(const Hazard& h){if(count<hazards.size())hazards[count++]=h;else overflow=true;}
void lasers(float scale,int horizon){
    for(const auto& laser:g_BulletManager.lasers){
        if(!laser.inUse)continue;
        float start=laser.startOffset,end=laser.endOffset,timer=laser.timer.AsFramesFloat();
        int state=laser.state;
        const float cs=std::cos(laser.angle),sn=std::sin(laser.angle);
        // Predict only local laser counters/lengths. No game update or RNG call.
        for(int f=1;f<=horizon&&state<=2;++f){
            end+=scale*laser.speed;start=std::max(0.f,std::max(start,end-laser.startLength));
            if(start>=640)break;
            float length=end-start;bool collidable=false;
            if(state==0){
                if(!(laser.flags&1)){
                    const float width=laser.startTime-std::min(laser.startTime,30)<timer&&laser.startTime>0?
                        timer*laser.width/laser.startTime:1.2f;
                    length=width*.5f; // Preserve TH06's length/width assignment.
                }
                collidable=timer>=laser.hitboxStartTime;
                if(timer>=laser.startTime){state=1;timer=0;collidable=true;}
            }
            if(state==1){
                collidable=true;
                if(timer>=laser.duration){state=2;timer=0;if(laser.despawnDuration==0)break;}
            }
            if(state==2){
                if(!(laser.flags&1)&&laser.despawnDuration>0)
                    length=(laser.width-timer*laser.width/laser.despawnDuration)*.5f;
                collidable=collidable||timer<laser.hitboxEndDelay;
                if(timer>=laser.despawnDuration)break;
            }
            if(collidable){
                Hazard h;h.shape=Shape::Box;
                const float mid=(start+end)*.5f;
                h.position={laser.pos.x+cs*mid,laser.pos.y+sn*mid};
                h.halfSize={std::max(0.f,length)*.5f,std::max(0.f,laser.width)*.25f};
                h.angle=laser.angle;h.activeFrom=h.activeUntil=f;add(h);
            }
            timer+=scale;
        }
    }
}
std::uint16_t map(std::uint32_t b){
    std::uint16_t out=0;
    if(b&Fire)out|=TH_BUTTON_SHOOT;if(b&Focus)out|=TH_BUTTON_FOCUS;
    if(b&Left)out|=TH_BUTTON_LEFT;if(b&Right)out|=TH_BUTTON_RIGHT;
    if(b&Up)out|=TH_BUTTON_UP;if(b&Down)out|=TH_BUTTON_DOWN;return out;
}
}
std::uint16_t Th06Ai::filter(std::uint16_t input){
    const bool key=SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F8];
    if(key&&!f8){enabled=!enabled;planner.reset();lastFrame=std::numeric_limits<u32>::max();}f8=key;
    active=enabled&&g_Supervisor.curState==SUPERVISOR_STATE_GAMEMANAGER&&
        !g_GameManager.isInReplay&&!g_GameManager.demoMode&&!g_GameManager.isInGameMenu&&
        !g_GameManager.isInRetryMenu&&!g_GameManager.isGameCompleted&&!g_GameManager.isTimeStopped&&
        !g_Gui.HasCurrentMsgIdx()&&g_Player.chainCalc&&
        (g_Player.playerState==PLAYER_STATE_ALIVE||g_Player.playerState==PLAYER_STATE_INVULNERABLE)&&
        !g_Player.bombInfo.isInUse;
#ifdef TH_ENABLE_THPRAC
    active=active&&!PracticeRuntime::CapturesGameInput();
#endif
    if(!active){if(wasActive)planner.reset();wasActive=false;lastFrame=std::numeric_limits<u32>::max();return input;}
    if(!wasActive||g_GameManager.currentStage!=int(lastStage)||g_GameManager.gameFrames<lastFrame)planner.reset();
    wasActive=true;
    constexpr std::uint16_t owned=TH_BUTTON_DIRECTION|TH_BUTTON_FOCUS|TH_BUTTON_SHOOT;
    const auto apply=[&](std::uint16_t ai){return std::uint16_t((input&~owned)|ai);};
    if(lastFrame==g_GameManager.gameFrames&&lastStage==u32(g_GameManager.currentStage))return apply(cached);
    lastFrame=g_GameManager.gameFrames;lastStage=u32(g_GameManager.currentStage);
    const auto begin=SDL_GetPerformanceCounter();count=0;overflow=false;
    const float scale=g_Supervisor.effectiveFramerateMultiplier;
    World w;const auto& p=g_Player;const auto& gm=g_GameManager;
    w.player.position={p.positionCenter.x,p.positionCenter.y};
    // TH06 uses an axis-aligned player square; its circumscribed circle is a
    // conservative fit for the shared planner's circular player geometry.
    w.player.radius=std::hypot(p.hitboxSize.x,p.hitboxSize.y);
    w.player.fastSpeed=p.characterData.orthogonalMovementSpeed*scale;
    w.player.slowSpeed=p.characterData.orthogonalMovementSpeedFocus*scale;
    w.bounds={gm.playerMovementAreaTopLeftPos.x,gm.playerMovementAreaTopLeftPos.y,
        gm.playerMovementAreaTopLeftPos.x+gm.playerMovementAreaSize.x,
        gm.playerMovementAreaTopLeftPos.y+gm.playerMovementAreaSize.y};
    w.goal={(w.bounds.left+w.bounds.right)*.5f,w.bounds.bottom-64};
    // Rank once per tick rather than inspect every item inside every route.
    std::array<std::pair<float,Pickup>,513> rankedItems{};std::size_t itemCount=0;
    for(const auto& item:g_ItemManager.items){
        if(!item.isInUse||item.itemType==ITEM_POINT_BULLET||item.state==1)continue;
        const auto& pos=item.currentPosition;
        if(pos.x<w.bounds.left-16||pos.x>w.bounds.right+16||pos.y>w.bounds.bottom+16)continue;
        const float value=item.itemType==ITEM_LIFE?4.f:item.itemType==ITEM_BOMB?3.f:
            (item.itemType==ITEM_FULL_POWER||item.itemType==ITEM_POWER_BIG)?2.f:1.f;
        Pickup point;point.position={pos.x,pos.y};
        point.velocity={std::clamp(pos.x-item.prevPosition.x,-4.f,4.f),std::clamp(pos.y-item.prevPosition.y,-4.f,4.f)};
        point.radius=std::min(p.grabItemSize.x,p.grabItemSize.y);point.value=value;
        rankedItems[itemCount++]={std::hypot(pos.x-p.positionCenter.x,pos.y-p.positionCenter.y)/value,point};
    }
    std::sort(rankedItems.begin(),rankedItems.begin()+itemCount,[](const auto& a,const auto& b){return a.first<b.first;});
    w.pickupCount=std::min(itemCount,pickups.size());
    for(std::size_t j=0;j<w.pickupCount;++j)pickups[j]=rankedItems[j].second;
    w.pickups=pickups.data();
    std::array<std::pair<float,AttackTarget>,257> rankedTargets{};std::size_t enemyCount=0;
    for(const auto& enemy:g_EnemyManager.enemies){
        if(!enemy.flags.isSlotOccupied||enemy.flags.isInvisible||!enemy.flags.isInteractable||!enemy.flags.isDamageable||enemy.life<=0)continue;
        const auto& pos=enemy.position;
        if(pos.x<0||pos.x>384||pos.y<0||pos.y>448)continue;
        AttackTarget target;target.position={pos.x,pos.y};
        target.velocity={pos.x-enemy.prevPosition.x,pos.y-enemy.prevPosition.y};
        target.halfWidth=std::max(8.f,enemy.hitboxDimensions.x*.5f);target.value=1;
        rankedTargets[enemyCount++]={std::abs(pos.x-p.positionCenter.x)+(pos.y>=p.positionCenter.y?448.f:0.f),target};
    }
    std::sort(rankedTargets.begin(),rankedTargets.begin()+enemyCount,[](const auto& a,const auto& b){return a.first<b.first;});
    w.targetCount=std::min(enemyCount,targets.size());
    for(std::size_t j=0;j<w.targetCount;++j)targets[j]=rankedTargets[j].second;
    w.targets=targets.data();
    for(const auto& bullet:g_BulletManager.bullets){
        if(bullet.state!=BULLET_STATE_FIRED)continue;
        Hazard h;h.shape=Shape::Box;h.position={bullet.pos.x,bullet.pos.y};
        h.velocity={bullet.velocity.x*scale,bullet.velocity.y*scale};
        if(bullet.exFlags&4)h.acceleration={bullet.ex4Acceleration.x*scale*scale,bullet.ex4Acceleration.y*scale*scale};
        h.halfSize={bullet.sprites.grazeSize.x*.5f,bullet.sprites.grazeSize.y*.5f};add(h);
    }
    for(const auto& enemy:g_EnemyManager.enemies){
        if(!enemy.flags.isSlotOccupied||!enemy.flags.hasBeenInBounds||enemy.flags.isInvisible||
           !enemy.flags.isCollidable||!enemy.flags.isInteractable)continue;
        Hazard h;h.shape=Shape::Box;h.position={enemy.position.x,enemy.position.y};
        h.velocity={enemy.position.x-enemy.prevPosition.x,enemy.position.y-enemy.prevPosition.y};
        h.halfSize={enemy.hitboxDimensions.x/3.f,enemy.hitboxDimensions.y/3.f};add(h);
    }
    lasers(scale,planner.settings().horizon);
    w.hazards=hazards.data();w.hazardCount=count;w.incomplete=overflow;
    result=planner.decide(w);decisions++;
    elapsedMs=1000.*double(SDL_GetPerformanceCounter()-begin)/double(SDL_GetPerformanceFrequency());
    if(result.flags&(IncompleteWorld|InvalidWorld)){cached=input&owned;return input;}
    cached=map(result.buttons);return apply(cached);
}
extern "C" {
AI_EXPORT void th06_ai_enable(int value){enabled=value!=0;planner.reset();lastFrame=std::numeric_limits<u32>::max();}
AI_EXPORT void th06_ai_configure(int horizon,int beam,int budget){
    auto s=planner.settings();s.horizon=horizon;s.beamWidth=beam;s.maxHazards=4096;
    s.maxCollisionTests=std::uint32_t(std::max(1,budget));planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_objectives(float pickup,float attack){
    auto s=planner.settings();s.pickupWeight=pickup;s.pickupApproachWeight=pickup*.0175f;s.attackWeight=attack;
    planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT double th06_ai_metric(int field){
    switch(field){case 0:return enabled;case 1:return active;case 2:return elapsedMs;case 3:return result.flags;
    case 4:return double(count);case 5:return result.stats.collisionTests;case 6:return result.stats.completedFrames;
    case 7:return double(decisions);case 8:return g_GameManager.currentStage;case 9:return result.safeFrames;default:return 0;}
}
}
