// Kokiri sword, "Modern Zelda Sword": it breaks after 10 hits (wall, fence, stone, grass, enemy, ...).
// The hits are counted by a tiny hook in z_player.c (Cursed_OnSwordHit, see the .h). Counter and break are
// stored in the save file. The sword stays "owned" and equipped, so Mido and everyone else still accept it.
// When it breaks: B button shows the broken sword, Link puts the sword away and presents the broken blade
// (get-item pose with a custom text). Pressing B afterwards shows a custom "unusable" text.
// The equipment menu shows the broken sword too (small hook in z_kaleido_equipment.c).
// Adult Link, if he ever holds it, still can use it.
#include "CursedEquipment_KokiriSword.h"

#include <string>

#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
// not in a header, but not static in the player actor
void Player_UseItem(PlayState* play, Player* player, s32 item);
}
// defined in OTRGlobals.cpp (extern "C"), not visible through the header here
extern "C" GetItemEntry ItemTable_Retrieve(int16_t getItemID);

namespace {

constexpr int kHitsToBreak = 10;
constexpr uint16_t kKokiriSwordGetItemText = 0x00A4;
constexpr uint16_t kBrokenPresentText = 0x7F10; // our own id, only ever shown through the hook below
constexpr int kBPressWindowFrames = 15;         // a text opening this soon after a B press is the "unusable" one

int sHits = 0;
bool sBroken = false;
bool sRegistered = false;
bool sCountedThisSwing = false;
bool sPresentPending = false;
int sBPressFrames = 0;

void SaveSection(const SaveContext& saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("broken", sBroken);
    SaveManager::Instance->SaveData("hits", sHits);
}

void LoadSection() {
    SaveManager::Instance->LoadData("broken", sBroken, false);
    SaveManager::Instance->LoadData("hits", sHits, 0);
}

void InitSave(bool isDebug) {
    sBroken = false;
    sHits = 0;
    sPresentPending = false;
}

// The B button shows the broken sword; pressing it does nothing useful
void SetBButton(PlayState* play, u8 item) {
    if (gSaveContext.equips.buttonItems[0] != item) {
        gSaveContext.equips.buttonItems[0] = item;
        Interface_LoadItemIcon1(play, 0);
    }
}

void Break(PlayState* play, Player* link) {
    sBroken = true;
    sPresentPending = true;
    // The same flying blade piece and sound as the Giant's Knife breaking
    EffectSsStick_Spawn(play, &link->bodyPartsPos[PLAYER_BODYPART_R_HAND], link->actor.shape.rot.y + 0x8000);
    Player_PlaySfx(&link->actor, NA_SE_IT_MAJIN_SWORD_BROKEN);
    SetBButton(play, ITEM_SWORD_BROKEN);
}

// Link holds up the broken blade. Uses the broken Goron's sword pose and model, but gives nothing
// (itemId = ITEM_NONE is skipped by the vanilla get-item code) and shows our own text.
bool PresentBrokenSword(PlayState* play) {
    GetItemEntry entry = ItemTable_Retrieve(GI_SWORD_BROKEN);
    entry.itemId = ITEM_NONE;
    entry.textId = kBrokenPresentText;
    return GiveItemEntryWithoutActor(play, entry) != 0;
}

void OnPlayerUpdate() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    Player* link = GET_PLAYER(play);

    if (link->meleeWeaponState == 0) {
        sCountedThisSwing = false;
    }

    if (sBPressFrames > 0) {
        sBPressFrames--;
    }

    if (!sBroken || !LINK_IS_CHILD) {
        return;
    }

    // Broken from here on. Only child Link is affected.
    if (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KOKIRI) {
        SetBButton(play, ITEM_SWORD_BROKEN);
    }
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
        sBPressFrames = kBPressWindowFrames;
    }

    // Wait until the breaking swing is over, put the sword away, then show the broken blade
    if (link->meleeWeaponState == 0) {
        if (link->heldItemAction == PLAYER_IA_SWORD_KOKIRI) {
            Player_UseItem(play, link, ITEM_NONE);
        }
        if (sPresentPending && PresentBrokenSword(play)) {
            sPresentPending = false;
        }
    }
}

void LoadText(const std::string& text, TextBoxType type) {
    CustomMessage msg(text, type);
    msg.AutoFormat();
    msg.LoadIntoFont();
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);

    // "You got the Modern Zelda Sword!"
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
        kKokiriSwordGetItemText, [](uint16_t* textId, bool* loadFromMessageTable) {
            *loadFromMessageTable = false;
            LoadText("You got the %rModern Zelda Sword%w!&Lorem ipsum dolor sit amet, "
                     "it breaks after ten hits.",
                     TEXTBOX_TYPE_BLUE);
        });

    // Link presents the broken sword
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
        kBrokenPresentText, [](uint16_t* textId, bool* loadFromMessageTable) {
            *loadFromMessageTable = false;
            LoadText("Oh no! Your %rModern Zelda Sword%w broke!&Lorem ipsum dolor sit amet, "
                     "consectetur adipiscing elit.",
                     TEXTBOX_TYPE_BLUE);
        });

    // Pressing B with the broken sword: replaces the vanilla "this item doesn't work here" text
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(
        [](uint16_t* textId, bool* loadFromMessageTable) {
            if (!sBroken || !LINK_IS_CHILD || sBPressFrames == 0 || *textId == kBrokenPresentText ||
                *textId == kKokiriSwordGetItemText) {
                return;
            }
            sBPressFrames = 0;
            *loadFromMessageTable = false;
            LoadText("Your %rModern Zelda Sword%w is broken.&Lorem ipsum dolor sit amet, it cannot be used.",
                     TEXTBOX_TYPE_BLACK);
        });

    // Saved with the save file; registered only once even if the init functions run again
    if (!sRegistered && SaveManager::Instance != nullptr) {
        sRegistered = true;
        SaveManager::Instance->AddLoadFunction("cursedKokiriSword", 1, LoadSection);
        SaveManager::Instance->AddSaveFunction("cursedKokiriSword", 1, SaveSection, true, SECTION_PARENT_NONE);
        SaveManager::Instance->AddInitFunction(InitSave);
    }
}

} // namespace

// Called from z_player.c every time a sword swing hits something (wall or object/enemy)
extern "C" void Cursed_OnSwordHit(PlayState* play, Player* player) {
    if (sBroken || sCountedThisSwing || player->heldItemAction != PLAYER_IA_SWORD_KOKIRI) {
        return;
    }
    sCountedThisSwing = true; // one swing counts once, however many things it touches
    if (++sHits >= kHitsToBreak) {
        Break(play, player);
    }
}

// Called from z_kaleido_equipment.c to pick the sword icon
extern "C" int Cursed_IsKokiriSwordBroken(void) {
    return sBroken ? 1 : 0;
}

namespace Cursed {

bool IsKokiriSwordBroken() {
    return sBroken;
}

void RepairKokiriSword() {
    sBroken = false;
    sHits = 0;
    sPresentPending = false;
    if (gPlayState != nullptr && LINK_IS_CHILD && gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BROKEN) {
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
        Interface_LoadItemIcon1(gPlayState, 0);
    }
}

} // namespace Cursed

static RegisterShipInitFunc kokiriSwordInit(Register);
