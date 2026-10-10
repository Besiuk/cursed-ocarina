// Rolling Gorons: three Gorons appear at the impact point and roll away in three directions
// (straight away from Link, and 120 degrees to either side). They flatten everything on the way and
// hurt Link too. Only a wall or an explosion stops them: they curl up and sit. After 30 seconds they vanish in a puff of smoke.
// They cannot be Z-targeted and cannot be talked to.
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
#include "overlays/actors/ovl_En_Go2/z_en_go2.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kCount = 3;
constexpr float kLifetimeSeconds = 30.0f; // after this they vanish in a puff of smoke, rolling or sitting
constexpr float kRollSpeed = 8.0f;     // units per frame
constexpr float kBlastRadius = 100.0f; // an explosion this close stops a goron
constexpr int kWallGraceFrames = 4;    // ignore walls right after spawning
constexpr s16 kSideAngle = 0x5555;     // 120 degrees
constexpr int kEnemyDamageTag = 0x00000329;
constexpr int kLinkHitCooldown = 30;   // frames between two hits on Link

enum State { STATE_ROLLING, STATE_CURLING, STATE_SITTING };

struct Puppet {
    Actor* actor;
    int state;
    int age;
    int hitCooldown;
    s16 yaw;
    ColliderCylinder collider;
};

std::vector<std::unique_ptr<Puppet>> sPuppets;

// Damages enemies and objects. Link is hurt separately below.
ColliderCylinderInit sRollInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { kEnemyDamageTag, 0x00, 0x04 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NORMAL,
      BUMP_NONE, OCELEM_NONE },
    { 34, 50, 0, { 0, 0, 0 } },
};

Puppet* FindPuppet(Actor* actor) {
    for (auto& p : sPuppets) {
        if (p->actor == actor) {
            return p.get();
        }
    }
    return nullptr;
}

bool ExplosionNearby(Actor* actor) {
    for (Actor* other = gPlayState->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].head; other != nullptr;
         other = other->next) {
        if (other->id == ACTOR_EN_BOM && other->params == BOMB_EXPLOSION &&
            Actor_WorldDistXYZToActor(actor, other) < kBlastRadius) {
            return true;
        }
    }
    return false;
}

void StartCurling(Puppet& puppet) {
    PlayState* play = gPlayState;
    EnGo2* goron = reinterpret_cast<EnGo2*>(puppet.actor);
    puppet.state = STATE_CURLING;
    puppet.actor->speedXZ = 0.0f;
    puppet.actor->shape.yOffset = 0.0f;
    puppet.actor->shape.rot.y = puppet.yaw;
    // The vanilla "curl up and sit" animation, ends in the sitting ball
    EnGo2_RollingAnimation(goron, play);
    Audio_PlayActorSound2(puppet.actor, NA_SE_EN_DODO_M_GND);
}

