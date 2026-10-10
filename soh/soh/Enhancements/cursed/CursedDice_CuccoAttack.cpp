#include <memory>
#include <vector>

#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Niw/z_en_niw.h"
extern PlayState* gPlayState;
}

namespace {

// Everything within this distance of the landing point is a target (enemies and Link alike).
// The attackers disappear after kLifetimeSeconds (the spawn helper also removes them on room change).
constexpr float kTargetRange = 450.0f;
constexpr float kLifetimeSeconds = 10.0f;

// A cucco whose vanilla AI is replaced by our own while the effect runs
struct Puppet {
    Actor* actor;
    float homeX;
    float homeZ;
    int hopTimer;
    int hitCooldown;
    int sfxTimer;
    ColliderCylinder collider;
};

std::vector<std::unique_ptr<Puppet>> sPuppets;

// Player-type attack collider, so enemies treat the cucco like an arrow hit
ColliderCylinderInit sAttackInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000329, 0x00, 0x02 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NONE, BUMP_NONE,
      OCELEM_NONE },
    { 22, 40, 0, { 0, 0, 0 } },
};

Puppet* FindPuppet(Actor* actor) {
    for (auto& puppet : sPuppets) {
        if (puppet->actor == actor) {
            return puppet.get();
        }
    }
    return nullptr;
}

void AddPuppet(Actor* actor) {
    auto puppet = std::make_unique<Puppet>();
    puppet->actor = actor;
    puppet->homeX = actor->world.pos.x;
    puppet->homeZ = actor->world.pos.z;
    Collider_InitCylinder(gPlayState, &puppet->collider);
    Collider_SetCylinder(gPlayState, &puppet->collider, actor, &sAttackInit);
    sPuppets.push_back(std::move(puppet));
}

// Nearest enemy that stands close to where the cuccos landed
Actor* FindNearestEnemy(PlayState* play, const Puppet& puppet) {
    Actor* best = nullptr;
    float bestSq = 1.0e12f;
    for (Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; enemy != nullptr; enemy = enemy->next) {
        if (enemy->id == ACTOR_EN_ATTACK_NIW || enemy->update == nullptr || enemy->colChkInfo.health == 0) {
            continue;
        }
        const float hx = enemy->world.pos.x - puppet.homeX;
        const float hz = enemy->world.pos.z - puppet.homeZ;
        if (hx * hx + hz * hz > kTargetRange * kTargetRange) {
            continue;
        }
        const float dx = enemy->world.pos.x - puppet.actor->world.pos.x;
        const float dy = enemy->world.pos.y - puppet.actor->world.pos.y;
        const float dz = enemy->world.pos.z - puppet.actor->world.pos.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < bestSq) {
            bestSq = distSq;
            best = enemy;
        }
    }
    return best;
}

void UpdatePuppet(Puppet& puppet) {
    PlayState* play = gPlayState;
    Actor* actor = puppet.actor;
    Player* player = GET_PLAYER(play);
    EnNiw* niw = reinterpret_cast<EnNiw*>(actor);

    // Target: whoever is nearest, enemy or Link, as long as they are close to the landing point
    Actor* target = FindNearestEnemy(play, puppet);
    const float lx = player->actor.world.pos.x - puppet.homeX;
    const float lz = player->actor.world.pos.z - puppet.homeZ;
    if (lx * lx + lz * lz < kTargetRange * kTargetRange) {
        if (target == nullptr || actor->xzDistToPlayer < Actor_WorldDistXZToActor(actor, target)) {
            target = &player->actor;
        }
    }

    if (target != nullptr) {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &target->world.pos);
        Math_SmoothStepToS(&actor->world.rot.y, yaw, 3, 4000, 0);
        actor->speedXZ = 5.0f;
    } else {
        actor->speedXZ = 0.0f;
    }

    // Hop along like a panicking cucco
    if (puppet.hopTimer > 0) {
        puppet.hopTimer--;
    }
    if (puppet.hopTimer == 0 && (actor->bgCheckFlags & 1)) {
        actor->velocity.y = 3.5f;
        puppet.hopTimer = 4;
    }

    // Wing flapping, same pose values the vanilla cucco uses when it flees
    const bool flapUp = (play->gameplayFrames & 1) != 0;
    niw->unk_2C8 = niw->unk_2D4 = flapUp ? 25000.0f : 8000.0f;
    niw->unk_2C4 = niw->unk_2D0 = -10000.0f;
    niw->unk_2CC = niw->unk_2D8 = 6000.0f;

    actor->shape.rot = actor->world.rot;
    Actor_MoveXZGravity(actor);
    Actor_UpdateBgCheckInfo(play, actor, 20.0f, 20.0f, 60.0f, 31);

    if (actor->floorHeight <= BGCHECK_Y_MIN) {
        Actor_Kill(actor);
        return;
    }

    // Damage to Link, same knockback and damage as the vanilla attack cuccos
    if (puppet.hitCooldown > 0) {
        puppet.hitCooldown--;
    }
    if (puppet.hitCooldown == 0 && player->invincibilityTimer == 0 && actor->xyzDistToPlayerSq < 20.0f * 20.0f) {
        Actor_SetPlayerKnockbackLarge(play, actor, 2.0f, actor->world.rot.y, 0.0f, 0x10);
        puppet.hitCooldown = 40;
    }

    // Damage to enemies through the attack collider
    Collider_UpdateCylinder(actor, &puppet.collider);
    CollisionCheck_SetAT(play, &play->colChkCtx, &puppet.collider.base);

    if (puppet.sfxTimer > 0) {
        puppet.sfxTimer--;
    } else {
        Audio_PlayActorSound2(actor, NA_SE_EV_CHICKEN_CRY_A);
        puppet.sfxTimer = 30;
    }

}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>(
        [](void* refActor, bool* should) {
            Actor* actor = static_cast<Actor*>(refActor);
            if (actor->id != ACTOR_EN_NIW || sPuppets.empty()) {
                return;
            }
            Puppet* puppet = FindPuppet(actor);
            if (puppet == nullptr) {
                return;
            }
            *should = false;
            UpdatePuppet(*puppet);
        });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        for (auto it = sPuppets.begin(); it != sPuppets.end(); ++it) {
            if ((*it)->actor == actor) {
                if (gPlayState != nullptr) {
                    Collider_DestroyCylinder(gPlayState, &(*it)->collider);
                }
                sPuppets.erase(it);
                return;
            }
        }
    });

    Cursed::RegisterEffect({
        "dice_cucco_attack",
        "Cucco Attack",
        0.0f,
        [] {
            int spawned = 0;
            for (int i = 0; i < 6; i++) {
                void* actor = Cursed::SpawnActorAtOrigin(ACTOR_EN_NIW, 0, 60.0f);
                if (actor != nullptr) {
                    Cursed::SetLifetimeSeconds(actor, kLifetimeSeconds);
                    AddPuppet(static_cast<Actor*>(actor));
                    spawned++;
                }
            }
            if (spawned == 0) {
                Cursed::Notify("Cannot spawn right now");
            }
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc cuccoAttackInit(Register);
