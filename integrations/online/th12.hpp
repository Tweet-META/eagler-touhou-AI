#pragma once

#include "Seat.hpp"
#include "../game/GameState.hpp"
#include "../game/LaserWorld.hpp"

namespace eagler::ai::online {

inline void captureTh12(Netplay::FrameInput& input, unsigned seat, unsigned captureFrame,
                        bool playing) {
    using namespace th12;
    using namespace th12::multiplayer;
    aiSeat.capture(input, {16, 32, 64, 128}, seat, captureFrame, unsigned(stageNumber), playing,
        [&](Snapshot& out) {
            if (seat >= options.count) return false;
            const auto& pilot = pilots[seat];
            if (pilot.ghost || pilot.playerState != 1) return false;
            const auto& shot = loadouts[pilot.character * 2 + pilot.shot];
            const float half = eb::playerHitboxes[pilot.character] * .5f;
            const float rate = gameSpeed;
            const float speedScale = pilot.playerBomb.speedMultiplier * rate;
            auto& world = out.world;
            world.bounds = {-184, 32, 184, 432};
            world.goal = {0, 368};
            world.player.position = {pilot.px / 128.f, pilot.py / 128.f};
            world.player.boxHalfSize = Vec2{half, half};
            world.player.orthogonalCircleRadius = eb::playerHitboxes[pilot.character];
            world.player.orthogonalBodyRadius = shot.hit;
            world.player.fastSpeed = shot.speed * speedScale;
            world.player.slowSpeed = shot.focus * speedScale;
            world.player.invulnerableFrames = std::max(0, pilot.invuln - 1);
            out.canBomb = !options.challenge && pilot.resources.bombs > 0 &&
                          !pilot.playerBomb.active && !pilot.invuln && !(pilot.playerFlags & 2u);
            for (unsigned other = 0; other < options.count; ++other) {
                const auto& peer = pilots[other];
                if (other != seat && !peer.ghost && peer.playerState == 1) {
                    out.teammate = Vec2{peer.px / 128.f, peer.py / 128.f};
                    break;
                }
            }
            for (const auto& projectile : bullets) {
                if (!projectile.active || projectile.friendly) continue;
                const auto& bullet = projectile.enemy;
                if (bullet.phase != eb::Phase::Alive &&
                    !(bullet.phase == eb::Phase::Spawning && bullet.collisionAge >= 8)) continue;
                const auto& appearance = eb::appearances[bullet.type];
                const Vec2 center{bullet.x, bullet.y}, velocity{bullet.vx * rate, bullet.vy * rate};
                if (appearance.effect == 2) out.circle(center, appearance.hitbox, velocity);
                else out.box(center, {appearance.hitbox * .5f, appearance.hitbox * .5f}, velocity);
            }
            for (const auto& object : enemies) {
                const auto& enemy = *object;
                if (!enemy.active) continue;
                out.boss |= enemy.boss;
                if (!enemy.hidden && !enemy.controller && !enemy.invincible && enemy.life > 0)
                    out.target({enemy.x, enemy.y}, enemy.hitWidth * .5f);
                if (!(enemy.flags & (2 | 32 | 0x2000000)) && !enemy.bodyImmunityTicks && enemy.bodyWidth > 0) {
                    // Enemy body contact uses the SHT radius rather than the bullet radius.
                    Hazard body;
                    body.shape = Shape::BodyCircle;
                    body.position = {enemy.x, enemy.y};
                    body.radius = enemy.bodyWidth * .5f;
                    body.densityScale = 0;
                    out.add(body);
                }
            }
            for (const auto* object : laserManager.ordered()) {
                const auto& laser = object->state;
                if (laser.phase == laser::Phase::Removed || laser.phase == laser::Phase::Retiring) continue;
                if (laser.kind == laser::Kind::Curve) {
                    float total = 0;
                    for (std::size_t i = 0; i + 1 < laser.nodes.size(); ++i) {
                        const auto& node = laser.nodes[i];
                        total += node.speed;
                        if (total < 16) continue;
                        out.laser({node.position.x + std::cos(node.angle) * node.speed * .5f,
                                   node.position.y + std::sin(node.angle) * node.speed * .5f},
                                  node.angle, node.speed, laser.width * .5f);
                    }
                    continue;
                }
                const bool moving = laser.kind == laser::Kind::Moving;
                if (laser.length <= 16 || (moving && laser.width <= 3) ||
                    (!moving && laser.phase != laser::Phase::Active && laser.phase != laser::Phase::Growing)) continue;
                const float width = laser.width < 32 ? laser.width * .5f :
                    laser.width - (laser.width + 16) / (moving ? 2.f : 3.f);
                const float offset = moving ? laser.length * .1f : 0;
                out.laser({laser.position.x + std::cos(laser.angle) * offset,
                           laser.position.y + std::sin(laser.angle) * offset}, laser.angle,
                          laser.length * (moving ? .8f : 1.f), width,
                          moving ? Vec2{laser.velocity.x, laser.velocity.y} : Vec2{});
            }
            for (const auto& item : itemManager.entities) {
                if (item.state != item_system::State::Falling && item.state != item_system::State::Token) continue;
                if (item.recipient >= 0 && unsigned(item.recipient) != seat) continue;
                const float value = item.type == int(item_system::Kind::LargePower) ? 2.f :
                    item.type == int(item_system::Kind::Point) ? .5f : 1.f;
                out.pickup({item.position.x, item.position.y}, {item.velocity.x, item.velocity.y}, value);
            }
            return true;
        });
}

} // namespace eagler::ai::online
