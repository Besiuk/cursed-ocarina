#include <algorithm>
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

constexpr float kScale = 0.07f;

std::vector<Actor*> sGiants;

bool IsGiant(Actor* actor) {
    return actor != nullptr && std::find(sGiants.begin(), sGiants.end(), actor) != sGiants.end();
}

void Register() {
    Cursed::RegisterEffect({
        "dice_giant_cucco",
        "Giant Cucco",
        0.0f,
        [] {
            Actor* actor = static_cast<Actor*>(Cursed::SpawnActorAtOrigin(ACTOR_EN_NIW, 0, 0.0f));
            if (actor == nullptr) {
                Cursed::Notify("Cannot spawn right now");
                return;
            }
            Actor_SetScale(actor, kScale);
            EnNiw* niw = reinterpret_cast<EnNiw*>(actor);
            niw->collider.dim.radius = static_cast<s16>(niw->collider.dim.radius * 7);
            niw->collider.dim.height = static_cast<s16>(niw->collider.dim.height * 7);
            sGiants.push_back(actor);
            Cursed::ShowInfinite(actor, "Giant Cucco");
        },
        nullptr,
    });

    // The angry cuccos of a giant cucco are spawned as its children: make them giant too
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool*) {
        if (sGiants.empty()) {
            return;
        }
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id == ACTOR_EN_ATTACK_NIW && IsGiant(actor->parent)) {
            Actor_SetScale(actor, kScale);
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        sGiants.erase(std::remove(sGiants.begin(), sGiants.end(), static_cast<Actor*>(refActor)), sGiants.end());
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sGiants.clear(); });
}

} // namespace

static RegisterShipInitFunc diceGiantCuccoInit(Register);
