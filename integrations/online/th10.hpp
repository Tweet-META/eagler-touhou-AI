#pragma once
#include "Seat.hpp"
#include "World.hpp"

namespace eagler::ai::online {
inline void captureTh10(Netplay::FrameInput& input, const th10::browser::World& game, unsigned frame, unsigned selectedSeat = 255) {
    const auto& actors = game.actors;
    const unsigned seat = selectedSeat == 255 ? game.local_player : selectedSeat;
    const bool playing = !game.loading && actors.session && actors.gui && !actors.gui->dialogue &&
        !(actors.session->session_flags & 0x74u) && !(input.buttons & 8) && game.engine.speed > 0;
    aiSeat.capture(input, {16, 32, 64, 128}, seat, frame,
        actors.session ? actors.session->stage_identifier : 0, playing, [&](Snapshot& out) {
            const auto& pilot = game.pilots[seat];
            if (!pilot.player || pilot.player->state != 1) return false;
            const auto& player = *pilot.player;
            const float rate = game.engine.speed;
            auto& world = out.world;
            world.bounds = {-184, 32, 184, 432};
            world.goal = {0, 368};
            world.player.position = {player.position.x, player.position.y};
            world.player.boxHalfSize = Vec2{player.hitbox_half_size.x, player.hitbox_half_size.y};
            world.player.fastSpeed = player.fast_speed * .01f * rate;
            world.player.slowSpeed = (player.focus_timer.current < 4 ? player.fast_speed : player.slow_speed) * .01f * rate;
            world.player.invulnerableFrames = std::max(0, player.invulnerability.current - 1);
            out.canBomb = pilot.bomb && !pilot.bomb->active && pilot.game.power >= 20 && !player.invulnerability.current;
            for (unsigned other = 0; other < game.player_count; ++other) {
                const auto* peer = game.pilots[other].player;
                if (other != seat && peer && peer->state == 1) {
                    out.teammate = Vec2{peer->position.x, peer->position.y};
                    break;
                }
            }
            if (actors.bullets) for (const auto& bullet : actors.bullets->pool) {
                if (bullet.state != 1 || !(bullet.flags & 2) || (bullet.flags & 8)) continue;
                out.box({bullet.motion.position.x, bullet.motion.position.y},
                        {bullet.cancel_size * .5f, bullet.hitbox_height * .5f},
                        {bullet.motion.velocity.x * rate, bullet.motion.velocity.y * rate});
            }
            if (actors.lasers) for (auto* laser = actors.lasers->sentinel.next; laser; laser = laser->next) {
                const bool straight = laser->original_virtual_table == 1;
                if (laser->length <= 16 || (straight && laser->width <= 3) ||
                    (!straight && laser->state != 2 && laser->state != 4)) continue;
                const float offset = laser->length * .1f;
                const float width = laser->width < 32 ? laser->width * .5f :
                    laser->width - (laser->width + 16) / (straight ? 2.f : 3.f);
                out.laser({laser->position.x + std::cos(laser->angle) * offset,
                           laser->position.y + std::sin(laser->angle) * offset}, laser->angle,
                          laser->length * .8f, width, {laser->velocity.x * rate, laser->velocity.y * rate});
            }
            if (actors.enemies) {
                for (auto* boss : actors.enemies->bosses) out.boss |= boss != nullptr;
                for (auto* node = actors.enemies->head; node; node = node->next) {
                    if (!node->value) continue;
                    const auto& enemy = node->value->state;
                    const Vec2 pos{enemy.current.position.x, enemy.current.position.y};
                    if (enemy.health > 0 && enemy.damage_immunity.current < 1)
                        out.target(pos, enemy.hitbox.x * .5f);
                    if (!(enemy.flags & 0x12) && enemy.collision_immunity.current < 1)
                        out.box(pos, {enemy.collision_box.x * .5f, enemy.collision_box.y * .5f}, {}, 0);
                }
            }
            if (actors.items) for (const auto& item : actors.items->regular) {
                if (item.state == 1)
                    out.pickup({item.position.x, item.position.y}, {item.velocity.x, item.velocity.y}, item.kind == 2 ? .5f : 1.f);
            }
            return true;
        });
}
} // namespace eagler::ai::online
