#include "CursedScript_DarkLink.h"
#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_darklink_guard",
        "Dark Link Guard",
        0.0f,
        [] { Cursed::SpawnDarkLinks(Cursed::DarkLinkMode::Ring, 4); },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceDarkLinkGuardInit(Register);
