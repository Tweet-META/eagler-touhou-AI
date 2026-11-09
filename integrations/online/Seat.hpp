#pragma once

#include "Planner.hpp"
#include "InputDelay.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <emscripten/emscripten.h>

namespace eagler::ai::online {

struct Buttons {
    std::uint16_t up, down, left, right;
    static constexpr std::uint16_t shoot = 1, bomb = 2, focus = 4;
    std::uint16_t mask() const { return shoot | bomb | focus | up | down | left | right; }
    std::uint16_t encode(std::uint32_t semantic) const {
        return ((semantic & Fire) ? shoot : 0) | ((semantic & Focus) ? focus : 0) |
               ((semantic & Left) ? left : 0) | ((semantic & Right) ? right : 0) |
               ((semantic & Up) ? up : 0) | ((semantic & Down) ? down : 0);
    }
};

struct Snapshot {
    World world;
    std::array<Hazard, Planner::MaxHazards> hazards;
    std::array<Pickup, Planner::MaxPickups> pickups;
    std::array<AttackTarget, Planner::MaxTargets> targets;
    std::optional<Vec2> teammate;
    bool boss = false, canBomb = false;

    void reset() {
        world = {};
        world.hazards = hazards.data();
        world.pickups = pickups.data();
        world.targets = targets.data();
        teammate.reset();
        boss = canBomb = false;
    }
    void add(Hazard hazard) {
        if (world.hazardCount == hazards.size()) world.incomplete = true;
        else hazards[world.hazardCount++] = hazard;
    }
    void box(Vec2 center, Vec2 half, Vec2 velocity = {}, float density = 1) {
        Hazard hazard;
        hazard.shape = Shape::Box;
        hazard.position = center;
        hazard.halfSize = half;
        hazard.velocity = velocity;
        hazard.densityScale = density;
        add(hazard);
    }
    void circle(Vec2 center, float radius, Vec2 velocity = {}, float density = 1) {
        Hazard hazard;
        hazard.position = center;
        hazard.radius = radius;
        hazard.velocity = velocity;
        hazard.densityScale = density;
        add(hazard);
    }
    void laser(Vec2 origin, float angle, float length, float width, Vec2 velocity = {}) {
        if (length <= 0 || width < 0) return;
        Hazard hazard;
        hazard.shape = Shape::LaserBox;
        hazard.position = {origin.x + std::cos(angle) * length * .5f,
                           origin.y + std::sin(angle) * length * .5f};
        hazard.halfSize = {length * .5f, width * .5f};
        hazard.angle = angle;
        hazard.velocity = velocity;
        hazard.densityScale = 0;
        add(hazard);
    }
    void target(Vec2 center, float halfWidth) {
        if (world.targetCount < targets.size()) targets[world.targetCount++] = {center, {}, halfWidth, 1};
    }
    void pickup(Vec2 center, Vec2 velocity, float value) {
        if (center.y >= (world.bounds.top + world.bounds.bottom) * .5f && world.pickupCount < pickups.size())
            pickups[world.pickupCount++] = {center, velocity, 8, value};
    }
};

struct Seat {
    Settings settings;
    Planner planner;
    InputDelay delay;
    Snapshot snapshot;
    std::array<Vec2, 19> follow{};
    std::size_t nextFollow = 0, followCount = 0;
    std::uint32_t lastFrame = 0, lastStage = 0, heldAction = 0;
    int remaining = 0, team = 0, local = -1;
    bool enabled = false, active = false, autoBomb = true, followAttack = false;
    double elapsed = 0;

