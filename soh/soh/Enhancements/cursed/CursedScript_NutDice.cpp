#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_M_Fire1/z_en_m_fire1.h"
}

namespace {

struct PendingRoll {
    bool active;
    float x;
    float y;
    float z;
};

PendingRoll sPending = { false, 0.0f, 0.0f, 0.0f };

void Register() {
    // The nut spawns EN_M_FIRE1 (the flash) where it lands
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorSpawn>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_M_FIRE1) {
            return;
        }
        // The flash normally stuns and hurts everything around it (angers cuccos, breaks signs).
        // Keep the visual, remove the attack.
        EnMFire1* flash = reinterpret_cast<EnMFire1*>(actor);
        flash->collider.base.atFlags &= ~AT_ON;
        flash->collider.info.toucher.dmgFlags = 0;
        sPending = { true, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
    });

    // Roll one frame later so we never spawn actors inside another actor's spawn
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (!sPending.active) {
            return;
        }
        sPending.active = false;
        Cursed::RollDiceAt(sPending.x, sPending.y, sPending.z);
    });
}

} // namespace

static RegisterShipInitFunc nutDiceInit(Register);
