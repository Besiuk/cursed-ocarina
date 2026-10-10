#include <memory>
#include <vector>

#include "CursedScript_Ally.h"
#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Ge2/z_en_ge2.h"
#include "objects/object_gla/object_gla.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kCount = 3;
constexpr float kEnemyRange = 600.0f; // from the landing point
constexpr float kFollowDistance = 140.0f;

struct Puppet {
    Actor* actor;
    float homeX;
    float homeZ;
    int mode; // 0 walk, 1 charge, 2 idle
    int swingTimer;
    ColliderCylinder collider;
};

std::vector<std::unique_ptr<Puppet>> sPuppets;

ColliderCylinderInit sAttackInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000329, 0x00, 0x04 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE,
      OCELEM_NONE },
    { 34, 60, 0, { 0, 0, 0 } },
};

Puppet* FindPuppet(Actor* actor) {
    for (auto& p : sPuppets) {
        if (p->actor == actor) {
            return p.get();
        }
    }
    return nullptr;
}

void SetMode(Puppet& puppet, int mode) {
    if (puppet.mode == mode) {
        return;
    }
    puppet.mode = mode;
    EnGe2* ge2 = reinterpret_cast<EnGe2*>(puppet.actor);
    if (mode == 1) {
        Animation_PlayLoop(&ge2->skelAnime, (AnimationHeader*)&gGerudoPurpleChargingAnim);
    } else {
        Animation_PlayLoop(&ge2->skelAnime, (AnimationHeader*)&gGerudoPurpleWalkingAnim);
    }
    ge2->skelAnime.playSpeed = (mode == 2) ? 0.0f : 1.0f;
}

void UpdatePuppet(Puppet& puppet) {
    PlayState* play = gPlayState;
    Actor* actor = puppet.actor;
    EnGe2* ge2 = reinterpret_cast<EnGe2*>(actor);
    Player* player = GET_PLAYER(play);

    Actor* target = Cursed::FindNearestEnemy(actor, puppet.homeX, puppet.homeZ, kEnemyRange);
    bool attacking = false;

    if (target != nullptr) {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &target->world.pos);
        Math_SmoothStepToS(&actor->world.rot.y, yaw, 2, 5000, 0);
        const float dist = Actor_WorldDistXZToActor(actor, target);
        if (dist > 45.0f) {
            actor->speedXZ = 5.5f;
            SetMode(puppet, 1);
        } else {
            actor->speedXZ = 0.0f;
            attacking = true;
            SetMode(puppet, 1);
        }
    } else if (actor->xzDistToPlayer > kFollowDistance) {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos);
        Math_SmoothStepToS(&actor->world.rot.y, yaw, 3, 3000, 0);
        actor->speedXZ = 3.0f;
        SetMode(puppet, 0);
    } else {
        actor->speedXZ = 0.0f;
        SetMode(puppet, 2);
    }

    actor->shape.rot.y = actor->world.rot.y;
    SkelAnime_Update(&ge2->skelAnime);
    Actor_MoveXZGravity(actor);
    Actor_UpdateBgCheckInfo(play, actor, 25.0f, 25.0f, 40.0f, 7);

    if (actor->floorHeight <= BGCHECK_Y_MIN) {
        Actor_Kill(actor);
        return;
    }

    // Strike in short bursts so the enemy's invincibility frames can run out in between
    if (puppet.swingTimer > 0) {
        puppet.swingTimer--;
    }
    if (attacking) {
        if (puppet.swingTimer == 0) {
            puppet.swingTimer = 20;
        }
        if (puppet.swingTimer > 12) {
            // Reach a bit in front of the Gerudo
            puppet.collider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
            Collider_UpdateCylinder(actor, &puppet.collider);
            puppet.collider.dim.pos.x += static_cast<s16>(Math_SinS(actor->shape.rot.y) * 30.0f);
            puppet.collider.dim.pos.z += static_cast<s16>(Math_CosS(actor->shape.rot.y) * 30.0f);
            CollisionCheck_SetAT(play, &play->colChkCtx, &puppet.collider.base);
        }
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_GE2 || sPuppets.empty()) {
            return;
        }
        Puppet* puppet = FindPuppet(actor);
        if (puppet == nullptr) {
            return;
        }
        *should = false;
        UpdatePuppet(*puppet);
        // The kill above removes the update function; the engine must not call it afterwards
        if (actor->update == nullptr) {
            *should = false;
        }
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

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sPuppets.clear(); });

    Cursed::RegisterEffect({
        "dice_gerudo_allies",
        "Gerudo Allies",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_GLA, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("Gerudo could not be loaded");
                    return;
                }
                int spawned = 0;
                for (int i = 0; i < kCount; i++) {
                    const float angle = (6.2831853f / kCount) * i;
                    const s16 rot = static_cast<s16>(i * (0x10000 / kCount));
                    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_GE2, 1 /* stationary */, x + 70.0f * sinf(angle), y + 10.0f,
                                                       z + 70.0f * cosf(angle), rot);
                    if (spawn == nullptr) {
                        continue;
                    }
                    Actor* actor = static_cast<Actor*>(spawn);
                    Cursed::MarkAlly(actor);
                    auto puppet = std::make_unique<Puppet>();
                    puppet->actor = actor;
                    puppet->homeX = x;
                    puppet->homeZ = z;
                    puppet->mode = -1;
                    puppet->swingTimer = 0;
                    Collider_InitCylinder(gPlayState, &puppet->collider);
                    Collider_SetCylinder(gPlayState, &puppet->collider, actor, &sAttackInit);
                    sPuppets.push_back(std::move(puppet));
                    SetMode(*sPuppets.back(), 2);
                    spawned++;
                }
                if (spawned == 0) {
                    Cursed::Notify("Gerudo cannot appear here");
                }
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceGerudoInit(Register);
