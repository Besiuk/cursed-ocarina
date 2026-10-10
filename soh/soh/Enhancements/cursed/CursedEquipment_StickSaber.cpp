// Stick Saber: the Deku Stick as a lightsaber.
//
//  - A normal stick is completely normal.
//  - Once it burns (Din's Fire, torch, fire arrow, ...) it becomes a Stick Saber:
//      * it never burns down and does not break on walls,
//      * every swing uses up one stick from the inventory,
//      * a blue-white blade glows over the stick and the level gets darker; the vanilla flame is switched off,
//      * the swing hits like a Biggoron slash and everything it touches (not Link) catches fire,
//      * the last swing puts the fire out and the stick breaks,
//      * putting it away (or swimming) keeps the state; drawing the stick again lights it again.
//  - The stick model itself is invisible (one small edit in z_player_lib.c), Link holds only the glow.
//  - The state is saved with the save file.
//  - The normal stick is called "Totally regular stick" (pause menu name, pick-up text); lit it is "Stick Saber".
//
// The first time it catches fire Link presents the saber like a new item (game frozen, arms up, the blade
// glows above him). The glow and the darkening are done by a small carrier actor: the spin attack's own light
// actor (EN_M_THUNDER), with its update and draw replaced. Its destroy function resets the darkening by itself.
// The blade is a simple self-made mesh in the stick's own coordinate frame, so it always lies exactly on the stick.
#include "CursedEquipment_StickSaber.h"

#include <cmath>
#include <string>

#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"
#include "soh/OTRGlobals.h"
#include "soh/frame_interpolation.h" // OPEN_DISPS needs its C linkage declarations
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_M_Thunder/z_en_m_thunder.h"
extern PlayState* gPlayState;
// not in a header, but not static in the player actor
void Player_UseItem(PlayState* play, Player* player, s32 item);
}
// defined in OTRGlobals.cpp (extern "C"), not visible through the header here
extern "C" GetItemEntry ItemTable_Retrieve(int16_t getItemID);

// extern "C" on purpose: OPEN_DISPS only links against the game's C functions in a function with C linkage.
extern "C" void CursedSaber_Update(Actor* thisx, PlayState* play);
extern "C" void CursedSaber_Draw(Actor* thisx, PlayState* play);

namespace {

// --- Tuning ---------------------------------------------------------------------------------------
constexpr uint16_t kStickGetItemText = 0x0037;  // vanilla "You got a Deku Stick" (also used for the packs)
constexpr uint16_t kIgniteText = 0x7F11;        // our own id, shown when a stick catches fire for the first time
constexpr float kDimming = 0.5f;                // how dark the level gets (the full spin attack charge is 1.0)
constexpr float kDimmingStep = 0.04f;           // fade in/out speed per frame
constexpr int kFireInterval = 6;                // frames between two fire touches of the same blade
constexpr float kFireRadius = 22.0f;
constexpr float kFireHeight = 44.0f;
constexpr int kFireBoxes = 3;                   // spread along the blade: near the hand, middle, tip
constexpr float kFireBoxAt[kFireBoxes] = { 0.25f, 0.6f, 1.0f };
// Blade mesh in the stick's own frame (the stick's long axis is its Y axis, the grip end is at 0, the tip at about 5400)
constexpr float kBladeStart = 450.0f;       // just above Link's hand
constexpr float kBladeEnd = 5400.0f;        // the tip of the stick
constexpr float kBladeCoreWidth = 110.0f;   // half width of the bright core
constexpr float kBladeHaloWidth = 260.0f;   // half width of the soft halo
constexpr float kCoreTurn = 0.45f;          // radians per frame the core turns around the stick
constexpr float kHaloTurn = 0.28f;          // the halo turns the other way, slower
// While Link presents the saber (blade in world units; Link himself is scaled by 0.01)
constexpr float kPresentScale = 0.003f;    // about 15 units long
constexpr float kPresentHeight = 28.0f;    // above Link's head
constexpr float kPresentTurn = 0.15f;      // radians per frame
// ---------------------------------------------------------------------------------------------------

bool sSaber = false;        // saved: the stick is a Stick Saber (also while put away)
bool sSeenPickup = false;   // saved: the first-pick-up text was shown
bool sSeenIgnite = false;   // saved: the first-ignition text was shown
bool sRegistered = false;

bool sIgniteTextPending = false;
bool sPresenting = false;   // Link holds the new saber up (get-item pose)
bool sSawGetting = false;
int sPresentFrames = 0;
Vtx sBladeCore[24];
Vtx sBladeHalo[24];
bool sBladeBuilt = false;
bool sPrevSwinging = false;
bool sLastSwing = false;    // the swing that uses the last stick is running
bool sBoxesReady = false;
Actor* sCarrier = nullptr;
ColliderCylinder sFireBoxes[kFireBoxes];

ColliderCylinderInit sFireInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { DMG_MAGIC_FIRE, 0x00, 1 }, { 0x00000000, 0x00, 0x00 }, TOUCH_ON | TOUCH_SFX_NONE, BUMP_NONE,
      OCELEM_NONE },
    { 22, 44, 0, { 0, 0, 0 } },
};