void UpdatePuppet(Puppet& puppet) {
    PlayState* play = gPlayState;
    Actor* actor = puppet.actor;
    EnGo2* goron = reinterpret_cast<EnGo2*>(actor);
    Player* player = GET_PLAYER(play);

    puppet.age++;
    // Belt and braces next to the central timer: the goron always leaves after its lifetime
    if (puppet.age > static_cast<int>(kLifetimeSeconds * 20.0f)) {
        Cursed::Despawn(actor);
        return;
    }
    if (puppet.hitCooldown > 0) {
        puppet.hitCooldown--;
    }

    if (puppet.state != STATE_ROLLING) {
        // Play the curl-up animation to its end, then stay put
        actor->speedXZ = 0.0f;
        SkelAnime_Update(&goron->skelAnime);
        if (puppet.state == STATE_CURLING && Animation_OnFrame(&goron->skelAnime, goron->skelAnime.endFrame)) {
            goron->skelAnime.playSpeed = 0.0f;
            puppet.state = STATE_SITTING;
        }
        Actor_MoveXZGravity(actor);
        Actor_UpdateBgCheckInfo(play, actor, 30.0f, 40.0f, 40.0f, 7);
        return;
    }

    actor->world.rot.y = puppet.yaw;
    actor->shape.rot.y = puppet.yaw;
    actor->speedXZ = kRollSpeed;
    Actor_MoveXZGravity(actor);
    Actor_UpdateBgCheckInfo(play, actor, 30.0f, 40.0f, 40.0f, 7);
    Audio_PlayActorSound2(actor, NA_SE_EV_BIGBALL_ROLL - SFX_FLAG);

    if (actor->floorHeight <= BGCHECK_Y_MIN) {
        Cursed::Despawn(actor);
        return;
    }

    if ((puppet.age > kWallGraceFrames && (actor->bgCheckFlags & BGCHECKFLAG_WALL)) || ExplosionNearby(actor)) {
        StartCurling(puppet);
        return;
    }

    // Enemies and objects
    Collider_UpdateCylinder(actor, &puppet.collider);
    puppet.collider.base.atFlags = AT_ON | AT_TYPE_PLAYER | AT_TYPE_OTHER;
    CollisionCheck_SetAT(play, &play->colChkCtx, &puppet.collider.base);

    // Link
    if (puppet.hitCooldown == 0 && player->invincibilityTimer == 0 && actor->xzDistToPlayer < 50.0f &&
        ABS(actor->yDistToPlayer) < 70.0f) {
        Actor_SetPlayerKnockbackLarge(play, actor, 6.0f, puppet.yaw, 4.0f, 0x10);
        puppet.hitCooldown = kLinkHitCooldown;
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_GO2 || sPuppets.empty()) {
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
        "dice_rolling_gorons",
        "Rolling Gorons",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_OF1D_MAP, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("Gorons could not be loaded");
                    return;
                }
                Player* player = GET_PLAYER(gPlayState);
                // Straight away from Link; if the nut landed on top of him, in his viewing direction
                Vec3f origin = { x, y, z };
                s16 baseYaw = player->actor.shape.rot.y;
                const float dx = x - player->actor.world.pos.x;
                const float dz = z - player->actor.world.pos.z;
                if (dx * dx + dz * dz > 40.0f * 40.0f) {
                    baseYaw = Math_Vec3f_Yaw(&player->actor.world.pos, &origin);
                }
                const s16 yaws[kCount] = { baseYaw, static_cast<s16>(baseYaw + kSideAngle),
                                           static_cast<s16>(baseYaw - kSideAngle) };

                int spawned = 0;
                for (int i = 0; i < kCount; i++) {
                    // Type "rolling small", no path (0x1F)
                    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_GO2, GORON_DMT_ROLLING_SMALL | (0x1F << 5),
                                                       x + Math_SinS(yaws[i]) * 30.0f, y + 10.0f,
                                                       z + Math_CosS(yaws[i]) * 30.0f, yaws[i]);
                    if (spawn == nullptr || static_cast<Actor*>(spawn)->update == nullptr) {
                        continue;
                    }
                    Actor* actor = static_cast<Actor*>(spawn);
                    Cursed::SetLifetimeSeconds(spawn, kLifetimeSeconds);
                    // No Z-targeting, no Navi marker, not hostile; being a puppet it never offers a conversation
                    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
                    auto puppet = std::make_unique<Puppet>();
                    puppet->actor = actor;
                    puppet->state = STATE_ROLLING;
                    puppet->age = 0;
                    puppet->hitCooldown = 0;
                    puppet->yaw = yaws[i];
                    Collider_InitCylinder(gPlayState, &puppet->collider);
                    Collider_SetCylinder(gPlayState, &puppet->collider, actor, &sRollInit);
                    sPuppets.push_back(std::move(puppet));
                    spawned++;
                }
                if (spawned == 0) {
                    Cursed::Notify("Gorons cannot appear here");
                }
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceRollingGoronsInit(Register);
