#include "CursedScript_DarkLink.h"
#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"

namespace {

void Register() {
    Cursed::RegisterEffect({
        "dice_darklink_shadow",
        "Dark Link Shadow",
        0.0f,
        [] { Cursed::SpawnDarkLinks(Cursed::DarkLinkMode::Side, 1); },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceDarkLinkShadowInit(Register);
