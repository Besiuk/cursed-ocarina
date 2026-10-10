#include "CursedScript_Effects.h"
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

int sPhase = -1;
u16 sSavedDayTime = 0;

void Register() {
    // Day and night flip every second, the original time returns afterwards
    Cursed::RegisterEffect({
        "dice_time_flip",
        "Time Flip",
        30.0f,
        [] {
            sPhase = -1;
            sSavedDayTime = gSaveContext.dayTime;
        },
        [] { gSaveContext.dayTime = sSavedDayTime; },
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr || !Cursed::IsActive("dice_time_flip")) {
            return;
        }
        const int phase = static_cast<int>(Cursed::RemainingSeconds("dice_time_flip")) & 1;
        if (phase != sPhase) {
            sPhase = phase;
            gSaveContext.dayTime = phase != 0 ? 0x8000 : 0x0000;
        }
    });
}

} // namespace

static RegisterShipInitFunc diceTimeFlipInit(Register);
