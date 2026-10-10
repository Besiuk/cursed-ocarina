#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_giant_link",
        "Giant Link",
        30.0f,
        [] { GameInteractor_SetLinkSize(GI_LINK_SIZE_GIANT); },
        [] { GameInteractor_SetLinkSize(GI_LINK_SIZE_RESET); },
    });
}

} // namespace

static RegisterShipInitFunc diceGiantLinkInit(Register);