    void reset() {
        delay.reset();
        planner.reset();
        remaining = 0;
        followCount = nextFollow = 0;
        active = false;
    }
    template<class Frame, class Read>
    void capture(Frame& input, Buttons buttons, unsigned seat, unsigned frame, unsigned stage,
                 bool playing, Read read) {
        local = int(seat);
        if (!enabled || !playing) { if (active) reset(); return; }
        if (active && (stage != lastStage || frame <= lastFrame)) reset();
        lastStage = stage;
        lastFrame = frame;
        const double start = emscripten_get_now();
        snapshot.reset();
        if (!read(snapshot)) { if (active) reset(); return; }
        active = true;
        auto& world = snapshot.world;
        auto config = settings;
        config.horizon = std::max(config.horizon, config.actionFrames);
        if (snapshot.teammate) {
            const Vec2 teammate = *snapshot.teammate;
            if (team == 0) {
                follow[nextFollow] = teammate;
                nextFollow = (nextFollow + 1) % follow.size();
                followCount = std::min(followCount + 1, follow.size());
                world.formationGoal = follow[followCount == follow.size() ? nextFollow : 0];
                world.formationWeight = {.035f, .035f};
                world.formationSlack = {4, 4};
                config.edgeWeight = config.movementWeight = 0;
                if (!followAttack) config.attackWeight = 0;
            } else if (!snapshot.boss) {
                const float middle = (world.bounds.left + world.bounds.right) * .5f;
                const float width = world.bounds.right - world.bounds.left;
                world.formationGoal = {middle + (teammate.x < middle ? .25f : -.25f) * width, 0};
                world.formationWeight = {.025f, 0};
                world.formationSlack = {24, 0};
                config.edgeWeight = 0;
            }
        } else {
            followCount = nextFollow = 0;
        }
        planner.configure(config);
        world.commitFirstAction = true;
        world.committedAction = heldAction;
        world.committedFrames = remaining;
        const auto result = planner.decide(world);
        elapsed = emscripten_get_now() - start;
        if (result.flags & (IncompleteWorld | InvalidWorld | Inactive)) { reset(); return; }
        if (!remaining) {
            heldAction = result.buttons & (Focus | Left | Right | Up | Down);
            remaining = config.actionFrames;
        }
        --remaining;
        auto next = buttons.encode(heldAction | (result.buttons & Fire));
        if (autoBomb && snapshot.canBomb && (result.flags & NoSafePath) && !(result.flags & BudgetExhausted))
            next |= Buttons::bomb;
        if (!autoBomb || !snapshot.canBomb) delay.cancelButtons(Buttons::bomb);
        const auto delivered = delay.tick(next);
        input = Frame((input.buttons & ~buttons.mask()) | delivered);
    }
};

inline Seat aiSeat;

} // namespace eagler::ai::online

extern "C" {
using eagler::ai::online::aiSeat;
EMSCRIPTEN_KEEPALIVE void eagler_ai_enable(int value) { if (aiSeat.enabled != bool(value)) aiSeat.reset(); aiSeat.enabled = value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_configure(int horizon,int beam,int budget) {
    aiSeat.settings.horizon=std::clamp(horizon,1,24);aiSeat.settings.beamWidth=beam;
    aiSeat.settings.maxCollisionTests=unsigned(std::max(1,budget));aiSeat.settings.maxHazards=4096;
}
EMSCRIPTEN_KEEPALIVE void eagler_ai_action_duration(int value) {
    value=std::clamp(value,1,12);
    if(aiSeat.settings.actionFrames!=value){aiSeat.settings.actionFrames=value;aiSeat.reset();}
}
EMSCRIPTEN_KEEPALIVE void eagler_ai_input_delay(int value) { aiSeat.delay.configure(value); }
EMSCRIPTEN_KEEPALIVE void eagler_ai_team_mode(int value) { if(aiSeat.team!=value)aiSeat.reset();aiSeat.team=value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_follow_attack(int value) { aiSeat.followAttack=value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_auto_bomb(int value) { aiSeat.autoBomb=value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_switch_weight(float value) { aiSeat.settings.switchWeight=value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_objectives(float pickup,float attack) {
    aiSeat.settings.pickupWeight=pickup;aiSeat.settings.pickupApproachWeight=pickup*.0175f;aiSeat.settings.attackWeight=attack;
}
EMSCRIPTEN_KEEPALIVE void eagler_ai_edges(float weight,float distance) { aiSeat.settings.edgeWeight=weight;aiSeat.settings.edgeDistance=distance; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_danger(float value) { aiSeat.settings.dangerWeight=value; }
EMSCRIPTEN_KEEPALIVE void eagler_ai_density(float value) { aiSeat.settings.densityWeight=value; }
EMSCRIPTEN_KEEPALIVE double eagler_ai_metric(int index) {
    switch (index) {
        case 0:return aiSeat.enabled;
        case 1:return aiSeat.active;
        case 2:return aiSeat.elapsed;
        case 4:return double(aiSeat.snapshot.world.hazardCount);
        case 29:return aiSeat.local;
        default:return 0;
    }
}
}
