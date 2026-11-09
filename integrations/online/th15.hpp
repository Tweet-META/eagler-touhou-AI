#pragma once

#include "Seat.hpp"
#include "../game/EnemyCollision.hpp"
#include "../game/GameBattle.hpp"
#include "../game/MultiplayerPilot.hpp"

namespace eagler::ai::online {

inline void captureTh15(Netplay::FrameInput& input, th15::GameBattle& game, const th15::AnmManager& animations,
                        unsigned seat, unsigned frame, unsigned stage, bool playing) {
    const auto* local = game.mp_enabled && seat < game.mp_options.seat_count ? game.pilots[seat].get() : nullptr;
    const bool flipped = local && local->player && local->player->motion.flip_vertical_step;
    const Buttons buttons = flipped ? Buttons{32, 16, 64, 128} : Buttons{16, 32, 64, 128};
    aiSeat.capture(input, buttons, seat, frame, stage, playing && game.mp_enabled,
        [&](Snapshot& out) {
            if (seat >= game.mp_options.seat_count || !game.pilots[seat] ||
                !game.bullet_scene || !game.laser_scene || !game.items) return false;
            auto& pilot = *game.pilots[seat];
            if (!pilot.alive() || pilot.player->life.state != 1) return false;
            const auto& player = *pilot.player;
            const auto& motion = player.motion;
            const float rate = game.bullet_scene->manager.context.rate;
            const bool enlarged = motion.behavior_flags & 16;
            const float scale = enlarged ? motion.enlargement : 1.f;
            auto& world = out.world;
            world.bounds = {-184, 32, 184, 432};
            world.goal = {0, 368};
            world.player.position = {motion.position.x, motion.position.y};
            world.player.boxHalfSize = Vec2{player.frame.bounds.hit_half_size.x * scale,
                                            player.frame.bounds.hit_half_size.y * scale};
            world.player.laserBoxHalfSize = Vec2{player.frame.bounds.hit_half_size.x,
                                                 player.frame.bounds.hit_half_size.y};
            world.player.orthogonalCircleRadius = player.resource.header.hitbox *
                (enlarged ? scale * 3.6f : 1.f);
            world.player.fastSpeed = motion.normal_speed / 128.f * rate;
            world.player.slowSpeed = motion.focus_speed / 128.f * rate;
            world.player.invulnerableFrames = std::max(0, player.life.invulnerability.current - 1);
            out.canBomb = pilot.session.bombs > 0 && pilot.allow_bomb() && !player.life.invulnerability.current;
            for (unsigned other = 0; other < game.mp_options.seat_count; ++other) {
                const auto* peer = game.pilots[other].get();
                if (other != seat && peer && peer->alive()) {
                    const auto& position = peer->player->motion.position;
                    out.teammate = Vec2{position.x, position.y};
                    break;
                }
            }
            const auto& bullets = game.bullet_scene->manager;
            for (auto slot = bullets.first_active(); slot != bullets.none; slot = bullets.next_active(slot)) {
                const auto& bullet = *bullets.state(slot);
                if (bullet.phase != 1 || !(bullet.flags & 2)) continue;
                const auto& motion = bullet.motion;
                const Vec2 position{motion.position.x, motion.position.y};
                const Vec2 velocity{motion.velocity.x * rate, motion.velocity.y * rate};
                const float scale = bullet.flags & 64 ? bullet.scale : 1.f;
                if (bullet.flags & 16) out.circle(position, bullet.hitbox.x * scale, velocity);
                else out.box(position, {bullet.hitbox.x * scale * .5f, bullet.hitbox.y * scale * .5f}, velocity);
            }
            auto& lasers = *game.laser_scene;
            lasers.manager.each([&](const th15::LaserObject& laser) {
                if (laser.state == 1 || (laser.flags & 14)) return;
                if (const auto* curve = lasers.curve_motion(laser.id)) {
                    float traveled = 0;
                    for (std::size_t i = 0; i + 1 < curve->points.size(); ++i) {
                        const auto& point = curve->points[i];
                        traveled += point.speed;
                        if (traveled < 16) continue;
                        out.laser({point.position.x + std::cos(point.angle) * point.speed * .5f,
                                   point.position.y + std::sin(point.angle) * point.speed * .5f},
                                  point.angle, point.speed, curve->width * .5f);
                    }
                } else if (const auto* moving = lasers.moving_motion(laser.id)) {
                    if (moving->length <= 16 || moving->width <= 3) return;
                    const bool full = lasers.ai_moving_full_length(laser);
                    const float offset = full ? 0.f : moving->length * .1f;
                    const float width = moving->width < 32 ? moving->width * .5f :
                        moving->width - (moving->width + 16) * .5f;
                    out.laser({moving->position.x + std::cos(moving->angle) * offset,
                               moving->position.y + std::sin(moving->angle) * offset}, moving->angle,
                              moving->length * (full ? 1.f : .8f), width,
                              {moving->velocity.x * rate, moving->velocity.y * rate});
                } else if (const auto* stationary = lasers.stationary_motion(laser.id)) {
                    if ((laser.state != 2 && laser.state != 4) || stationary->length <= 16) return;
                    const float width = stationary->width < 32 ? stationary->width * .5f :
                        stationary->width - (stationary->width + 16) / 3.f;
                    out.laser({stationary->position.x, stationary->position.y}, stationary->angle,
                              stationary->length * .9f, width);
                }
            });
            if (const auto* boss = game.enemy_world.boss) {
                out.boss = true;
                out.target({boss->motion.position.x, boss->motion.position.y}, 16);
            }
            for (const auto* enemy : game.enemy_world.enemies) {
                if (!enemy) continue;
                if (!(enemy->flags & 0x31) && enemy->life > 0 && enemy->collision_timer.current <= 0)
                    out.target({enemy->motion.position.x, enemy->motion.position.y}, enemy->hitbox.x * .5f);
                if ((enemy->flags & 0x4000022) || enemy->invulnerability_timer.current > 0) continue;
                const auto shape = th15::enemy_contact_shape(*enemy, animations.registry.find(enemy->animation_handles[0]));
                const Vec2 origin{shape.position.x, shape.position.y};
                if (shape.rectangle) out.laser(origin, shape.angle, shape.length, shape.width);
                else out.circle(origin, shape.radius, {}, 0);
            }
            for (unsigned id = 1; id <= th15::ItemManager::ordinary_count; ++id) {
                const auto& item = game.items->find(id)->state;
                if (item.state != 2) continue;
                const int owner = game.mp_item_targets[id - 1];
                if (owner >= 0 && unsigned(owner) != seat) continue;
                const auto kind = static_cast<th15::ItemKind>(item.kind);
                const float value = kind == th15::ItemKind::Point ? .5f :
                    kind == th15::ItemKind::LargePower ? 2.f : 1.f;
                out.pickup({item.position.x, item.position.y}, {item.velocity.x, item.velocity.y}, value);
            }
            return true;
        });
}

} // namespace eagler::ai::online
