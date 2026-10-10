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

void Register() {
    Cursed::RegisterEffect({
        "dice_epona",
        "Epona",
        0.0f,
        [] {
            // The effect origin is gone by the time the object is loaded, so remember it now
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_HORSE, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("Epona could not be loaded");
                    return;
                }
                Player* player = GET_PLAYER(gPlayState);
                const s16 rot = player->actor.shape.rot.y + 0x8000;
                void* horse = Cursed::SpawnActorAt(ACTOR_EN_HORSE, 1, x, y + 20.0f, z, rot);
                if (horse == nullptr) {
                    Cursed::Notify("Epona cannot appear here");
                } else {
                    Cursed::ShowInfinite(horse, "Epona");
                }
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceEponaInit(Register);
