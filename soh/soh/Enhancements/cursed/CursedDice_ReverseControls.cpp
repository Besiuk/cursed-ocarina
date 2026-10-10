#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_reverse_controls",
        "Reverse Controls",
        30.0f,
        [] { GameInteractor::State::ReverseControlsActive = true; },
        [] { GameInteractor::State::ReverseControlsActive = false; },
    });
}

} // namespace

static RegisterShipInitFunc diceReverseControlsInit(Register);
