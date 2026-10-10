// Renames items in the pause menu ("Modern Zelda Sword", "BOOMerang", "Totally regular stick" / "Stick Saber")
// by swapping the name texture.
// Uses the game's own VB_DRAW_CUSTOM_ITEM_NAME hook (the same one Roc's Feather uses), so no edits of
// the SoH sources are needed. The PNGs live in soh/assets/custom/textures/item_name_static/ and are
// packed into soh.o2r by the build.
#include <cstdarg>
#include <cstring>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include "CursedEquipment_StickSaber.h"
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

// Same name in every language
constexpr const char* kModernZeldaSword = "__OTR__textures/item_name_static/gModernZeldaSwordItemNameTex";
constexpr const char* kBoomErang = "__OTR__textures/item_name_static/gBoomErangItemNameTex";
constexpr const char* kRegularStick = "__OTR__textures/item_name_static/gTotallyRegularStickItemNameTex";
constexpr const char* kStickSaber = "__OTR__textures/item_name_static/gStickSaberItemNameTex";

// The game crashes when a name texture is missing from soh.o2r, so only use ours if it can be loaded.
// Checked once per texture.
bool TextureExists(const char* path, int& cache) {
    if (cache == 0) {
        auto res = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path);
        cache = (res != nullptr) ? 1 : -1;
    }
    return cache > 0;
}

void Register() {
    REGISTER_VB_SHOULD(VB_DRAW_CUSTOM_ITEM_NAME, {
        u32 namedItem = va_arg(args, u32);
        static int swordCache = 0;
        static int boomCache = 0;
        static int regularCache = 0;
        static int saberCache = 0;
        const char* textureName = nullptr;
        if (namedItem == ITEM_SWORD_KOKIRI && TextureExists(kModernZeldaSword, swordCache)) {
            textureName = kModernZeldaSword;
        } else if (namedItem == ITEM_BOOMERANG && TextureExists(kBoomErang, boomCache)) {
            textureName = kBoomErang;
        } else if (namedItem == ITEM_STICK) {
            // lit it is a saber, otherwise it is "just a stick"
            if (Cursed::IsStickSaberActive() && TextureExists(kStickSaber, saberCache)) {
                textureName = kStickSaber;
            } else if (TextureExists(kRegularStick, regularCache)) {
                textureName = kRegularStick;
            }
        }
        if (textureName != nullptr && gPlayState != nullptr) {
            *should = true;
            memcpy(gPlayState->pauseCtx.nameSegment, textureName, strlen(textureName) + 1);
        }
    });
}

} // namespace

static RegisterShipInitFunc itemNamesInit(Register);