void SaveSection(const SaveContext& saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("saber", sSaber);
    SaveManager::Instance->SaveData("seenPickup", sSeenPickup);
    SaveManager::Instance->SaveData("seenIgnite", sSeenIgnite);
}

void LoadSection() {
    SaveManager::Instance->LoadData("saber", sSaber, false);
    SaveManager::Instance->LoadData("seenPickup", sSeenPickup, false);
    SaveManager::Instance->LoadData("seenIgnite", sSeenIgnite, false);
}

void InitSave(bool isDebug) {
    sSaber = false;
    sSeenPickup = false;
    sSeenIgnite = false;
    sIgniteTextPending = false;
    sPresenting = false;
    sPrevSwinging = false;
    sLastSwing = false;
}

void LoadText(const std::string& text, TextBoxType type) {
    CustomMessage msg(text, type);
    msg.AutoFormat();
    msg.LoadIntoFont();
}

// Two crossed strips along the stick (4 rows x 3 columns each): opaque in the middle, transparent at the edges
// and at both ends, so it looks like a soft glowing blade from every side.
void BuildBlade(Vtx* v, float halfWidth, u8 r, u8 g, u8 b, float alpha) {
    const float rows[4] = { kBladeStart, kBladeStart + 350.0f, kBladeEnd - 450.0f, kBladeEnd };
    const float rowAlpha[4] = { 0.35f, 1.0f, 1.0f, 0.0f };
    for (int plane = 0; plane < 2; plane++) {
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 3; col++) {
                Vtx& vtx = v[plane * 12 + row * 3 + col];
                const float side = (col - 1) * halfWidth;
                vtx.v.ob[0] = static_cast<s16>(plane == 0 ? side : 0.0f);
                vtx.v.ob[1] = static_cast<s16>(rows[row]);
                vtx.v.ob[2] = static_cast<s16>(plane == 0 ? 0.0f : side);
                vtx.v.flag = 0;
                vtx.v.tc[0] = 0;
                vtx.v.tc[1] = 0;
                vtx.v.cn[0] = r;
                vtx.v.cn[1] = g;
                vtx.v.cn[2] = b;
                vtx.v.cn[3] = static_cast<u8>(col == 1 ? 255.0f * alpha * rowAlpha[row] : 0.0f);
            }
        }
    }
}

// Emits the triangles of one blade (24 vertices) into the transparent display list.
// No OPEN_DISPS here (it would need C linkage), the list is addressed directly.
void DrawBladeMesh(GraphicsContext* gfxCtx, Vtx* v) {
    gSPVertex(gfxCtx->polyXlu.p++, reinterpret_cast<uintptr_t>(v), 24, 0);
    for (int plane = 0; plane < 2; plane++) {
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 2; col++) {
                const int a = plane * 12 + row * 3 + col;
                gSP2Triangles(gfxCtx->polyXlu.p++, a, a + 1, a + 3, 0, a + 1, a + 4, a + 3, 0);
            }
        }
    }
}

// The get-item pose normally shows the item model; ours shows nothing, the glow is drawn by the carrier
void NoItemModel(PlayState* play, GetItemEntry* entry) {
}

// Link holds the saber up like a new item; the game is frozen while the text is shown
bool PresentSaber(PlayState* play) {
    GetItemEntry entry = ItemTable_Retrieve(GI_STICKS_1);
    entry.itemId = ITEM_NONE; // nothing is given, only the pose and our text
    entry.textId = kIgniteText;
    entry.drawFunc = NoItemModel; // no stick model above his hands
    return GiveItemEntryWithoutActor(play, entry) != 0;
}

bool HoldsStick(Player* link) {
    return link->heldItemAction == PLAYER_IA_DEKU_STICK;
}

void Activate() {
    sSaber = true;
    sLastSwing = false;
    if (!sSeenIgnite) {
        sIgniteTextPending = true;
    }
    Cursed::Notify("Stick Saber ignited");
}

