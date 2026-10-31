// Compiled into an isolated TH06 local runtime. Reads the live collision
// objects; only the returned logical input word enters authoritative state.
#include "AvoidanceHook.hpp"
#include <eagler/ai/Planner.hpp>
#include <eagler/ai/BombDecision.hpp>
#include <eagler/ai/InputImpairment.hpp>
#include "Player.hpp"
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
Planner planner;
BombDecision bombDecision;
InputImpairment inputImpairment;
bool inputTickValid=false,wasRescuing=false;
u32 inputTickFrame=0,inputTickStage=0;
std::uint16_t deliveredInput=0;
void resetInput(){inputImpairment.reset();inputTickValid=false;deliveredInput=0;}
bool autoBomb=true,bombTickValid=false,bombPulse=false,humanBombRequested=false,previousHumanBomb=false;
u32 bombTickFrame=0,bombTickStage=0;
void resetBomb(){bombDecision.reset();bombTickValid=false;bombPulse=false;}
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
bool ownsInputs=false;
int teamMode=0,splitSide=0; // 0 follows P1; 1 covers the opposite side on stage.
float splitAttackWeight=.2f,splitEdgeWeight=1.f;
const float splitMovementWeight=planner.settings().movementWeight;
bool humanUnavailable(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[0];
    return human.chainCalc&&human.playerState!=PLAYER_STATE_ALIVE&&human.playerState!=PLAYER_STATE_INVULNERABLE;
#else
    return false;
#endif
}
bool rescueWanted(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& ai=g_Players[1];
    return MultiplayerGameplay::IsMultiplayer()&&MultiplayerGameplay::IsPlayerActive(0)&&
        g_Players[0].chainCalc&&g_Players[0].playerState==PLAYER_STATE_REVIVABLE&&GetPlayerLives(1)>0&&
        ai.chainCalc&&(ai.playerState==PLAYER_STATE_ALIVE||ai.playerState==PLAYER_STATE_INVULNERABLE);
#else
    return false;
#endif
}
bool teamWeightsLocked(){return rescueWanted()||(teamMode==0&&!humanUnavailable());}
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
    const float attack=locked?0:splitAttackWeight,edge=locked?0:splitEdgeWeight,movement=locked?0:splitMovementWeight;
    if(s.attackWeight==attack&&s.edgeWeight==edge&&s.movementWeight==movement)return;
    s.attackWeight=attack;s.edgeWeight=edge;s.movementWeight=movement;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
