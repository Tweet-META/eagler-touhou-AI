#pragma once
#include "Seat.hpp"
#include "../game/GameSession.hpp"
#include "../game/MultiplayerBattle.hpp"

namespace eagler::ai::online {
inline void captureTh11(Netplay::FrameInput& input, const th11::GameSession& session,
                        unsigned seat, unsigned frame, bool playing) {
    const auto* game = session.battle.get();
    playing = playing && game && game->mp_enabled && !game->mp_paused && !game->mp_finished &&
        !game->transitioning && (!game->dialogue || !game->dialogue->active) &&
        session.state.phase == th11::GameSessionPhase::stage;
    aiSeat.capture(input, {16, 32, 64, 128}, seat, frame, session.state.stage, playing,
        [&](Snapshot& out) {
            if (seat >= game->mp_options.seat_count || !game->pilots[seat]) return false;
            const auto& pilot = *game->pilots[seat];
            if (!pilot.player || pilot.ghost || pilot.player->state.life_state != 1) return false;
            const auto& player = *pilot.player;
            const auto& motion = player.motion.state;
            const auto collision = player.collision();
            const float rate = session.animations.rate;
            auto& world = out.world;
            world.bounds = {-184, 32, 184, 432};
            world.goal = {0, 368};
            world.player.position = {motion.position.x, motion.position.y};
            world.player.boxHalfSize = Vec2{player.state.hit_half.x, player.state.hit_half.y};
            world.player.orthogonalCircleRadius = collision.radius;
            world.player.fastSpeed = motion.normal_speed / 128.f * rate;
            world.player.slowSpeed = motion.focus_speed / 128.f * rate;
            world.player.invulnerableFrames = std::max(0, collision.invincibility_frames - 1);
            out.canBomb = !game->mp_options.challenge && pilot.bomb && !pilot.bomb->state.active &&
                          pilot.economy.power_step > 0 && pilot.economy.power >= pilot.economy.power_step &&
                          !collision.invincibility_frames;
            for (unsigned other = 0; other < game->mp_options.seat_count; ++other) {
                const auto* peer = game->pilots[other].get();
                if (other != seat && peer && peer->alive()) {
                    const auto& position = peer->player->motion.state.position;
                    out.teammate = Vec2{position.x, position.y};
                    break;
                }
            }
            for (unsigned i = 0; i < game->bullets.capacity; ++i) {
                const auto& bullet = game->bullets.at(i);
                if (bullet.state != 1 || !(bullet.flags & 2) || (bullet.flags & 8)) continue;
                const Vec2 pos{bullet.position.x, bullet.position.y}, velocity{bullet.velocity.x * rate, bullet.velocity.y * rate};
                if (bullet.flags & 0x10) out.circle(pos, bullet.hitbox.x, velocity);
                else out.box(pos, {bullet.hitbox.x * .5f, bullet.hitbox.y * .5f}, velocity);
            }
            for (const auto* laser = game->lasers.first(); laser; laser = laser->next) {
                const bool moving = laser->type == 0;
                if (laser->marked || laser->length <= 16 ||
                    (moving ? laser->width <= 3 : laser->state != 2 && laser->state != 4)) continue;
                const float offset = laser->length * .1f;
                const float width = laser->width < 32 ? laser->width * .5f :
                    laser->width - (laser->width + 16) / (moving ? 2.f : 3.f);
                out.laser({laser->position.x + std::cos(laser->angle) * offset,
                           laser->position.y + std::sin(laser->angle) * offset}, laser->angle,
                          laser->length * .8f, width, {laser->velocity.x * rate, laser->velocity.y * rate});
            }
            for (const auto* boss : game->enemy_commands.bosses) {
                if (!boss) continue;
                out.boss = true;
                out.target({boss->current.position.x, boss->current.position.y}, 16);
            }
            for (const auto* link = game->enemies.first; link; link = link->next) {
                if (!link->value) continue;
                const auto& enemy = link->value->state;
                const Vec2 center{enemy.current.position.x, enemy.current.position.y};
                if (!(enemy.flags & 0x31) && enemy.health > 0 && enemy.damage_immunity.current <= 0)
                    out.target(center, enemy.hitbox.x * .5f);
                if (!(enemy.flags & 0x400022) && enemy.collision_immunity.current <= 0 &&
                    !th11::u32(enemy.collision_callback))
                    out.circle(center, enemy.collision_box.x * .5f, {}, 0);
            }
            for (unsigned i = 0; i < game->items.capacity; ++i) {
                const auto& item = game->items.at(i);
                if (item.state == 1) out.pickup({item.position.x, item.position.y},
                    {item.velocity.x, item.velocity.y}, item.type == 2 ? .5f : 1.f);
            }
            return true;
        });
}
} // namespace eagler::ai::online
