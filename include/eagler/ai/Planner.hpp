#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eagler::ai {
struct Vec2 { float x=0, y=0; };
struct Bounds { float left=0, top=0, right=384, bottom=448; };
enum class Shape : std::uint32_t { Circle=0, Box=1, Capsule=2 };
// Positions, sizes and speeds are in playfield pixels and logical frames.
// Capsule endpoints have independent linear velocities. Box angles are radians.
struct Hazard {
    Shape shape=Shape::Circle;
    Vec2 position{}, velocity{}, acceleration{};
    Vec2 end{}, endVelocity{};
    Vec2 halfSize{};
    float radius=0, angle=0, angularVelocity=0;
    int activeFrom=0, activeUntil=24;
    float densityScale=1; // 0 excludes lasers/enemies from bullet density.
};
struct Player {
    Vec2 position{};
    float radius=2, fastSpeed=4, slowSpeed=2;
    int invulnerableFrames=0;
};
// Semantic buttons; the title adapter maps these to its own input bits.
enum Button : std::uint32_t {
    Fire=1, Focus=2, Left=4, Right=8, Up=16, Down=32
};
struct Pickup {
    Vec2 position{},velocity{};
    float radius=8,value=1;
};
struct AttackTarget {
    Vec2 position{},velocity{};
    float halfWidth=16,value=1;
};
struct World {
    Player player;
    Bounds bounds;
    Vec2 goal{192,360};
    const Hazard* hazards=nullptr;
    std::size_t hazardCount=0;
    const Pickup* pickups=nullptr;
    std::size_t pickupCount=0;
    const AttackTarget* targets=nullptr;
    std::size_t targetCount=0;
    bool active=true;
    // The producer sets this when it cannot describe the full danger field.
    bool incomplete=false;
    // Optional team positioning, independent of pickup/attack objectives.
    // Zero axis weights disable it; slack avoids chasing an exact pixel.
    Vec2 formationGoal{}, formationWeight{}, formationSlack{};
    bool measurePressure=false;
    int requiredFirstFocus=-1; // -1 free, 0 released, 1 pressed; first tick only.
    std::uint32_t committedAction=0;
    int committedFrames=0; // Unchangeable direction/focus prefix, including this tick.
    bool commitFirstAction=false; // First focus gesture also lasts actionFrames.
};
struct Settings {
    int horizon=12, beamWidth=8, actionFrames=3;
    int maxHazards=2048;
    std::uint32_t maxCollisionTests=40000, maxTreeVisits=120000;
    float safetyMargin=.45f, proximityRange=24;
    float dangerWeight=10, goalWeight=.004f, edgeWeight=1.f;
    float movementWeight=.002f, switchWeight=.025f, edgeDistance=80;
    float mergeDistance=3;
    float pickupWeight=3.f, pickupApproachWeight=.0525f, attackWeight=.2f;
    float densityWeight=.1f;
    bool allowFast=true, fire=true;
};
enum ResultFlag : std::uint32_t {
    BudgetExhausted=1, IncompleteWorld=2, NoSafePath=4,
    InvalidWorld=8, Inactive=16
};
struct Statistics {
    std::uint32_t collisionTests=0, treeVisits=0, expanded=0, indexedHazards=0;
    int completedFrames=0;
};
struct Pressure {
    float score=0, safeDirectionRatio=1, bestClearance=0, density=0;
    bool valid=false; // False for incomplete snapshots or exhausted budgets.
};
struct Result {
    std::uint32_t buttons=0, flags=0;
    Vec2 destination{}; // First tick, not the end of the whole route.
    float cost=0, clearance=0;
    int safeFrames=0;
    Statistics stats;
    Pressure pressure;
};

// One instance per controlled player. No world/RNG mutations or clock reads.
// Scratch vectors are reserved once; normal decide() calls do not allocate.
class Planner {
public:
    static constexpr int MaxHorizon=24, MaxBeam=16, MaxHazards=4096, MaxPickups=32, MaxTargets=8;
    explicit Planner(Settings settings={});
    void configure(Settings settings);
    const Settings& settings() const { return config_; }
    void reset() { previous_=0; }
    Result decide(const World& world);
private:
    struct Aabb { float l=0,t=0,r=0,b=0; };
    struct Entry { Hazard hazard; Aabb bounds; };
    struct Tree { Aabb bounds; int left=-1,right=-1,begin=0,end=0; };
    struct Route {
        Vec2 p{},firstPosition{};
        std::uint32_t first=0,last=0;
        std::uint32_t ordinal=0;
        std::uint32_t collected=0;
        float cost=0,margin=10000,densitySum=0;
    };
    Settings config_;
    std::uint32_t previous_=0;
    std::vector<Entry> entries_;
    std::vector<int> order_;
    std::vector<Tree> tree_;
    std::vector<Route> beam_,next_,candidates_;
    int build(int begin,int end);
    bool clearance(Vec2 from,Vec2 to,int frame,float playerRadius,bool collectDensity,
                   Statistics& stats,float& margin,float& density) const;
};
} // namespace eagler::ai