// The last swing is over: the fire goes out and the stick breaks
void BreakSaber(PlayState* play, Player* link) {
    sSaber = false;
    sLastSwing = false;
    sPrevSwinging = false;
    link->unk_860 = 0;
    link->unk_85C = 0.5f;
    EffectSsStick_Spawn(play, &link->bodyPartsPos[PLAYER_BODYPART_L_HAND], link->actor.shape.rot.y + 0x8000);
    Player_PlaySfx(&link->actor, NA_SE_IT_WOODSTICK_BROKEN);
    Player_UseItem(play, link, ITEM_NONE);
    Cursed::Notify("The Stick Saber burnt out");
}

// Fire touch boxes along the blade; the game then lets enemies burn, grass catch fire and so on
void UpdateFireBoxes(PlayState* play, Player* link) {
    if (!sBoxesReady) {
        for (int i = 0; i < kFireBoxes; i++) {
            Collider_InitCylinder(play, &sFireBoxes[i]);
            Collider_SetCylinder(play, &sFireBoxes[i], &link->actor, &sFireInit);
        }
        sBoxesReady = true;
    }
    if ((play->gameplayFrames % kFireInterval) != 0) {
        return;
    }
    const Vec3f base = link->bodyPartsPos[PLAYER_BODYPART_L_HAND];
    const Vec3f tip = link->meleeWeaponInfo[0].tip;
    for (int i = 0; i < kFireBoxes; i++) {
        ColliderCylinder& box = sFireBoxes[i];
        const float t = kFireBoxAt[i];
        box.dim.pos.x = static_cast<s16>(base.x + (tip.x - base.x) * t);
        box.dim.pos.y = static_cast<s16>(base.y + (tip.y - base.y) * t - kFireHeight * 0.5f);
        box.dim.pos.z = static_cast<s16>(base.z + (tip.z - base.z) * t);
        box.base.atFlags = AT_ON | AT_TYPE_PLAYER;
        CollisionCheck_SetAT(play, &play->colChkCtx, &box.base);
    }
}

// The swing hits like a Biggoron slash (instead of a stick)
void BoostSwing(Player* link) {
    for (int i = 0; i < 2; i++) {
        ColliderQuad& quad = link->meleeWeaponQuads[i];
        if (quad.info.toucher.dmgFlags == DMG_DEKU_STICK) {
            quad.info.toucher.dmgFlags = DMG_SLASH_GIANT;
        } else if (quad.info.toucher.dmgFlags == 0x08000000) { // the stick's jump slash
            quad.info.toucher.dmgFlags = DMG_JUMP_GIANT;
        } else {
            continue;
        }
        quad.info.toucherFlags = TOUCH_ON | TOUCH_NEAREST; // no wooden "tok" sound
    }
}

void EnsureCarrier(PlayState* play, Player* link) {
    if (sCarrier != nullptr || Play_InCsMode(play)) {
        return;
    }
    Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_M_THUNDER, link->actor.world.pos.x,
                               link->actor.world.pos.y, link->actor.world.pos.z, 0, 0, 0, 1);
    if (actor != nullptr) {
        actor->update = CursedSaber_Update;
        actor->draw = CursedSaber_Draw;
        sCarrier = actor;
    }
}

void OnPlayerUpdate() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    Player* link = GET_PLAYER(play);

    // The presentation is over when Link leaves the get-item pose
    if (sPresenting) {
        sPresentFrames++;
        const bool getting = (link->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0;
        sSawGetting = sSawGetting || getting;
        if ((sSawGetting && !getting) || sPresentFrames > 1200) {
            sPresenting = false;
        }
    }

    if (!HoldsStick(link)) {
        sPrevSwinging = false;
        return;
    }

    // Fire contact turns the stick into a saber
    if (!sSaber && link->unk_860 != 0 && AMMO(ITEM_STICK) > 0) {
        Activate();
    }
    if (!sSaber) {
        return;
    }

    // Water: the saber is only put away
    if (link->stateFlags1 & PLAYER_STATE1_IN_WATER) {
        Player_UseItem(play, link, ITEM_NONE);
        return;
    }

    // One stick per swing; the swing that uses the last one ends it
    const bool swinging = link->meleeWeaponState != 0;
    if (swinging && !sPrevSwinging) {
        Inventory_ChangeAmmo(ITEM_STICK, -1);
        if (AMMO(ITEM_STICK) <= 0) {
            sLastSwing = true;
        }
    }
    sPrevSwinging = swinging;
    if (swinging) {
        BoostSwing(link);
    }
    if (!swinging && sLastSwing) {
        BreakSaber(play, link);
        return;
    }

    // The first time: Link presents the saber like a new item
    if (sIgniteTextPending && !swinging && play->msgCtx.msgMode == MSGMODE_NONE && !Play_InCsMode(play) &&
        PresentSaber(play)) {
        sIgniteTextPending = false;
        sSeenIgnite = true;
        sPresenting = true;
        sSawGetting = false;
        sPresentFrames = 0;
    }

    UpdateFireBoxes(play, link);
    EnsureCarrier(play, link);
}

