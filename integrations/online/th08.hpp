#pragma once
#include "Seat.hpp"
#include "../game/GameplayScene.hpp"

namespace eagler::ai::online {
inline void captureTh08(Netplay::FrameInput& input, const th08::GameplayScene& game,
                        unsigned seat, unsigned frame, bool playing) {
    aiSeat.capture(input, {16, 32, 64, 128}, seat, frame, game.globals.stage, playing,
        [&](Snapshot& out) {
            if (!game.roster.eligible(seat)) return false;
            auto& pilot = *game.roster.seats[seat].player;
            const auto& state = pilot.status();
            if (state.life.state != 0 && state.life.state != 3) return false;
            const auto& motion = state.motion.movement;
            const auto& min = state.input.minimum;
            const auto& extent = state.input.extent;
            auto& world = out.world;
            world.bounds = {min.x, min.y, min.x + extent.x, min.y + extent.y};
            world.goal = {min.x + extent.x * .5f, min.y + extent.y - 64};
            world.player.position = {motion.position.x, motion.position.y};
            world.player.boxHalfSize = Vec2{motion.half_boxes[0].x, motion.half_boxes[0].y};
            const float multiplier = std::min(motion.multiplier.x, motion.multiplier.y);
            world.player.fastSpeed = pilot.profile(false).normal_speed * multiplier;
            world.player.slowSpeed = pilot.profile(true).focus_speed * multiplier;
            world.player.invulnerableFrames = std::max(0, std::max(state.life.state == 3 ? state.life.timer.current : 0,
                                                               state.life.team_bomb_protection.current) - 1);
            out.canBomb = state.life.state == 0 && !state.bomb.active && !state.bomb.cooldown &&
                          !state.context.challenge_mode && state.context.bombs > 0;
            for (unsigned peer = 0; peer < game.roster.count; ++peer) {
                if (peer == seat || !game.roster.eligible(peer)) continue;
                const auto& other = game.roster.seats[peer].player->status();
                if (other.life.state != 0 && other.life.state != 3) continue;
                const auto& p = other.motion.movement.position;
                out.teammate = Vec2{p.x, p.y};
                break;
            }
            for (const auto& bullet : game.projectile_pool.bullets) {
                if (bullet.state != 1 || bullet.reisen_illusion) continue;
                out.box({bullet.position.x, bullet.position.y},
                        {bullet.sprites.hitbox.x * .5f, bullet.sprites.hitbox.y * .5f},
                        {bullet.velocity.x, bullet.velocity.y});
            }
            for (const auto& laser : game.projectile_pool.lasers) {
                if (!laser.in_use || laser.state > 2) continue;
                const int age = laser.timer.current;
                if ((laser.state == 0 && age < laser.hitbox_start && age < laser.start) ||
                    (laser.state == 2 && age >= laser.hitbox_stop)) continue;
                const float length = laser.end_offset - laser.start_offset;
                float collisionLength = length * (laser.start_offset > 0 ? .7f : 1.f);
                if (!(laser.flags & 1) && laser.state == 0 && age < laser.start) {
                    const float warning = laser.start > 0 && age > laser.start - std::min(30, laser.start)
                        ? float(age) * laser.width / laser.start : 1.2f;
                    collisionLength = warning * .5f;
                }
                if (!(laser.flags & 1) && laser.state == 2 && laser.stop > 0)
                    collisionLength = laser.width * (1.f - float(age) / laser.stop) * .5f;
                const float origin = laser.start_offset + (length - collisionLength) * .5f;
                out.laser({laser.position.x + std::cos(laser.angle) * origin,
                           laser.position.y + std::sin(laser.angle) * origin},
                          laser.angle, collisionLength, laser.width * .5f);
            }
            for (const auto* boss : game.globals.boss_slots) {
                if (!boss || boss->life <= 0) continue;
                out.boss = true;
                out.target({boss->position.x, boss->position.y}, boss->hitbox.x * .5f);
            }
            for (unsigned index = 0; const auto* enemy = game.enemies.population.at(index); ++index) {
                if (!(enemy->flags & 1) || enemy->parent || (enemy->flags & 0x830)) continue;
                const Vec2 center{enemy->resolved_position.x, enemy->resolved_position.y};
                if (!(enemy->flags & 2) && enemy->life > 0)
                    out.target(center, enemy->hitbox.x * .5f);
                if (!(enemy->flags & 4)) continue;
                out.box(center, {enemy->hitbox.x / 3.f, enemy->hitbox.y / 3.f}, {}, 0);
                if (!enemy->trail.flags) continue;
                const int length = std::min<int>(96, enemy->trail.collision_length);
                for (int i = 1; i < length; i += 6) {
                    const auto& position = enemy->trail.points[i].position;
                    const float scale = enemy->trail.flags & 2 ? 1.f - float(i) / length : 1.f;
                    out.box({position.x, position.y},
                            {enemy->hitbox.x * scale / 3.f, enemy->hitbox.y * scale / 3.f}, {}, 0);
                }
            }
            for (const auto& item : game.items.status().items) {
                if (!item.active || item.state != 0 || item.type == 6) continue;
                out.pickup({item.position.x, item.position.y}, {item.velocity.x, item.velocity.y}, item.type == 1 ? .5f : 1.f);
            }
            return true;
        });
}
} // namespace eagler::ai::online
