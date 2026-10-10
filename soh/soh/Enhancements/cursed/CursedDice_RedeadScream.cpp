#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
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

Actor* sRedead = nullptr;
int sFramesLeft = 0;

void SpawnRedead() {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    const s16 facing = player->actor.shape.rot.y;
    const float x = player->actor.world.pos.x + Math_SinS(facing) * 80.0f;
    const float z = player->actor.world.pos.z + Math_CosS(facing) * 80.0f;
    const float y = player->actor.world.pos.y;
    Actor* actor =
        static_cast<Actor*>(Cursed::SpawnActorAt(ACTOR_EN_RD, 0, x, y, z, static_cast<s16>(facing + 0x8000), true));
    if (actor == nullptr) {
        Cursed::Notify("Cannot spawn right now");
        return;
    }
    sRedead = actor;
    sFramesLeft = 30;
    Cursed::ShowInfinite(actor, "Redead"); // listed for as long as it lives
}

void Register() {
    Cursed::RegisterEffect({
        "dice_redead_scream",
        "Redead Scream",
        0.0f,
        [] {
            Cursed::RequestObject(OBJECT_RD, [](bool ok) {
                if (!ok) {
                    Cursed::Notify("Redead could not be loaded");
                    return;
                }
                SpawnRedead();
            });
        },
        nullptr,
    });

    // The Redead only screams when Link "walks" near it (footstep flag); fake that for its first frames
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool*) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (sRedead == nullptr || actor != sRedead) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        if (sFramesLeft > 0 && player->actor.freezeTimer == 0) {
            player->stateFlags2 |= PLAYER_STATE2_FOOTSTEP;
            sFramesLeft--;
        } else {
            sFramesLeft = 0;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        if (refActor == sRedead) {
            sRedead = nullptr;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sRedead = nullptr; });
}

} // namespace

static RegisterShipInitFunc diceRedeadScreamInit(Register);
