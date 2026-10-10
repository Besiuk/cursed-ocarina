#include <cmath>

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

int sTick = -1;
int sOrbitFrame = 0;

constexpr float kOrbitRadius = 60.0f;
constexpr float kOrbitSpeed = 0.12f;   // radians per frame, about one lap every 2.6 s
constexpr float kBobSpeed = 0.19f;     // up and down, independent of the lap
constexpr float kBaseHeight = 45.0f;   // centre of the bobbing above Link's feet
constexpr float kBobAmplitude = 35.0f;

void Register() {
    Cursed::RegisterEffect({
        "dice_navi_spam",
        "Navi Spam",
        30.0f,
        [] {
            sTick = -1;
            sOrbitFrame = 0;
        },
        nullptr,
    });

    // Navi circles around Link while the effect runs, flying up and down. This runs after Navi's own update,
    // so her normal behaviour and sparkles stay, only her position is replaced.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorUpdate>([](void* refActor) {
        if (gPlayState == nullptr || !Cursed::IsActive("dice_navi_spam")) {
            return;
        }
        Actor* actor = static_cast<Actor*>(refActor);
        Player* player = GET_PLAYER(gPlayState);
        if (actor != player->naviActor) {
            return;
        }
        sOrbitFrame++;
        const float angle = sOrbitFrame * kOrbitSpeed;
        const float tx = player->actor.world.pos.x + std::sin(angle) * kOrbitRadius;
        const float tz = player->actor.world.pos.z + std::cos(angle) * kOrbitRadius;
        const float ty = player->actor.world.pos.y + kBaseHeight + std::sin(sOrbitFrame * kBobSpeed) * kBobAmplitude;
        // Glide there instead of jumping, so she flies in from wherever she was
        Math_ApproachF(&actor->world.pos.x, tx, 0.35f, 40.0f);
        Math_ApproachF(&actor->world.pos.y, ty, 0.35f, 40.0f);
        Math_ApproachF(&actor->world.pos.z, tz, 0.35f, 40.0f);
        actor->shape.rot.y = static_cast<s16>(angle * 10430.0f);
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr || !Cursed::IsActive("dice_navi_spam")) {
            return;
        }
        const int tick = static_cast<int>(Cursed::RemainingSeconds("dice_navi_spam"));
        if (tick == sTick) {
            return;
        }
        sTick = tick;
        static const u16 sounds[3] = { NA_SE_VO_NA_HELLO_0, NA_SE_VO_NA_HELLO_1, NA_SE_VO_NA_HELLO_2 };
        Player_PlaySfx(&GET_PLAYER(gPlayState)->actor, sounds[tick % 3]);
        if (tick % 3 == 0) {
            static const char* lines[4] = { "Navi: Hey! Listen!", "Navi: Look!", "Navi: Watch out!",
                                            "Navi: Hello!" };
            Cursed::Notify(lines[(tick / 3) % 4]);
        }
    });
}

} // namespace

static RegisterShipInitFunc diceNaviSpamInit(Register);
