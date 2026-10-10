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
#include "overlays/actors/ovl_En_Skj/z_en_skj.h"
#include "overlays/actors/ovl_En_Skjneedle/z_en_skjneedle.h"
#include "objects/object_skj/object_skj.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kCount = 3;
constexpr float kEnemyRange = 700.0f;  // from the landing point
constexpr float kShootDistance = 260.0f;
constexpr float kFollowDistance = 150.0f;
constexpr int kShootInterval = 50; // frames

enum Mode { MODE_WALK, MODE_STANCE, MODE_SHOOT };

struct Puppet {
    Actor* actor;
    float homeX;
    float homeZ;
    int mode;
    int shootTimer;
    bool needleFired;
};

std::vector<std::unique_ptr<Puppet>> sPuppets;

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
    EnSkj* skj = reinterpret_cast<EnSkj*>(puppet.actor);
    switch (mode) {
        case MODE_WALK:
            Animation_PlayLoop(&skj->skelAnime, (AnimationHeader*)&gSkullKidWalkToPlayerAnim);
            break;
        case MODE_STANCE:
            Animation_PlayLoop(&skj->skelAnime, (AnimationHeader*)&gSkullKidFightingStanceAnim);
            break;
        case MODE_SHOOT:
            Animation_PlayOnce(&skj->skelAnime, (AnimationHeader*)&gSkullKidShootNeedleAnim);
            puppet.needleFired = false;
            break;
    }
}

// The needle is an enemy projectile in vanilla; turn it into a player-side one so it only hurts enemies
void FireNeedle(Actor* actor) {
    const float fx = Math_SinS(actor->shape.rot.y);
    const float fz = Math_CosS(actor->shape.rot.y);
    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_SKJNEEDLE, 0, actor->world.pos.x + fx * 40.0f,
                                       actor->world.pos.y + 27.0f, actor->world.pos.z + fz * 40.0f, actor->shape.rot.y,
                                       true);
    if (spawn == nullptr) {
        return;
    }
    EnSkjneedle* needle = static_cast<EnSkjneedle*>(spawn);
    needle->killTimer = 100;
    needle->actor.speedXZ = 22.0f;
    needle->collider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
    needle->collider.base.acFlags = AC_NONE;
    needle->collider.info.toucher.dmgFlags = 0x00000329;
    needle->collider.info.toucher.damage = 4;
    Cursed::SetLifetimeSeconds(spawn, 4.0f);
}

void UpdatePuppet(Puppet& puppet) {
    PlayState* play = gPlayState;
    Actor* actor = puppet.actor;
    EnSkj* skj = reinterpret_cast<EnSkj*>(actor);
    Player* player = GET_PLAYER(play);

    Actor* target = Cursed::FindNearestEnemy(actor, puppet.homeX, puppet.homeZ, kEnemyRange);
    actor->speedXZ = 0.0f;

    if (puppet.shootTimer > 0) {
        puppet.shootTimer--;
    }

    if (puppet.mode == MODE_SHOOT) {
        // Fire halfway through the animation, then go back to the stance
        if (!puppet.needleFired && skj->skelAnime.curFrame >= skj->skelAnime.endFrame * 0.5f) {
            FireNeedle(actor);
            puppet.needleFired = true;
        }
    } else if (target != nullptr) {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &target->world.pos);
        Math_SmoothStepToS(&actor->world.rot.y, yaw, 2, 5000, 0);
        const float dist = Actor_WorldDistXZToActor(actor, target);
        if (dist > kShootDistance) {
            actor->speedXZ = 3.5f;
            SetMode(puppet, MODE_WALK);
        } else {
            SetMode(puppet, MODE_STANCE);
            if (puppet.shootTimer == 0) {
                puppet.shootTimer = kShootInterval;
                SetMode(puppet, MODE_SHOOT);
            }
        }
    } else if (actor->xzDistToPlayer > kFollowDistance) {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos);
        Math_SmoothStepToS(&actor->world.rot.y, yaw, 3, 3000, 0);
        actor->speedXZ = 3.0f;
        SetMode(puppet, MODE_WALK);
    } else {
        SetMode(puppet, MODE_STANCE);
    }

    actor->shape.rot.y = actor->world.rot.y;
    if (SkelAnime_Update(&skj->skelAnime) && puppet.mode == MODE_SHOOT) {
        SetMode(puppet, MODE_STANCE);
    }
    Actor_MoveXZGravity(actor);
    Actor_UpdateBgCheckInfo(play, actor, 25.0f, 25.0f, 40.0f, 7);

    if (actor->floorHeight <= BGCHECK_Y_MIN) {
        Actor_Kill(actor);
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_SKJ || sPuppets.empty()) {
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
                sPuppets.erase(it);
                return;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sPuppets.clear(); });

    Cursed::RegisterEffect({
        "dice_skullkid_allies",
        "Skull Kid Allies",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_SKJ, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("Skull Kids could not be loaded");
                    return;
                }
                // The fighting Skull Kid type removes itself unless Link owns the saw; borrow it for the spawn
                u8& tradeSlot = gSaveContext.inventory.items[SLOT_TRADE_ADULT];
                const u8 oldTrade = tradeSlot;
                if (tradeSlot < ITEM_SAW) {
                    tradeSlot = ITEM_SAW;
                }
                int spawned = 0;
                for (int i = 0; i < kCount; i++) {
                    const float angle = (6.2831853f / kCount) * i + 0.5f;
                    const s16 rot = static_cast<s16>(i * (0x10000 / kCount));
                    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_SKJ, 3 << 10, x + 70.0f * sinf(angle), y + 10.0f,
                                                       z + 70.0f * cosf(angle), rot, true);
                    if (spawn == nullptr) {
                        continue;
                    }
                    Actor* actor = static_cast<Actor*>(spawn);
                    // Not hostile any more: no battle music, no red target arrow
                    actor->flags &= ~ACTOR_FLAG_HOSTILE;
                    Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
                    Cursed::MarkAlly(actor);
                    auto puppet = std::make_unique<Puppet>();
                    puppet->actor = actor;
                    puppet->homeX = x;
                    puppet->homeZ = z;
                    puppet->mode = -1;
                    puppet->shootTimer = 20 + i * 10;
                    puppet->needleFired = false;
                    sPuppets.push_back(std::move(puppet));
                    SetMode(*sPuppets.back(), MODE_STANCE);
                    spawned++;
                }
                tradeSlot = oldTrade;
                if (spawned == 0) {
                    Cursed::Notify("Skull Kids cannot appear here");
                }
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceSkullKidInit(Register);