void sampleTeammate(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[0];
    if(!MultiplayerGameplay::IsPlayerActive(0)||!human.chainCalc||
       (human.playerState!=PLAYER_STATE_ALIVE&&human.playerState!=PLAYER_STATE_INVULNERABLE)){
        followHistory.reset();return;
    }
    if(g_Supervisor.curState!=SUPERVISOR_STATE_GAMEMANAGER||g_GameManager.isInGameMenu||
       g_GameManager.isInRetryMenu||g_GameManager.isGameCompleted||g_GameManager.isTimeStopped||
       g_Gui.HasCurrentMsgIdx())return;
#ifdef TH_ENABLE_THPRAC
    if(PracticeRuntime::CapturesGameInput())return;
#endif
    followHistory.sample({human.positionCenter.x,human.positionCenter.y},
                         g_GameManager.gameFrames,u32(g_GameManager.currentStage));
#endif
}
::Player& controlledPlayer(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    return g_Players[1];
#else
    return g_Player;
#endif
}
bool canBomb(){
    const auto& player=controlledPlayer();
    bool eligible=autoBomb&&player.playerState==PLAYER_STATE_ALIVE&&!player.bombInfo.isInUse&&
                  player.bombInfo.calc&&player.respawnTimer!=0;
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    eligible=eligible&&GetPlayerBombs(1)>0&&player.teamBombProtectionTimer.AsFrames()<=0&&!humanBombRequested;
    for(int i=0;i<MultiplayerGameplay::GetPlayerCount();++i)
        if(i!=1&&MultiplayerGameplay::IsPlayerActive(i)&&g_Players[i].bombInfo.isInUse)eligible=false;
#else
    eligible=eligible&&g_GameManager.bombsRemaining>0;
#endif
    return eligible;
}
bool updateBomb(){
    if(!canBomb()){resetBomb();return false;}
    const u32 frame=g_GameManager.gameFrames,stage=u32(g_GameManager.currentStage);
    if(bombTickValid){
        if(stage!=bombTickStage||frame<bombTickFrame)resetBomb();
        else if(frame==bombTickFrame)return bombPulse;
    }
    bombTickValid=true;bombTickFrame=frame;bombTickStage=stage;
    bombPulse=bombDecision.tick(result.pressure.score,result.pressure.valid);
    return bombPulse;
}
std::uint16_t deliverInput(std::uint16_t wanted){
    const u32 frame=g_GameManager.gameFrames,stage=u32(g_GameManager.currentStage);
    if(inputTickValid&&(stage!=inputTickStage||frame<inputTickFrame))resetInput();
    const bool bombAllowed=canBomb();
    if(!bombAllowed){inputImpairment.cancelButtons(TH_BUTTON_BOMB);wanted&=~TH_BUTTON_BOMB;}
    if(!inputTickValid||frame!=inputTickFrame){
        inputTickValid=true;inputTickFrame=frame;inputTickStage=stage;
        deliveredInput=inputImpairment.tick(wanted,TH_BUTTON_BOMB);
    }
    // A delayed bomb never survives death/protection or becomes a deathbomb.
    if(!bombAllowed)deliveredInput&=~TH_BUTTON_BOMB;
    return deliveredInput;
}
void add(const Hazard& h){if(count<hazards.size())hazards[count++]=h;else overflow=true;}
void formation(World& w){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    const auto& human=g_Players[0];
    if(rescueWanted()){
        // Approach the drifting ghost using normal movement and collision search.
        w.formationGoal={std::clamp(human.positionCenter.x,w.bounds.left,w.bounds.right),
                         std::clamp(human.positionCenter.y,w.bounds.top,w.bounds.bottom)};
        w.formationWeight={.08f,.08f};w.formationSlack={3,3};
        const float dx=w.player.position.x-human.positionCenter.x,dy=w.player.position.y-human.positionCenter.y;
        if(dx*dx+dy*dy<=18.f*18.f)
            w.requiredFirstFocus=(g_GameManager.gameFrames%4)<2?1:0;
        return;
    }
    if(!MultiplayerGameplay::IsMultiplayer()||!MultiplayerGameplay::IsPlayerActive(0)||!human.chainCalc||
       (human.playerState!=PLAYER_STATE_ALIVE&&human.playerState!=PLAYER_STATE_INVULNERABLE))return;
    if(teamMode==0){
        if(!followHistory.count)return;
        const auto delayed=followHistory.delayed();
        w.formationGoal={std::clamp(delayed.x,w.bounds.left,w.bounds.right),
                         std::clamp(delayed.y,w.bounds.top,w.bounds.bottom)};
        w.formationWeight={.035f,.035f};w.formationSlack={4,4};
    }else if(!g_Gui.BossPresent()){
        const float middle=(w.bounds.left+w.bounds.right)*.5f;
        // Keep the assigned side while P1 crosses the centre's 32 px band.
        if(human.positionCenter.x<middle-16)splitSide=1;
        else if(human.positionCenter.x>middle+16)splitSide=-1;
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
std::uint16_t Th06Ai::filter(std::uint16_t input){
    const auto& controlled=controlledPlayer();
    const bool key=SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F8];
    if(key&&!f8){enabled=!enabled;planner.reset();lastFrame=std::numeric_limits<u32>::max();}f8=key;
    // Pause/tool menus freeze the pending delay. Death, dialogue, protection
    // and leaving play cancel it; no response is synthesized after being hit.
    if(!enabled||g_Supervisor.curState!=SUPERVISOR_STATE_GAMEMANAGER||
       g_GameManager.isInReplay||g_GameManager.demoMode||g_GameManager.isGameCompleted||
       g_Gui.HasCurrentMsgIdx()||!controlled.chainCalc||controlled.playerState!=PLAYER_STATE_ALIVE||
       controlled.bombInfo.isInUse)resetBomb();
    active=enabled&&g_Supervisor.curState==SUPERVISOR_STATE_GAMEMANAGER&&
        !g_GameManager.isInReplay&&!g_GameManager.demoMode&&!g_GameManager.isInGameMenu&&
        !g_GameManager.isInRetryMenu&&!g_GameManager.isGameCompleted&&!g_GameManager.isTimeStopped&&
        !g_Gui.HasCurrentMsgIdx()&&controlled.chainCalc&&
        (controlled.playerState==PLAYER_STATE_ALIVE||controlled.playerState==PLAYER_STATE_INVULNERABLE);
#ifdef TH_ENABLE_THPRAC
    active=active&&!PracticeRuntime::CapturesGameInput();
#endif
    if(!active){if(wasActive)planner.reset();resetInput();wasRescuing=false;wasActive=false;lastFrame=std::numeric_limits<u32>::max();return input;}
    const bool rescuing=rescueWanted();
    if(rescuing!=wasRescuing){resetInput();wasRescuing=rescuing;} // Do not donate again from stale rescue taps.
    applyTeamWeights();
    if(!wasActive||g_GameManager.currentStage!=int(lastStage)||g_GameManager.gameFrames<lastFrame)planner.reset();
    wasActive=true;
    constexpr std::uint16_t owned=TH_BUTTON_DIRECTION|TH_BUTTON_FOCUS|TH_BUTTON_SHOOT|TH_BUTTON_BOMB;
    const auto apply=[&](std::uint16_t ai){return std::uint16_t((input&~owned)|ai);};
    if(lastFrame==g_GameManager.gameFrames&&lastStage==u32(g_GameManager.currentStage))return apply(deliverInput(cached));
    lastFrame=g_GameManager.gameFrames;lastStage=u32(g_GameManager.currentStage);
    const auto begin=SDL_GetPerformanceCounter();count=0;overflow=false;
    const float scale=g_Supervisor.effectiveFramerateMultiplier;
    World w;const auto& p=controlled;const auto& gm=g_GameManager;
    w.measurePressure=autoBomb;
    w.player.position={p.positionCenter.x,p.positionCenter.y};
    // TH06 uses an axis-aligned player square; its circumscribed circle is a
    // conservative fit for the shared planner's circular player geometry.
    w.player.radius=std::hypot(p.hitboxSize.x,p.hitboxSize.y);
    // TH06's Bomb speed modifiers are equal on both axes (Master Spark: 0.3).
    // Keep planning during Bomb, using its real slowdown instead of stopping.
    const float movementScale=scale*std::min(p.horizontalMovementSpeedMultiplierDuringBomb,
                                             p.verticalMovementSpeedMultiplierDuringBomb);
    w.player.fastSpeed=p.characterData.orthogonalMovementSpeed*movementScale;
    w.player.slowSpeed=p.characterData.orthogonalMovementSpeedFocus*movementScale;
    // Player::OnUpdate decrements protection before movement/collision. Only
    // skip danger queries for the remaining guaranteed protected ticks.
    if(p.playerState==PLAYER_STATE_INVULNERABLE)
        w.player.invulnerableFrames=std::max(0,p.invulnerabilityTimer.AsFrames()-1);
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    w.player.invulnerableFrames=std::max(w.player.invulnerableFrames,
                                       std::max(0,p.teamBombProtectionTimer.AsFrames()-1));
#endif
    w.bounds={gm.playerMovementAreaTopLeftPos.x,gm.playerMovementAreaTopLeftPos.y,
        gm.playerMovementAreaTopLeftPos.x+gm.playerMovementAreaSize.x,
        gm.playerMovementAreaTopLeftPos.y+gm.playerMovementAreaSize.y};
    w.goal={(w.bounds.left+w.bounds.right)*.5f,w.bounds.bottom-64};
    formation(w);
    // Rank once per tick rather than inspect every item inside every route.
    std::array<std::pair<float,Pickup>,513> rankedItems{};std::size_t itemCount=0;
    for(const auto& item:g_ItemManager.items){
        if(!item.isInUse||item.itemType==ITEM_POINT_BULLET||item.state==1)continue;
        const auto& pos=item.currentPosition;
        // Only pursue drops after they enter the lower half of the field.
        if(pos.y<(w.bounds.top+w.bounds.bottom)*.5f)continue;
        if(pos.x<w.bounds.left-16||pos.x>w.bounds.right+16||pos.y>w.bounds.bottom+16)continue;
        const float value=item.itemType==ITEM_LIFE?4.f:item.itemType==ITEM_BOMB?3.f:
            (item.itemType==ITEM_FULL_POWER||item.itemType==ITEM_POWER_BIG)?2.f:item.itemType==ITEM_POINT?.5f:1.f;
        Pickup point;point.position={pos.x,pos.y};
        point.velocity={std::clamp(pos.x-item.prevPosition.x,-4.f,4.f),std::clamp(pos.y-item.prevPosition.y,-4.f,4.f)};
        point.radius=std::min(p.grabItemSize.x,p.grabItemSize.y);point.value=value;
        rankedItems[itemCount++]={std::hypot(pos.x-p.positionCenter.x,pos.y-p.positionCenter.y)/value,point};
    }
    std::sort(rankedItems.begin(),rankedItems.begin()+itemCount,[](const auto& a,const auto& b){return a.first<b.first;});
    w.pickupCount=std::min(itemCount,pickups.size());
    if(rescueWanted())w.pickupCount=0;
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
    if(rescueWanted())w.targetCount=0;
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
        Hazard h;h.shape=Shape::Box;h.densityScale=0;h.position={enemy.position.x,enemy.position.y};
        h.velocity={enemy.position.x-enemy.prevPosition.x,enemy.position.y-enemy.prevPosition.y};
        h.halfSize={enemy.hitboxDimensions.x/3.f,enemy.hitboxDimensions.y/3.f};add(h);
    }
    lasers(scale,planner.settings().horizon);
    w.hazards=hazards.data();w.hazardCount=count;w.incomplete=overflow;
    result=planner.decide(w);decisions++;
    elapsedMs=1000.*double(SDL_GetPerformanceCounter()-begin)/double(SDL_GetPerformanceFrequency());
    if(result.flags&(IncompleteWorld|InvalidWorld)){resetBomb();resetInput();cached=input&owned;return input;}
    cached=map(result.buttons);
    if(rescueWanted())cached&=~TH_BUTTON_SHOOT; // Native life-transfer gesture requires no shooting.
    if(updateBomb())cached|=TH_BUTTON_BOMB;
    return apply(deliverInput(cached));
}
void Th06Ai::configureSession(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.isInReplay||g_GameManager.demoMode)return;
    MultiplayerGameplay::SessionState session;
    session.playerCount=2;session.localPlayer=0;session.showStagePlayerNames=true;
    session.showContributionStats=true;
    session.players[0].active=session.players[1].active=true;
    session.players[0].character=g_GameManager.character;session.players[0].shot=g_GameManager.shotType;
    session.players[1].character=g_GameManager.character==0?1:0;session.players[1].shot=0;
    MultiplayerGameplay::Configure(session);
    splitSide=0;followHistory.reset();applyTeamWeights();resetBomb();
    resetInput();inputImpairment.startRun();wasRescuing=false;
    humanBombRequested=previousHumanBomb=false;
    planner.reset();lastFrame=std::numeric_limits<u32>::max();
