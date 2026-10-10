#include "CursedScript_Debug.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Cursed {

bool gDebugInfiniteNuts = false;
bool gDebugNoDamage = false;

namespace {

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr || !GameInteractor::IsSaveLoaded()) {
            return;
        }
        if (gDebugInfiniteNuts) {
            // Make sure Link owns nuts, then keep the stack full
            if (gSaveContext.inventory.items[SLOT_NUT] != ITEM_NUT) {
                gSaveContext.inventory.items[SLOT_NUT] = ITEM_NUT;
            }
            gSaveContext.inventory.ammo[SLOT_NUT] = 30;
        }
        if (gDebugNoDamage) {
            gSaveContext.health = gSaveContext.healthCapacity;
        }
    });
}

} // namespace

} // namespace Cursed

static RegisterShipInitFunc cursedDebugInit(Cursed::Register);
