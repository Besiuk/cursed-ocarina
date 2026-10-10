#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_cucco_party",
        "Cucco Party",
        0.0f,
        [] {
            int spawned = 0;
            for (int i = 0; i < 5; i++) {
                void* cucco = Cursed::SpawnActorAtOrigin(ACTOR_EN_NIW, 0, 80.0f);
                if (cucco != nullptr) {
                    Cursed::ShowInfinite(cucco, "Cucco Party");
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

static RegisterShipInitFunc diceCuccoPartyInit(Register);
