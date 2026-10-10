#include <algorithm>
#include <vector>

#include "CursedScript_Ally.h"
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
std::vector<Actor*> sAllies;

// No Z-target reticle and no Navi highlight for friendly effect NPCs
void StripMarkers(Actor* actor) {
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
}

void Register() {
    // Vanilla code may set the flags again, so clear them every frame
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool*) {
        if (!sAllies.empty()) {
            Actor* actor = static_cast<Actor*>(refActor);
            if (std::find(sAllies.begin(), sAllies.end(), actor) != sAllies.end()) {
                StripMarkers(actor);
            }
        }
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        auto it = std::find(sAllies.begin(), sAllies.end(), static_cast<Actor*>(refActor));
        if (it != sAllies.end()) {
            sAllies.erase(it);
        }
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sAllies.clear(); });
}
} // namespace

namespace Cursed {

void MarkAlly(Actor* actor) {
    sAllies.push_back(actor);
    StripMarkers(actor);
    SetLifetimeSeconds(actor, kAllyLifetimeSeconds);
}

bool IsAlly(Actor* actor) {
    return std::find(sAllies.begin(), sAllies.end(), actor) != sAllies.end();
}

Actor* FindNearestEnemy(Actor* from, float homeX, float homeZ, float range) {
    PlayState* play = gPlayState;
    Actor* best = nullptr;
    float bestSq = 1.0e12f;
    for (Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; enemy != nullptr; enemy = enemy->next) {
        if (enemy->id == ACTOR_EN_ATTACK_NIW || enemy->id == ACTOR_EN_SKJNEEDLE || enemy->update == nullptr ||
            enemy->colChkInfo.health == 0 || IsAlly(enemy)) {
            continue;
        }
        const float hx = enemy->world.pos.x - homeX;
        const float hz = enemy->world.pos.z - homeZ;
        if (hx * hx + hz * hz > range * range) {
            continue;
        }
        const float dx = enemy->world.pos.x - from->world.pos.x;
        const float dy = enemy->world.pos.y - from->world.pos.y;
        const float dz = enemy->world.pos.z - from->world.pos.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < bestSq) {
            bestSq = distSq;
            best = enemy;
        }
    }
    return best;
}

} // namespace Cursed

static RegisterShipInitFunc allyInit(Register);
