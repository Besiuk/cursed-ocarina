#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_minish_link",
        "Minish Link",
        30.0f,
        [] { GameInteractor_SetLinkSize(GI_LINK_SIZE_MINISH); },
        [] { GameInteractor_SetLinkSize(GI_LINK_SIZE_RESET); },
    });
}

} // namespace

static RegisterShipInitFunc diceMinishLinkInit(Register);