#endif
}
void Th06Ai::resetPracticeResources(){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.isInReplay||g_GameManager.demoMode||!MultiplayerGameplay::IsMultiplayer())return;
    bool practice=g_GameManager.isInPracticeMode!=0;
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
    ReplayExtension::MultiplayerPlayerResourceSnapshot resources[TH06_MULTI_MAX_PLAYERS]{};
    for(u8 seat=0;seat<MultiplayerGameplay::GetPlayerCount();++seat){
        resources[seat].lives=GetPlayerLives(seat);
        resources[seat].bombs=GetPlayerBombs(seat);
        resources[seat].power=GetPlayerPower(seat);
        resources[seat].challengeDeaths=g_MultiplayerContributionStats[seat].challengeDeaths;
    }
    ReplayExtension::CaptureMultiplayerStageResources(
        g_GameManager.currentStage-1,resources,MultiplayerGameplay::GetPlayerCount());
#endif
}
void Th06Ai::commitLocal(std::uint16_t humanInput){
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    if(g_GameManager.isInReplay||g_GameManager.demoMode){
        if(ownsInputs){Netplay::Input::ClearPlayerButtonOverrides();ownsInputs=false;}
        followHistory.reset();resetBomb();resetInput();active=0;return;
    }
    if(!MultiplayerGameplay::IsMultiplayer()){
        if(ownsInputs){Netplay::Input::ClearPlayerButtonOverrides();ownsInputs=false;}
        followHistory.reset();
        resetBomb();
        resetInput();
        return;
    }
    const bool humanBomb=(humanInput&TH_BUTTON_BOMB)!=0;
    humanBombRequested=humanBomb&&!previousHumanBomb&&GetPlayerBombs(0)>0&&
        (g_Players[0].playerState==PLAYER_STATE_ALIVE||g_Players[0].playerState==PLAYER_STATE_INVULNERABLE);
    previousHumanBomb=humanBomb;
    sampleTeammate();
    std::array<std::uint16_t,TH06_MULTI_MAX_PLAYERS> inputs{};
    inputs[0]=humanInput;inputs[1]=filter(0);
    Netplay::Input::SetPlayerButtonOverrides(inputs.data(),inputs.size());ownsInputs=true;
#else
    (void)humanInput;
#endif
}
extern "C" {
AI_EXPORT void th06_ai_enable(int value){enabled=value!=0;planner.reset();resetBomb();resetInput();lastFrame=std::numeric_limits<u32>::max();}
AI_EXPORT void th06_ai_input_config(int delayFrames,float failureRate){
    inputImpairment.configure(delayFrames,failureRate);inputTickValid=false;deliveredInput=0;
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_bomb_config(int value,float threshold,int confirmationFrames,int delayFrames){
    autoBomb=value!=0;bombDecision.configure(threshold,confirmationFrames,delayFrames);
    bombTickValid=false;bombPulse=false;lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_team_mode(int mode){
    if(mode<0||mode>1)return;
    teamMode=mode;splitSide=0;planner.reset();lastFrame=std::numeric_limits<u32>::max();
    applyTeamWeights();
}
AI_EXPORT void th06_ai_configure(int horizon,int beam,int budget){
    auto s=planner.settings();s.horizon=horizon;s.beamWidth=beam;s.maxHazards=4096;
    s.maxCollisionTests=std::uint32_t(std::max(1,budget));planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_objectives(float pickup,float attack){
    splitAttackWeight=attack;
    auto s=planner.settings();s.pickupWeight=pickup;s.pickupApproachWeight=pickup*.0175f;s.attackWeight=teamWeightsLocked()?0:attack;
    planner.configure(s);lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_danger(float weight){
    auto s=planner.settings();s.dangerWeight=weight;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_edges(float weight,float distance){
    splitEdgeWeight=weight;
    auto s=planner.settings();s.edgeWeight=teamWeightsLocked()?0:weight;s.edgeDistance=distance;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT void th06_ai_density(float weight){
    auto s=planner.settings();s.densityWeight=weight;planner.configure(s);
    lastFrame=std::numeric_limits<u32>::max();
}
AI_EXPORT double th06_ai_metric(int field){
    switch(field){case 0:return enabled;case 1:return active;case 2:return elapsedMs;case 3:return result.flags;
    case 4:return double(count);case 5:return result.stats.collisionTests;case 6:return result.stats.completedFrames;
    case 7:return double(decisions);case 8:return g_GameManager.currentStage;case 9:return result.safeFrames;
    case 19:return active&&autoBomb&&result.pressure.valid?result.pressure.score:-1;
    case 20:return bombDecision.highFrames();
    case 21:return bombDecision.pending()?bombDecision.remainingFrames():-1;
    case 22:return teamWeightsLocked();case 23:return rescueWanted();
    case 24:return deliveredInput;case 25:return inputImpairment.failedPresses();
#ifdef TH_ENABLE_MULTIPLAYER_GAMEPLAY
    case 10:return MultiplayerGameplay::GetPlayerCount();case 11:return GetMultiplayerBossDamageMultiplier();
    case 12:return GetPlayerLives(0);case 13:return GetPlayerLives(1);
    case 14:return GetPlayerPower(0);case 15:return GetPlayerPower(1);
    case 16:return g_Players[1].playerState;case 17:return GetPlayerBombs(0);case 18:return GetPlayerBombs(1);
#endif
    default:return 0;}
}
}
