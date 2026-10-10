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
#include "overlays/actors/ovl_En_Dog/z_en_dog.h"
#include "objects/object_dog/object_dog.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kCount = 5;
constexpr float kEnemyRange = 650.0f; // from the landing point
constexpr float kFollowDistance = 130.0f;
constexpr float kBiteDistance = 75.0f; // starts the snap when the target is this close

// Snap pattern: run in, short wind-up, lunge forward and bite, fall back, repeat
enum State { STATE_CHASE, STATE_WINDUP, STATE_LUNGE, STATE_RECOVER };
enum Anim { ANIM_NONE, ANIM_RUN, ANIM_BARK, ANIM_IDLE };

constexpr int kWindupFrames = 4;
constexpr int kLungeFrames = 6;
constexpr int kRecoverFrames = 14;

struct Puppet {
    Actor* actor;
    float homeX;
    float homeZ;
    int state;
    int timer;
    int anim;
    ColliderCylinder collider;
};

std::vector<std::unique_ptr<Puppet>> sPuppets;

ColliderCylinderInit sBiteInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000329, 0x00, 0x02 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE,
      OCELEM_NONE },
    { 26, 36, 0, { 0, 0, 0 } },
};

Puppet* FindPuppet(Actor* actor) {
    for (auto& p : sPuppets) {
        if (p->actor == actor) {
            return p.get();
        }
    }
    return nullptr;
}

void SetAnim(Puppet& puppet, int anim) {
    if (puppet.anim == anim) {
        return;
    }
    puppet.anim = anim;
    EnDog* dog = reinterpret_cast<EnDog*>(puppet.actor);
    AnimationHeader* header = nullptr;
    switch (anim) {
        case ANIM_RUN:
            header = (AnimationHeader*)&gDogRunAnim;
            break;
        case ANIM_BARK:
            header = (AnimationHeader*)&gDogBarkAnim;
            break;
        default:
            header = (AnimationHeader*)&gDogWalkAnim;
            break;
    }
    Animation_Change(&dog->skelAnime, header, anim == ANIM_IDLE ? 0.0f : 1.0f, 0.0f,
                     static_cast<f32>(Animation_GetLastFrame(header)), ANIMMODE_LOOP, -4.0f);
}

void UpdatePuppet(Puppet& puppet) {
    PlayState* play = gPlayState;
    Actor* actor = puppet.actor;
    EnDog* dog = reinterpret_cast<EnDog*>(actor);
    Player* player = GET_PLAYER(play);

    Actor* target = Cursed::FindNearestEnemy(actor, puppet.homeX, puppet.homeZ, kEnemyRange);
    bool biting = false;
    actor->speedXZ = 0.0f;

    if (puppet.timer > 0) {
        puppet.timer--;
    }

    if (target == nullptr) {
        // No enemy around: stay close to Link
        puppet.state = STATE_CHASE;
        if (actor->xzDistToPlayer > kFollowDistance) {
            const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos);
            Math_SmoothStepToS(&actor->world.rot.y, yaw, 3, 4000, 0);
            actor->speedXZ = 5.0f;
            SetAnim(puppet, ANIM_RUN);
        } else {
            SetAnim(puppet, ANIM_IDLE);
        }
    } else {
        const s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &target->world.pos);
        const float dist = Actor_WorldDistXZToActor(actor, target);
        switch (puppet.state) {
            case STATE_CHASE:
                Math_SmoothStepToS(&actor->world.rot.y, yaw, 2, 6000, 0);
                actor->speedXZ = 6.0f;
                SetAnim(puppet, ANIM_RUN);
                if (dist < kBiteDistance) {
                    puppet.state = STATE_WINDUP;
                    puppet.timer = kWindupFrames;
                    SetAnim(puppet, ANIM_BARK);
                    Audio_PlayActorSound2(actor, NA_SE_EV_SMALL_DOG_BARK);
                }
                break;
            case STATE_WINDUP:
                // Crouch and aim
                Math_SmoothStepToS(&actor->world.rot.y, yaw, 1, 9000, 0);
                if (puppet.timer == 0) {
                    puppet.state = STATE_LUNGE;
                    puppet.timer = kLungeFrames;
                }
                break;
            case STATE_LUNGE:
                actor->speedXZ = 12.0f;
                biting = true;
                if (puppet.timer == 0) {
                    puppet.state = STATE_RECOVER;
                    puppet.timer = kRecoverFrames;
                    SetAnim(puppet, ANIM_RUN);
                }
                break;
            case STATE_RECOVER:
                // Back off a little before the next attack
                actor->speedXZ = -2.5f;
                if (puppet.timer == 0) {
                    puppet.state = STATE_CHASE;
                }
                break;
        }
    }

    actor->shape.rot.y = actor->world.rot.y;
    SkelAnime_Update(&dog->skelAnime);
    Actor_MoveXZGravity(actor);
    Actor_UpdateBgCheckInfo(play, actor, 20.0f, 25.0f, 30.0f, 7);

    if (actor->floorHeight <= BGCHECK_Y_MIN) {
        Actor_Kill(actor);
        return;
    }

    if (biting) {
        puppet.collider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
        Collider_UpdateCylinder(actor, &puppet.collider);
        puppet.collider.dim.pos.x += static_cast<s16>(Math_SinS(actor->shape.rot.y) * 18.0f);
        puppet.collider.dim.pos.z += static_cast<s16>(Math_CosS(actor->shape.rot.y) * 18.0f);
        CollisionCheck_SetAT(play, &play->colChkCtx, &puppet.collider.base);
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_DOG || sPuppets.empty()) {
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

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sPuppets.clear(); });

    Cursed::RegisterEffect({
        "dice_wild_dogs",
        "Wild Dogs",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_DOG, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("Dogs could not be loaded");
                    return;
                }
                int spawned = 0;
                for (int i = 0; i < kCount; i++) {
                    const float angle = (6.2831853f / kCount) * i;
                    const s16 rot = static_cast<s16>(i * (0x10000 / kCount));
                    // 0x8000: free-roaming dog (no market logic), 0x00F0: no path, low bits: coat colour
                    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_DOG, 0x80F0 | (i % 2), x + 60.0f * sinf(angle),
                                                       y + 10.0f, z + 60.0f * cosf(angle), rot);
                    if (spawn == nullptr) {
                        continue;
                    }
                    Actor* actor = static_cast<Actor*>(spawn);
                    Cursed::MarkAlly(actor);
                    auto puppet = std::make_unique<Puppet>();
                    puppet->actor = actor;
                    puppet->homeX = x;
                    puppet->homeZ = z;
                    puppet->state = STATE_CHASE;
                    puppet->timer = i * 3; // keep the pack from striking in perfect unison
                    puppet->anim = ANIM_NONE;
                    Collider_InitCylinder(gPlayState, &puppet->collider);
                    Collider_SetCylinder(gPlayState, &puppet->collider, actor, &sBiteInit);
                    sPuppets.push_back(std::move(puppet));
                    SetAnim(*sPuppets.back(), ANIM_IDLE);
                    spawned++;
                }
                if (spawned == 0) {
                    Cursed::Notify("Dogs cannot appear here");
                }
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceWildDogsInit(Register);
