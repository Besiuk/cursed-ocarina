#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_moon_gravity",
        "Moon Gravity",
        30.0f,
        [] { GameInteractor::State::GravityLevel = GI_GRAVITY_LEVEL_LIGHT; },
        [] { GameInteractor::State::GravityLevel = GI_GRAVITY_LEVEL_NORMAL; },
    });
}

} // namespace

static RegisterShipInitFunc diceMoonGravityInit(Register);
