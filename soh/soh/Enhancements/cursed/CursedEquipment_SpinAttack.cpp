#include <cmath>
#include <cstdlib>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Endless spin attack: once the sword is charged and released, Link keeps spinning until he
//  - bumps into a wall (he is thrown back like after a hit from a jump attack),
//  - is hit by an enemy (the normal hit reaction ends the spin),
//  - or touches water.
// The spin animation plays up to its middle, that pose is then frozen, and Link is turned around his
// vertical axis by hand, faster and faster (exponentially) up to ten times the starting speed.
// Meanwhile he walks at a slow, iron-boots-like pace and screams the whole time.
// Falling off a ledge does not interrupt the spin.
// Works with and without magic: the magic spin takes the spin flag away, so it is only used as a trigger.

namespace {

// --- Tuning ---------------------------------------------------------------------------------------
constexpr float kMoveSpeed = 3.2f;       // units per frame at full stick, tuned to feel like iron boots
constexpr int kScreamInterval = 30;      // frames between two long screams
constexpr int kWallGraceFrames = 6;      // no wall bounce right after the release
constexpr float kBounceSpeed = 14.0f;    // how far Link is thrown back from a wall
constexpr float kBounceHeight = 8.0f;
constexpr float kFreezeAt = 0.5f;        // part of the animation at which the pose is frozen
constexpr float kTurnStart = 0x0C00;     // turn per frame when the freeze begins (about one turn per 21 frames)
constexpr float kTurnMax = kTurnStart * -4.0f; // five times as fast; stays below half a turn per frame
constexpr float kTurnGrowth = 0.3f;     // speed-up per frame (about +10 % each frame)
// ---------------------------------------------------------------------------------------------------

bool sActive = false;
bool sWaitForRelease = false; // set when we ended the spin ourselves; vanilla must finish it first
const void* sSpinAnimation = nullptr; // the spin animation we hold; a different one means the spin is over
float sFreezeFrame = 0.0f;
bool sFrozen = false;
float sTurnSpeed = 0.0f;
int sAge = 0;
int sScreamTimer = 0;

bool IsSpinAnimation(int animation) {
    return animation >= PLAYER_MWA_SPIN_ATTACK_1H && animation <= PLAYER_MWA_BIG_SPIN_2H;
}

void EndSpin() {
    sActive = false;
    sWaitForRelease = true;
}

void Register() {
    // Before Link's update: hold the spin pose so the animation never ends or restarts
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (!sActive || actor->id != ACTOR_PLAYER) {
            return;
        }
        Player* link = reinterpret_cast<Player*>(actor);
        // The spin happens in place; walking is done by hand below
        link->skelAnime.movementFlags = 0;

        if (!sFrozen && link->skelAnime.curFrame + link->skelAnime.playSpeed >= sFreezeFrame) {
            sFrozen = true;
        }
        if (sFrozen) {
            link->skelAnime.curFrame = sFreezeFrame;
            link->skelAnime.playSpeed = 0.0f;
            // The sword must stay active even if the frozen frame lies outside the swing window
            if (link->meleeWeaponState == 0) {
                link->meleeWeaponState = 1;
            }
        }
    });

    // After Link's update: start / end the endless spin, turn, move, scream, bounce
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        PlayState* play = gPlayState;
        if (play == nullptr) {
            return;
        }
        Player* link = GET_PLAYER(play);
        const bool spinFlag = (link->stateFlags2 & PLAYER_STATE2_SPIN_ATTACKING) != 0;

        // With magic, the spin-attack light (EN_M_THUNDER) takes the flag away one frame after the
        // release, so the flag only serves as the trigger. Afterwards the animation tells whether Link
        // is still spinning: any hit, knockback or finished spin switches to a different animation.
        if (sActive && static_cast<const void*>(link->skelAnime.animation) != sSpinAnimation) {
            sActive = false;
        }
        if (!spinFlag) {
            sWaitForRelease = false;
        }

        if (!sActive) {
            if (!spinFlag || sWaitForRelease || !IsSpinAnimation(link->meleeWeaponAnimation) ||
                play->csCtx.state != 0) {
                return;
            }
            sActive = true;
            sSpinAnimation = static_cast<const void*>(link->skelAnime.animation);
            sFreezeFrame = link->skelAnime.endFrame * kFreezeAt;
            sFrozen = false;
            sTurnSpeed = kTurnStart;
            sAge = 0;
            sScreamTimer = 0;
        }

        sAge++;

        // Water, conversations and cutscenes end the endless spin
        if ((link->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_TALKING)) || play->csCtx.state != 0) {
            EndSpin();
            return;
        }

        // Long scream, over and over (adult / child voice)
        if (sScreamTimer > 0) {
            sScreamTimer--;
        } else {
            Player_PlaySfx(&link->actor, LINK_IS_ADULT ? NA_SE_VO_LI_FALL_L : NA_SE_VO_LI_FALL_L_KID);
            sScreamTimer = kScreamInterval;
        }

        // Once the pose is frozen, turn faster and faster
        if (sFrozen) {
            link->actor.shape.rot.y += static_cast<s16>(sTurnSpeed);
            sTurnSpeed = fminf(sTurnSpeed * kTurnGrowth, kTurnMax);
        }

        // Walk with the control stick at a slow pace (same stick conversion as Link's own movement)
        Input* input = &play->state.input[0];
        f32 magnitude = 0.0f;
        s16 stickAngle = 0;
        func_80077D10(&magnitude, &stickAngle, input);
        bool moving = false;
        s16 moveYaw = 0;
        if (magnitude > 8.0f) {
            moving = true;
            moveYaw = stickAngle + Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
            const float speed = kMoveSpeed * magnitude / 60.0f;
            link->actor.world.pos.x += Math_SinS(moveYaw) * speed;
            link->actor.world.pos.z += Math_CosS(moveYaw) * speed;
        }

        // Wall: only when actually walking into it
        if (moving && sAge > kWallGraceFrames && (link->actor.bgCheckFlags & BGCHECKFLAG_WALL)) {
            const s16 towardsWall = static_cast<s16>(link->actor.wallYaw + 0x8000);
            const s16 diff = static_cast<s16>(moveYaw - towardsWall);
            if (std::abs(static_cast<int>(diff)) < 0x4000) {
                Actor_SetPlayerKnockbackLargeNoDamage(play, &link->actor, kBounceSpeed, link->actor.wallYaw,
                                                      kBounceHeight);
                EndSpin();
            }
        }
    });
}

} // namespace

static RegisterShipInitFunc spinAttackInit(Register);
