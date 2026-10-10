#include <cstring>
#include <vector>

#include "CursedScript_Ally.h"
#include "CursedScript_DarkLink.h"
#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

constexpr float kSideOffset = 50.0f;
constexpr float kRingRadius = 35.0f;
constexpr s16 kRingSpeed = 0x0150; // per frame, about one turn in 10 s

// A and C buttons are not replayed blindly: the shadow never uses items. A is only passed along while Z is
// held (roll / jump slash). A on its own would put the sword away.
constexpr u16 kButtonMask = BTN_A | BTN_B | BTN_R | BTN_Z;

struct Clone {
    Actor* actor;
    Cursed::DarkLinkMode mode;
    int slot;
    int count;
};

std::vector<Clone> sClones;
s16 sRingAngle = 0;
u32 sRingFrame = 0;

Clone* FindClone(Actor* actor) {
    for (Clone& clone : sClones) {
        if (clone.actor == actor) {
            return &clone;
        }
    }
    return nullptr;
}

// Noclip: the shadow is placed relative to Link, whatever the real collision says
void Pin(const Clone& clone, Player* dark, Player* link) {
    float x;
    float z;
    s16 yaw;
    if (clone.mode == Cursed::DarkLinkMode::Ring) {
        // Angle comes from a world-space counter, so Link's own rotation has no influence
        yaw = sRingAngle + static_cast<s16>(clone.slot * (0x10000 / clone.count));
        x = link->actor.world.pos.x + Math_SinS(yaw) * kRingRadius;
        z = link->actor.world.pos.z + Math_CosS(yaw) * kRingRadius;
    } else {
        yaw = link->actor.shape.rot.y;
        x = link->actor.world.pos.x + Math_CosS(yaw) * kSideOffset;
        z = link->actor.world.pos.z - Math_SinS(yaw) * kSideOffset;
    }
    dark->actor.world.pos.x = x;
    dark->actor.world.pos.z = z;
    dark->actor.world.pos.y = link->actor.world.pos.y;
    dark->actor.prevPos = dark->actor.world.pos;
    dark->actor.home.pos = dark->actor.world.pos;
    dark->actor.world.rot.y = yaw;
    dark->actor.shape.rot.y = yaw;
    dark->actor.focus.pos = dark->actor.world.pos;
    dark->actor.focus.pos.y += 20.0f;
}

// Sword and shield stay out, nothing can hurt the shadow, and only enemies feel its blade
void Prepare(Player* dark) {
    dark->heldItemAction = dark->heldItemId = PLAYER_IA_SWORD_MASTER;
    dark->currentShield = PLAYER_SHIELD_HYLIAN;
    Player_SetModelGroup(dark, PLAYER_MODELGROUP_SWORD_AND_SHIELD);

    dark->cylinder.base.acFlags &= ~AC_ON;
    dark->cylinder.base.ocFlags1 &= ~OC1_ON;
    dark->cylinder.base.atFlags &= ~AT_ON;
    ColliderQuad* quads[2] = { &dark->meleeWeaponQuads[0], &dark->meleeWeaponQuads[1] };
    for (ColliderQuad* quad : quads) {
        quad->base.atFlags = (quad->base.atFlags & ~AT_TYPE_ALL) | AT_TYPE_PLAYER | AT_ON;
        quad->base.acFlags &= ~AC_ON;
        quad->info.bumperFlags = 0;
    }
    dark->shieldQuad.base.atFlags &= ~AT_ON;
    dark->shieldQuad.base.acFlags &= ~AC_ON;
    dark->actor.flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    dark->actor.colChkInfo.health = 99;
}

void UpdateClone(Clone& clone) {
    PlayState* play = gPlayState;
    Player* link = GET_PLAYER(play);
    Player* dark = reinterpret_cast<Player*>(clone.actor);

    // One orbit step per game frame, no matter how many shadows there are
    if (sRingFrame != play->gameplayFrames) {
        sRingFrame = play->gameplayFrames;
        sRingAngle += kRingSpeed;
    }

    Input input = play->state.input[0];
    if (play->csCtx.state != 0) {
        std::memset(&input, 0, sizeof(input));
    } else {
        const bool zHeld = (input.cur.button & BTN_Z) != 0;
        const u16 mask = zHeld ? kButtonMask : static_cast<u16>(kButtonMask & ~BTN_A);
        input.cur.button &= mask;
        input.press.button &= mask;
        input.rel.button &= mask;
        input.prev.button &= mask;
    }

    Pin(clone, dark, link);
    Prepare(dark);
    play->playerUpdate(dark, play, &input);
    Prepare(dark);
    Pin(clone, dark, link);
    dark->actor.velocity.y = 0.0f;
}

void KillAll() {
    for (Clone& clone : sClones) {
        Cursed::Despawn(clone.actor);
    }
    sClones.clear();
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        if (sClones.empty()) {
            return;
        }
        Clone* clone = FindClone(static_cast<Actor*>(refActor));
        if (clone == nullptr) {
            return;
        }
        *should = false;
        UpdateClone(*clone);
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        for (auto it = sClones.begin(); it != sClones.end(); ++it) {
            if (it->actor == refActor) {
                sClones.erase(it);
                return;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sClones.clear(); });
}

} // namespace

namespace Cursed {

void SpawnDarkLinks(DarkLinkMode mode, int count) {
    RequestObject(OBJECT_TORCH2, [mode, count](bool ok) {
        if (!ok) {
            Notify("Dark Link could not be loaded");
            return;
        }
        KillAll();
        Player* link = GET_PLAYER(gPlayState);
        int spawned = 0;
        for (int i = 0; i < count; i++) {
            Clone clone{ nullptr, mode, i, count };
            void* spawn = SpawnActorAt(ACTOR_EN_TORCH2, 0, link->actor.world.pos.x, link->actor.world.pos.y,
                                       link->actor.world.pos.z, link->actor.shape.rot.y, true);
            if (spawn == nullptr) {
                continue;
            }
            clone.actor = static_cast<Actor*>(spawn);
            SetLifetimeSeconds(spawn, kAllyLifetimeSeconds);
            sClones.push_back(clone);
            spawned++;
        }
        if (spawned == 0) {
            Notify("Dark Link cannot appear here");
        }
    });
}

} // namespace Cursed

static RegisterShipInitFunc darkLinkEngineInit(Register);
