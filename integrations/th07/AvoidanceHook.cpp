// Compiled into an isolated TH07 local runtime. Reads the live collision
// objects; only the returned logical input word enters authoritative state.
#include "AvoidanceHook.hpp"
#include <eagler/ai/Planner.hpp>
#include <eagler/ai/InputDelay.hpp>
#include "Player.hpp"
#include "Controller.hpp"
#include "EnemyManager.hpp"
#include "ItemManager.hpp"
#include "Gui.hpp"
#include "Supervisor.hpp"
#include "ReplayExtension.hpp"
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
#include "multiplayer/GameplaySession.hpp"
#include <eagler/netplay/NetplayInput.hpp>
#endif
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
#if defined(TH_ENABLE_NETPLAY)
#error Local AI seats must not be attached to a network session.
#endif
namespace {
using namespace eagler::ai;
constexpr int controlledSlot=1,teammateSlot=0;
u32 decisionFrame(){return g_GameManager.framesThisStage;}
Planner planner;
InputDelay inputDelay;
int requestedHorizon=12,actionRemaining=0;
std::uint32_t heldAction=Focus;
bool actionTickValid=false;
u32 actionTickFrame=0,actionTickStage=0;
void resetAction(){actionRemaining=0;heldAction=Focus;actionTickValid=false;}
std::uint32_t takeAction(std::uint32_t wanted){
    const u32 frame=decisionFrame(),stage=u32(g_GameManager.currentStage);
    if(actionTickValid&&frame==actionTickFrame&&stage==actionTickStage)return heldAction;
    if(!actionRemaining){heldAction=wanted&(Focus|Left|Right|Up|Down);actionRemaining=planner.settings().actionFrames;}
    --actionRemaining;actionTickValid=true;actionTickFrame=frame;actionTickStage=stage;
    return heldAction;
}
bool inputTickValid=false,wasRescuing=false;
u32 inputTickFrame=0,inputTickStage=0;
std::uint16_t deliveredInput=0;
void resetInput(){inputDelay.reset();inputTickValid=false;deliveredInput=0;}
bool autoBomb=true,bombTickValid=false,bombPulse=false,humanBombRequested=false,previousHumanBomb=false;
u32 bombTickFrame=0,bombTickStage=0;
void resetBomb(){bombTickValid=false;bombPulse=false;}
std::array<Hazard,Planner::MaxHazards> hazards;
std::array<Pickup,Planner::MaxPickups> pickups;
std::array<AttackTarget,Planner::MaxTargets> targets;
std::size_t count=0;
bool enabled=true;
bool wasActive=false,overflow=false;
u32 lastFrame=std::numeric_limits<u32>::max(),lastStage=0;
std::uint16_t cached=0;
Result result;
double elapsedMs=0;
std::uint64_t decisions=0;
int active=0;
bool ownsInputs=false;
int teamMode=0,splitSide=0; // 0 follows P1; 1 covers the opposite side on stage.
bool preserveFollowAttack=false;
float splitAttackWeight=.2f,splitEdgeWeight=1.f;
const float splitMovementWeight=planner.settings().movementWeight;
bool canMove(const ::Player& p){
    return p.playerState==PLAYER_STATE_ALIVE||p.playerState==PLAYER_STATE_INVULNERABLE||
           p.playerState==PLAYER_STATE_BORDER;
}
bool hasBorderProtection(const ::Player& p){
    return p.hasBorder!=BORDER_NONE||p.playerState==PLAYER_STATE_BORDER||p.borderInvulnerabilityTime>0;
}
int protectedFrames(const ::Player& p){
    if(hasBorderProtection(p))return 0;
    int remaining=p.playerState==PLAYER_STATE_INVULNERABLE?std::max(0,p.invulnerabilityTimer.current-1):0;
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    remaining=std::max(remaining,std::max(0,p.teamBombProtectionTimer.current-1));
#endif
    return remaining;
}
bool humanUnavailable(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[teammateSlot];
    if(!MultiplayerGameplay::IsPlayerActive(teammateSlot))return true;
    return human.calcChain&&!canMove(human);
#else
    return false;
#endif
}
bool rescueWanted(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& ai=g_Players[controlledSlot];
    return MultiplayerGameplay::IsMultiplayer()&&MultiplayerGameplay::IsPlayerActive(teammateSlot)&&
        g_Players[teammateSlot].calcChain&&g_Players[teammateSlot].playerState==PLAYER_STATE_SPIRIT&&GetPlayerLives(controlledSlot)>0&&
        ai.calcChain&&canMove(ai);
#else
    return false;
#endif
}
bool teamWeightsLocked(){return rescueWanted()||(teamMode==0&&!humanUnavailable());}
bool attackWeightLocked(){return rescueWanted()||(!preserveFollowAttack&&teamMode==0&&!humanUnavailable());}
bool edgeWeightLocked(){return teamWeightsLocked()||(teamMode==1&&!g_Gui.BossPresent());}
struct FollowHistory {
    std::array<Vec2,19> positions{}; // Current sample plus 18 logical ticks (0.3 s).
    std::size_t next=0,count=0;
    u32 frame=std::numeric_limits<u32>::max(),stage=0;
    void reset(){next=count=0;frame=std::numeric_limits<u32>::max();stage=0;}
    void sample(Vec2 pos,u32 now,u32 currentStage){
        if(count&&(currentStage!=stage||now<frame))reset();
        if(count&&now==frame)return;
        positions[next]=pos;next=(next+1)%positions.size();
        count=std::min(count+1,positions.size());frame=now;stage=currentStage;
    }
    Vec2 delayed() const {return positions[count==positions.size()?next:0];}
} followHistory;
void applyTeamWeights(){
    const bool locked=teamWeightsLocked();auto s=planner.settings();
    const float attack=attackWeightLocked()?0:splitAttackWeight,edge=edgeWeightLocked()?0:splitEdgeWeight,movement=locked?0:splitMovementWeight;
    if(s.attackWeight==attack&&s.edgeWeight==edge&&s.movementWeight==movement)return;
    s.attackWeight=attack;s.edgeWeight=edge;s.movementWeight=movement;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
void sampleTeammate(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[teammateSlot];
    if(!MultiplayerGameplay::IsPlayerActive(teammateSlot)||!human.calcChain||
       (!canMove(human))){
        followHistory.reset();return;
    }
    if(g_Supervisor.curState!=SUPERVISOR_STATE_GAMEMANAGER||g_GameManager.isInPauseMenu||
       g_GameManager.isInRetryMenu||g_GameManager.finished||g_GameManager.isTimeStopped||
       g_Gui.HasCurrentMsgIdx())return;
#ifdef TH_ENABLE_THPRAC
    if(PracticeRuntime::CapturesGameInput())return;
#endif
    followHistory.sample({human.pos.x,human.pos.y},
                         decisionFrame(),u32(g_GameManager.currentStage));
#endif
}
::Player& controlledPlayer(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    return g_Players[controlledSlot];
#else
    return g_Player;
#endif
}
bool canBomb(){
    const auto& player=controlledPlayer();
    bool eligible=autoBomb&&player.playerState==PLAYER_STATE_ALIVE&&!player.bombInfo.isInUse&&
                  player.bombInfo.bombCalc&&player.respawnTimer!=0&&!hasBorderProtection(player);
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    eligible=eligible&&GetPlayerBombs(controlledSlot)>0&&player.teamBombProtectionTimer.current<=0&&!humanBombRequested;
    for(int i=0;i<MultiplayerGameplay::GetPlayerCount();++i)
        if(i!=controlledSlot&&MultiplayerGameplay::IsPlayerActive(i)&&g_Players[i].bombInfo.isInUse)eligible=false;
#else
    eligible=eligible&&g_GameManager.globals&&g_GameManager.globals->bombsRemaining>0;
#endif
    return eligible;
}
bool updateBomb(){
    if(!canBomb()){resetBomb();return false;}
    const u32 frame=decisionFrame(),stage=u32(g_GameManager.currentStage);
    if(bombTickValid){
        if(stage!=bombTickStage||frame<bombTickFrame)resetBomb();
        else if(frame==bombTickFrame)return bombPulse;
    }
    bombTickValid=true;bombTickFrame=frame;bombTickStage=stage;
    // A fully checked search with no surviving route triggers immediately.
    // Running out of work budget is uncertainty, not proof of no route.
    bombPulse=(result.flags&NoSafePath)&&
        !(result.flags&(BudgetExhausted|IncompleteWorld|InvalidWorld|Inactive));
    return bombPulse;
}
std::uint16_t deliverInput(std::uint16_t wanted){
    const u32 frame=decisionFrame(),stage=u32(g_GameManager.currentStage);
    if(inputTickValid&&(stage!=inputTickStage||frame<inputTickFrame))resetInput();
    const bool bombAllowed=canBomb();
    if(!bombAllowed){inputDelay.cancelButtons(TH_BUTTON_BOMB);wanted&=~TH_BUTTON_BOMB;}
    if(!inputTickValid||frame!=inputTickFrame){
        inputTickValid=true;inputTickFrame=frame;inputTickStage=stage;
        deliveredInput=inputDelay.tick(wanted);
    }
    // A delayed bomb never survives death/protection or becomes a deathbomb.
    if(!bombAllowed)deliveredInput&=~TH_BUTTON_BOMB;
    return deliveredInput;
}
void add(const Hazard& h){if(count<hazards.size())hazards[count++]=h;else overflow=true;}
void formation(World& w){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[teammateSlot];
    if(rescueWanted()){
        // Approach the drifting ghost using normal movement and collision search.
        w.formationGoal={std::clamp(human.pos.x,w.bounds.left,w.bounds.right),
                         std::clamp(human.pos.y,w.bounds.top,w.bounds.bottom)};
        w.formationWeight={.08f,.08f};w.formationSlack={3,3};
        const float dx=w.player.position.x-human.pos.x,dy=w.player.position.y-human.pos.y;
        if(dx*dx+dy*dy<=18.f*18.f&&!w.committedFrames)
            w.requiredFirstFocus=(heldAction&Focus)?0:1;
        return;
    }
    if(!MultiplayerGameplay::IsMultiplayer()||!MultiplayerGameplay::IsPlayerActive(teammateSlot)||!human.calcChain||
       (!canMove(human)))return;
    if(teamMode==0){
        if(!followHistory.count)return;
        const auto delayed=followHistory.delayed();
        w.formationGoal={std::clamp(delayed.x,w.bounds.left,w.bounds.right),
                         std::clamp(delayed.y,w.bounds.top,w.bounds.bottom)};
        w.formationWeight={.035f,.035f};w.formationSlack={4,4};
    }else if(!g_Gui.BossPresent()){
        const float middle=(w.bounds.left+w.bounds.right)*.5f;
        // Keep the assigned side while P1 crosses the centre's 32 px band.
        if(human.pos.x<middle-16)splitSide=1;
        else if(human.pos.x>middle+16)splitSide=-1;
        else if(!splitSide)splitSide=w.player.position.x>=middle?1:-1;
        w.formationGoal={middle+splitSide*(w.bounds.right-w.bounds.left)*.25f,w.player.position.y};
        w.formationWeight={.025f,0};w.formationSlack={24,0};
    }
#else
    (void)w;
#endif
}
void lasers(float scale,int horizon){
    for(const auto& laser:g_BulletManager.lasers){
        if(!laser.isInUse)continue;
        float start=laser.startOffset,end=laser.endOffset,timer=(float(laser.timer.current)+laser.timer.subFrame);
        int state=laser.state;
        const float cs=std::cos(laser.angle),sn=std::sin(laser.angle);
        // Predict only local laser counters/lengths. No game update or RNG call.
        for(int f=1;f<=horizon&&state<=2;++f){
            end+=scale*laser.speed;start=std::max(0.f,std::max(start,end-laser.startLength));
            if(start>=384)break;
            float length=end-start;bool collidable=false;
            if(state==0){
                if(!(laser.flags&1)){
                    const float width=laser.startTime-std::min(laser.startTime,30)<timer&&laser.startTime>0?
                        timer*laser.width/laser.startTime:1.2f;
                    length=width*.5f; // Preserve TH07's length/width assignment.
                }
                collidable=timer>=laser.hitboxStartTime;
                if(timer>=laser.startTime){state=1;timer=0;collidable=true;}
            }
            if(state==1){
                collidable=true;
                if(timer>=laser.duration){state=2;timer=0;if(laser.endTime==0)break;}
            }
            if(state==2){
                if(!(laser.flags&1)&&laser.endTime>0)
                    length=(laser.width-timer*laser.width/laser.endTime)*.5f;
                collidable=collidable||timer<laser.hitboxEndTime;
                if(timer>=laser.endTime)break;
            }
            if(collidable){
                Hazard h;h.shape=Shape::Box;h.densityScale=0;
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
std::uint16_t Th07Ai::filter(std::uint16_t input){
    const auto& controlled=controlledPlayer();
    // Death, dialogue, protection and leaving play cancel bomb requests;
    // no response is synthesized after being hit.
    if(!enabled||g_Supervisor.curState!=SUPERVISOR_STATE_GAMEMANAGER||
       g_GameManager.replay||g_GameManager.demo||g_GameManager.finished||
       g_Gui.HasCurrentMsgIdx()||!controlled.calcChain||controlled.playerState!=PLAYER_STATE_ALIVE||
       controlled.bombInfo.isInUse)resetBomb();
    active=enabled&&g_Supervisor.curState==SUPERVISOR_STATE_GAMEMANAGER&&
        !g_GameManager.replay&&!g_GameManager.demo&&g_GameManager.notInMenu&&!g_GameManager.isInPauseMenu&&
        !g_GameManager.isInRetryMenu&&!g_GameManager.finished&&!g_GameManager.isTimeStopped&&
        !g_Gui.HasCurrentMsgIdx()&&controlled.calcChain&&controlled.shooterData&&
        canMove(controlled);
#ifdef TH_ENABLE_THPRAC
    active=active&&!PracticeRuntime::CapturesGameInput();
#endif
    if(!active){if(wasActive)planner.reset();resetInput();resetAction();wasRescuing=false;wasActive=false;lastFrame=std::numeric_limits<u32>::max();return input;}
    if(actionTickValid&&(actionTickStage!=u32(g_GameManager.currentStage)||decisionFrame()<actionTickFrame))resetAction();
    const bool rescuing=rescueWanted();
    if(rescuing!=wasRescuing){resetInput();wasRescuing=rescuing;} // Do not donate again from stale rescue taps.
    applyTeamWeights();
    if(!wasActive||g_GameManager.currentStage!=int(lastStage)||decisionFrame()<lastFrame)planner.reset();
    wasActive=true;
    constexpr std::uint16_t owned=TH_BUTTON_DIRECTION|TH_BUTTON_FOCUS|TH_BUTTON_SHOOT|TH_BUTTON_BOMB;
    const auto apply=[&](std::uint16_t ai){return std::uint16_t((input&~owned)|ai);};
    if(lastFrame==decisionFrame()&&lastStage==u32(g_GameManager.currentStage))return apply(deliverInput(cached));
    lastFrame=decisionFrame();lastStage=u32(g_GameManager.currentStage);
    const auto begin=SDL_GetPerformanceCounter();count=0;overflow=false;
    const float scale=g_Supervisor.effectiveFramerateMultiplier;
    World w;const auto& p=controlled;const auto& gm=g_GameManager;
    w.commitFirstAction=true;w.committedAction=heldAction;
    w.committedFrames=actionRemaining+(actionTickValid&&actionTickFrame==decisionFrame()?1:0);
    w.player.position={p.pos.x,p.pos.y};
    w.player.boxHalfSize=Vec2{p.hitboxSize.x,p.hitboxSize.y};
    // TH07's Bomb speed modifiers are equal on both axes (Master Spark: 0.3).
    // Keep planning during Bomb, using its real slowdown instead of stopping.
    const float movementScale=scale*std::min(p.horizontalMovementSpeedMultiplierDuringBomb,
                                             p.verticalMovementSpeedMultiplierDuringBomb);
    w.player.fastSpeed=p.shooterData->speed*movementScale;
    w.player.slowSpeed=p.shooterData->speedFocus*movementScale;
    // Border uses invulnerabilityTimer as a gauge, not a licence to collide.
    // Keep full hazard checks during activation, the Border and its aftereffect.
    w.player.invulnerableFrames=protectedFrames(p);
    w.bounds={gm.playerMovementAreaTopLeftPos.x,gm.playerMovementAreaTopLeftPos.y,
        gm.playerMovementAreaTopLeftPos.x+gm.playerMovementAreaSize.x,
        gm.playerMovementAreaTopLeftPos.y+gm.playerMovementAreaSize.y};
    w.goal={(w.bounds.left+w.bounds.right)*.5f,w.bounds.bottom-64};
    formation(w);
    // Rank once per tick rather than inspect every item inside every route.
    static std::array<std::pair<float,Pickup>,MAX_ITEMS+1> rankedItems{};std::size_t itemCount=0;
    for(const auto& item:g_ItemManager.items){
        if(!item.isInUse||item.itemType==ITEM_POINT_BULLET||item.state!=0||item.autoCollect)continue;
        const auto& pos=item.pos;
        // Only pursue drops after they enter the lower half of the field.
        if(pos.y<(w.bounds.top+w.bounds.bottom)*.5f)continue;
        if(pos.x<w.bounds.left-16||pos.x>w.bounds.right+16||pos.y>w.bounds.bottom+16)continue;
        const float value=item.itemType==ITEM_LIFE?4.f:item.itemType==ITEM_BOMB?3.f:
            (item.itemType==ITEM_FULL_POWER||item.itemType==ITEM_POWER_BIG)?2.f:item.itemType==ITEM_POINT?.5f:1.f;
        Pickup point;point.position={pos.x,pos.y};
        point.velocity={std::clamp(pos.x-item.prevPos.x,-4.f,4.f),std::clamp(pos.y-item.prevPos.y,-4.f,4.f)};
        point.radius=std::min(p.grabItemSize.x,p.grabItemSize.y);point.value=value;
        rankedItems[itemCount++]={std::hypot(pos.x-p.pos.x,pos.y-p.pos.y)/value,point};
    }
    std::sort(rankedItems.begin(),rankedItems.begin()+itemCount,[](const auto& a,const auto& b){return a.first<b.first;});
    w.pickupCount=std::min(itemCount,pickups.size());
    if(rescueWanted())w.pickupCount=0;
    for(std::size_t j=0;j<w.pickupCount;++j)pickups[j]=rankedItems[j].second;
    w.pickups=pickups.data();
    static std::array<std::pair<float,AttackTarget>,MAX_ENEMIES+1> rankedTargets{};std::size_t enemyCount=0;
    for(const auto& enemy:g_EnemyManager.enemies){
        if(!enemy.active||enemy.hasNoCollision||enemy.invisibleOnBomb||!enemy.canDie||!enemy.canBeDamaged||!enemy.isHittable||enemy.life<=0)continue;
        const auto& pos=enemy.pos;
        if(pos.x<0||pos.x>384||pos.y<0||pos.y>448)continue;
        AttackTarget target;target.position={pos.x,pos.y};
        target.velocity={pos.x-enemy.prevPos.x,pos.y-enemy.prevPos.y};
        target.halfWidth=std::max(8.f,enemy.hitboxSize.x*.5f);target.value=1;
        rankedTargets[enemyCount++]={std::abs(pos.x-p.pos.x)+(pos.y>=p.pos.y?448.f:0.f),target};
    }
    std::sort(rankedTargets.begin(),rankedTargets.begin()+enemyCount,[](const auto& a,const auto& b){return a.first<b.first;});
    w.targetCount=std::min(enemyCount,targets.size());
    if(rescueWanted())w.targetCount=0;
    for(std::size_t j=0;j<w.targetCount;++j)targets[j]=rankedTargets[j].second;
    w.targets=targets.data();
    for(const auto& bullet:g_BulletManager.bullets){
        if(bullet.state!=BULLET_NORMAL)continue;
        Hazard h;h.shape=Shape::Box;h.position={bullet.pos.x,bullet.pos.y};
        // TH07 stores already-scaled velocity and moves by velocity directly.
        h.velocity={bullet.velocity.x,bullet.velocity.y};
        if((bullet.exFlags&0x10)&&bullet.commandStates[1].timer.current<bullet.commandStates[1].duration){
            const auto& c=bullet.commandStates[1];
            h.acceleration={c.vec3.x*scale,c.vec3.y*scale};
        }
        if((bullet.exFlags&0x20)&&bullet.commandStates[2].timer.current<bullet.commandStates[2].duration){
            const auto& c=bullet.commandStates[2];
            const float angle=bullet.angle+c.angle*scale,speed=(bullet.speed+c.speed*scale)*scale;
            h.acceleration.x+=std::cos(angle)*speed-h.velocity.x;
            h.acceleration.y+=std::sin(angle)*speed-h.velocity.y;
        }
        h.halfSize={bullet.sprites.grazeSize.x*.5f,bullet.sprites.grazeSize.y*.5f};add(h);
    }
    for(const auto& enemy:g_EnemyManager.enemies){
        if(!enemy.active||enemy.hasNoCollision||enemy.invisibleOnBomb||!enemy.canDie||!enemy.hasContactHitbox)continue;
        Hazard h;h.shape=Shape::Box;h.densityScale=0;h.position={enemy.pos.x,enemy.pos.y};
        h.velocity={enemy.pos.x-enemy.prevPos.x,enemy.pos.y-enemy.prevPos.y};
        h.halfSize={enemy.hitboxSize.x/3.f,enemy.hitboxSize.y/3.f};add(h);
        if(enemy.trailFlags){
            for(int j=1;j<enemy.trailInterval&&j<96;j+=6){
                const auto& node=enemy.enemyHistory[j];
                if(node.pos.x<=-990)continue;
                Hazard trail=h;trail.position={node.pos.x,node.pos.y};trail.velocity={0,0};
                if(enemy.trailFlags&2){
                    const float shrink=1.f-float(j)/enemy.trailInterval;
                    trail.halfSize={h.halfSize.x*shrink,h.halfSize.y*shrink};
                }
                add(trail);
            }
        }
    }
    lasers(scale,planner.settings().horizon);
    w.hazards=hazards.data();w.hazardCount=count;w.incomplete=overflow;
    result=planner.decide(w);decisions++;
    elapsedMs=1000.*double(SDL_GetPerformanceCounter()-begin)/double(SDL_GetPerformanceFrequency());
    if(result.flags&(IncompleteWorld|InvalidWorld)){resetBomb();resetInput();resetAction();cached=input&owned;return input;}
    cached=map(takeAction(result.buttons)|(result.buttons&Fire));
    if(rescueWanted())cached&=~TH_BUTTON_SHOOT; // Native life-transfer gesture requires no shooting.
    if(updateBomb())cached|=TH_BUTTON_BOMB;
    return apply(deliverInput(cached));
}
void Th07Ai::configureSession(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.replay||g_GameManager.demo)return;
    MultiplayerGameplay::SessionState session;
    session.playerCount=2;session.localPlayer=0;session.showStagePlayerNames=true;
    session.showContributionStats=true;
    session.players[0].active=session.players[1].active=true;
    session.players[0].character=g_GameManager.character;session.players[0].shot=g_GameManager.shotType;
    session.players[1].character=g_GameManager.character==0?1:0;session.players[1].shot=0;
    MultiplayerGameplay::Configure(session);
    splitSide=0;followHistory.reset();applyTeamWeights();resetBomb();
    resetInput();resetAction();wasRescuing=false;
    humanBombRequested=previousHumanBomb=false;
    planner.reset();lastFrame=std::numeric_limits<u32>::max();
#endif
}
void Th07Ai::resetPracticeResources(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.replay||g_GameManager.demo||!MultiplayerGameplay::IsMultiplayer())return;
    bool practice=g_GameManager.practice!=0;
#ifdef TH_ENABLE_THPRAC
    practice=practice||PracticeRuntime::Active(); // Extra practice clears the vanilla flag.
#endif
    if(!practice)return;
    // THPrac applies P1's configured stocks after player/replay registration.
    // Copy the finalized values, not the prior attempt or the co-op default.
    for(u8 seat=1;seat<MultiplayerGameplay::GetPlayerCount();++seat){
        if(!MultiplayerGameplay::IsPlayerActive(seat))continue;
        SetPlayerLives(seat,GetPlayerLives(0));
        SetPlayerBombs(seat,GetPlayerBombs(0));
        SetPlayerPower(seat,GetPlayerPower(0));
    }
    // Registration already captured an early resource snapshot. Replace it
    // with the actual practice start so recorded AI inputs use the same stocks.
    ReplayExtension::MultiplayerPlayerResourceSnapshot resources[TH07_MULTI_MAX_PLAYERS]{};
    for(u8 seat=0;seat<MultiplayerGameplay::GetPlayerCount();++seat){
        resources[seat].lives=GetPlayerLives(seat);
        resources[seat].bombs=GetPlayerBombs(seat);
        resources[seat].power=GetPlayerPower(seat);
        resources[seat].challengeDeaths=g_MultiplayerContributionStats[seat].challengeDeaths;
    }
    ReplayExtension::CaptureMultiplayerStageResources(
        std::min(g_GameManager.currentStage-1,6),resources,MultiplayerGameplay::GetPlayerCount());
#endif
}
void Th07Ai::commitLocal(std::uint16_t humanInput){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.replay||g_GameManager.demo){
        if(ownsInputs){Netplay::Input::ClearPlayerButtonOverrides();ownsInputs=false;}
        followHistory.reset();resetBomb();resetInput();resetAction();active=0;return;
    }
    if(!MultiplayerGameplay::IsMultiplayer()){
        if(ownsInputs){Netplay::Input::ClearPlayerButtonOverrides();ownsInputs=false;}
        followHistory.reset();
        resetBomb();
        resetInput();
        resetAction();
        return;
    }
    const bool humanBomb=(humanInput&TH_BUTTON_BOMB)!=0;
    humanBombRequested=humanBomb&&!previousHumanBomb&&GetPlayerBombs(0)>0&&
        (canMove(g_Players[0])&&!hasBorderProtection(g_Players[0]));
    previousHumanBomb=humanBomb;
    sampleTeammate();
    std::array<std::uint16_t,TH07_MULTI_MAX_PLAYERS> inputs{};
    inputs[0]=humanInput;inputs[1]=filter(0);
    Netplay::Input::SetPlayerButtonOverrides(inputs.data(),inputs.size());ownsInputs=true;
#else
    (void)humanInput;
#endif
}
extern "C" {
AI_EXPORT void th07_ai_enable(int value){enabled=value!=0;planner.reset();resetBomb();resetInput();resetAction();lastFrame=std::numeric_limits<u32>::max();}
AI_EXPORT void th07_ai_action_duration(int frames){
    auto s=planner.settings();s.actionFrames=std::clamp(frames,1,12);
    s.horizon=std::max(requestedHorizon,s.actionFrames);planner.configure(s);
    planner.reset();resetAction();resetInput();lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_input_delay(int delayFrames){
    inputDelay.configure(delayFrames);inputTickValid=false;deliveredInput=0;
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_auto_bomb(int value){
    autoBomb=value!=0;resetBomb();
    if(!autoBomb){inputDelay.cancelButtons(TH_BUTTON_BOMB);deliveredInput&=~TH_BUTTON_BOMB;}
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_team_mode(int mode){
    if(mode<0||mode>1)return;
    teamMode=mode;splitSide=0;planner.reset();lastFrame=std::numeric_limits<u32>::max();
    applyTeamWeights();
}
AI_EXPORT void th07_ai_follow_attack(int value){
    const bool preserve=value!=0;
    if(preserveFollowAttack==preserve)return;
    preserveFollowAttack=preserve;applyTeamWeights();
}
AI_EXPORT void th07_ai_configure(int horizon,int beam,int budget){
    requestedHorizon=std::clamp(horizon,1,Planner::MaxHorizon);
    auto s=planner.settings();s.horizon=std::max(requestedHorizon,s.actionFrames);s.beamWidth=beam;s.maxHazards=4096;
    s.maxCollisionTests=std::uint32_t(std::max(1,budget));planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_objectives(float pickup,float attack){
    splitAttackWeight=attack;
    auto s=planner.settings();s.pickupWeight=pickup;s.pickupApproachWeight=pickup*.0175f;s.attackWeight=attackWeightLocked()?0:attack;
    planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_danger(float weight){
    auto s=planner.settings();s.dangerWeight=weight;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_edges(float weight,float distance){
    splitEdgeWeight=weight;
    auto s=planner.settings();s.edgeWeight=edgeWeightLocked()?0:weight;s.edgeDistance=distance;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_density(float weight){
    auto s=planner.settings();s.densityWeight=weight;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th07_ai_switch_weight(float weight){
    auto s=planner.settings();s.switchWeight=weight;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT double th07_ai_metric(int field){
    switch(field){case 0:return enabled;case 1:return active;case 2:return elapsedMs;case 3:return result.flags;
    case 4:return double(count);case 5:return result.stats.collisionTests;case 6:return result.stats.completedFrames;
    case 7:return double(decisions);case 8:return g_GameManager.currentStage;case 9:return result.safeFrames;
    case 22:return teamWeightsLocked();case 23:return rescueWanted();
    case 24:return deliveredInput;
    case 26:return actionRemaining;case 27:return planner.settings().horizon;
    case 28:return edgeWeightLocked();
    case 30:return hasBorderProtection(controlledPlayer());
    case 31:return protectedFrames(controlledPlayer());
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    case 10:return MultiplayerGameplay::GetPlayerCount();case 11:return GetMultiplayerBossDamageMultiplier();
    case 12:return GetPlayerLives(teammateSlot);case 13:return GetPlayerLives(controlledSlot);
    case 14:return GetPlayerPower(teammateSlot);case 15:return GetPlayerPower(controlledSlot);
    case 16:return g_Players[controlledSlot].playerState;case 17:return GetPlayerBombs(teammateSlot);case 18:return GetPlayerBombs(controlledSlot);
    case 29:return controlledSlot;
#endif
    default:return 0;}
}
}