void Register() {
    // In saber mode the stick never burns down, burns out or breaks, and has no flame. The fire flag is kept
    // set, so drawing the saber again lights it again.
    REGISTER_VB_SHOULD(VB_DEKU_STICK_BREAK, {
        if (sSaber) {
            *should = false;
        }
    });
    REGISTER_VB_SHOULD(VB_DEKU_STICK_BURN_OUT, {
        if (sSaber) {
            *should = false;
        }
    });
    REGISTER_VB_SHOULD(VB_DEKU_STICK_BURN_DOWN, {
        if (sSaber) {
            *should = false;
        }
    });
    REGISTER_VB_SHOULD(VB_DEKU_STICK_BE_ON_FIRE, {
        if (sSaber && gPlayState != nullptr) {
            Player* player = GET_PLAYER(gPlayState);
            // Keeps the fire flag set (torches, drawing the stick again) but skips the vanilla burning code,
            // which is what makes the orange flame at the tip
            player->unk_860 = 200;
            player->unk_85C = 1.0f;
            *should = false;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        if (refActor == sCarrier) {
            sCarrier = nullptr;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        sCarrier = nullptr;
        sPresenting = false;
        sBoxesReady = false; // the colliders point to Link's actor, which is new in every scene
        sPrevSwinging = false;
    });

    // "You got a Totally regular stick!" (the first time with a hint that something is off)
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
        kStickGetItemText, [](uint16_t* textId, bool* loadFromMessageTable) {
            *loadFromMessageTable = false;
            if (!sSeenPickup) {
                sSeenPickup = true;
                LoadText("You got a %rTotally regular stick%w!&Lorem ipsum dolor sit amet. Nothing to see here, "
                         "just a stick.",
                         TEXTBOX_TYPE_BLUE);
            } else {
                LoadText("You got a %rTotally regular stick%w!", TEXTBOX_TYPE_BLUE);
            }
        });

    // The first time a stick catches fire
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
        kIgniteText, [](uint16_t* textId, bool* loadFromMessageTable) {
            *loadFromMessageTable = false;
            LoadText("The %rTotally regular stick%w caught fire...&It is a %bStick Saber%w!&Lorem ipsum dolor sit "
                     "amet, every swing will cost you a stick.",
                     TEXTBOX_TYPE_BLUE);
        });

    // Saved with the save file; registered only once even if the init functions run again
    if (!sRegistered && SaveManager::Instance != nullptr) {
        sRegistered = true;
        SaveManager::Instance->AddLoadFunction("cursedStickSaber", 1, LoadSection);
        SaveManager::Instance->AddSaveFunction("cursedStickSaber", 1, SaveSection, true, SECTION_PARENT_NONE);
        SaveManager::Instance->AddInitFunction(InitSave);
    }
}

} // namespace

// The carrier's update: dims the level, lights the surroundings and follows the blade
extern "C" void CursedSaber_Update(Actor* thisx, PlayState* play) {
    EnMThunder* this_ = reinterpret_cast<EnMThunder*>(thisx);
    Player* link = GET_PLAYER(play);

    const bool on = sSaber && (sPresenting || link->heldItemAction == PLAYER_IA_DEKU_STICK) &&
                    (sPresenting || !Play_InCsMode(play));
    if (!on) {
        // the destroy function of the spin attack actor resets the darkening and removes the light
        Actor_Kill(thisx);
        return;
    }

    Math_StepToF(&this_->dimmingIntensity, kDimming, kDimmingStep);
    Environment_AdjustLights(play, this_->dimmingIntensity, 850.0f, 0.2f, 0.0f);

    const Vec3f hand = link->bodyPartsPos[PLAYER_BODYPART_L_HAND];
    const Vec3f tip = link->meleeWeaponInfo[0].tip;
    thisx->world.pos.x = (hand.x + tip.x) * 0.5f;
    thisx->world.pos.y = (hand.y + tip.y) * 0.5f;
    thisx->world.pos.z = (hand.z + tip.z) * 0.5f;
    Lights_PointNoGlowSetInfo(&this_->lightInfo, static_cast<s16>(thisx->world.pos.x),
                              static_cast<s16>(thisx->world.pos.y), static_cast<s16>(thisx->world.pos.z), 150, 190,
                              255, 500);
}

// The carrier's draw: the glowing blade, lying on the stick (or above Link while he presents it)
extern "C" void CursedSaber_Draw(Actor* thisx, PlayState* play) {
    static f32 sPulse[] = { 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.25f, 0.2f, 0.15f };
    Player* link = GET_PLAYER(play);
    const bool inHand = link->heldItemAction == PLAYER_IA_DEKU_STICK;
    if (!sSaber || (!sPresenting && !inHand)) {
        return;
    }
    if (!sBladeBuilt) {
        BuildBlade(sBladeCore, kBladeCoreWidth, 215, 240, 255, 1.0f);
        BuildBlade(sBladeHalo, kBladeHaloWidth, 90, 170, 255, 0.45f);
        sBladeBuilt = true;
    }
    const u8 alpha = static_cast<u8>(200 + sPulse[play->gameplayFrames & 7] * 180.0f);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    if (sPresenting) {
        // Above Link's head, lying flat and turning around the vertical axis like a rotor
        const Vec3f& head = link->bodyPartsPos[PLAYER_BODYPART_HEAD];
        Matrix_Translate(head.x, head.y + kPresentHeight, head.z, MTXMODE_NEW);
        Matrix_RotateY(static_cast<f32>(play->gameplayFrames) * kPresentTurn, MTXMODE_APPLY);
        Matrix_RotateZ(-1.5708f, MTXMODE_APPLY); // the blade's long axis (Y) onto the horizontal
        Matrix_Scale(kPresentScale, kPresentScale, kPresentScale, MTXMODE_APPLY);
        Matrix_Translate(0.0f, -(kBladeStart + kBladeEnd) * 0.5f, 0.0f, MTXMODE_APPLY);
    } else {
        // mf_9E0 is the stick's own frame here (the game keeps the stick's translate/rotate/scale in it)
        Matrix_Mult(&link->mf_9E0, MTXMODE_NEW);
    }
    // The base matrix (stick frame or above Link); each layer is then turned and pulsed on its own
    MtxF base;
    Matrix_Get(&base);

    // vertex colors only, no lighting, visible from both sides
    gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineLERP(POLY_XLU_DISP++, SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE, 0,
                      SHADE, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, alpha);

    // Halo: wide, turning one way, pulsing out of step
    Matrix_Put(&base);
    Matrix_RotateY(-static_cast<f32>(play->gameplayFrames) * kHaloTurn, MTXMODE_APPLY);
    const f32 haloPulse = (sPulse[(play->gameplayFrames + 3) & 7] * 3.0f) + 1.0f;
    Matrix_Scale(haloPulse, 1.0f, haloPulse, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    DrawBladeMesh(play->state.gfxCtx, sBladeHalo);

    // Core: turning the other way, pulsing like the spin attack's charge (thickness only, not the length)
    Matrix_Put(&base);
    Matrix_RotateY(static_cast<f32>(play->gameplayFrames) * kCoreTurn, MTXMODE_APPLY);
    const f32 corePulse = (sPulse[play->gameplayFrames & 7] * 2.0f) + 1.0f;
    Matrix_Scale(corePulse, 1.0f, corePulse, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    DrawBladeMesh(play->state.gfxCtx, sBladeCore);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);

    CLOSE_DISPS(play->state.gfxCtx);
}

// Called from z_player_lib.c: the stick model is not drawn while it is a saber (only the glow is)
extern "C" int Cursed_IsStickHidden(void) {
    return sSaber ? 1 : 0;
}

namespace Cursed {

bool IsStickSaberActive() {
    return sSaber;
}

void ResetStickSaber() {
    sSaber = false;
    sPresenting = false;
    if (gPlayState != nullptr) {
        Player* link = GET_PLAYER(gPlayState);
        if (link->heldItemAction == PLAYER_IA_DEKU_STICK) {
            link->unk_860 = 0; // a lit stick in hand goes out
        }
    }
    sLastSwing = false;
    sIgniteTextPending = false;
    sSeenPickup = false;
    sSeenIgnite = false;
}

} // namespace Cursed

static RegisterShipInitFunc stickSaberInit(Register);
