#include <vector>

#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Magic_Fire/z_magic_fire.h"
extern PlayState* gPlayState;
}

namespace {

constexpr float kRing = 110.0f;  // distance of the cardinal fires from the centre
constexpr float kBurnSeconds = 5.0f; // about how long a fire burns before it fades out (for the timer list)
constexpr int kStepFrames = 4;   // delay between the three waves

struct Pending {
    int delay;
    float x, y, z;
};

struct Fire {
    Actor* actor;
    float x, y, z;
    bool center;
    int updates;
};

std::vector<Pending> sPending;
std::vector<Fire> sFires;

void SpawnFire(const Pending& p, bool center) {
    Actor* actor = static_cast<Actor*>(Cursed::SpawnActorAt(ACTOR_MAGIC_FIRE, 0, p.x, p.y, p.z, 0));
    if (actor != nullptr) {
        Cursed::ShowTimerOnly(actor, kBurnSeconds);
        sFires.push_back({ actor, p.x, p.y, p.z, center, 0 });
    }
}

void StartBurst() {
    float x, y, z;
    Cursed::GetOriginPoint(x, y, z);

    sPending.clear();
    // wave 0: centre, spawned right away below
    // wave 1: N / S / E / W
    const float cardinal[4][2] = { { kRing, 0 }, { -kRing, 0 }, { 0, kRing }, { 0, -kRing } };
    for (auto& c : cardinal) {
        sPending.push_back({ kStepFrames, x + c[0], y, z + c[1] });
    }
    // wave 2: diagonals
    const float diag[4][2] = { { kRing, kRing }, { kRing, -kRing }, { -kRing, kRing }, { -kRing, -kRing } };
    for (auto& d : diag) {
        sPending.push_back({ kStepFrames * 2, x + d[0], y, z + d[1] });
    }

    const size_t before = sFires.size();
    SpawnFire({ 0, x, y, z }, true);
    if (sFires.size() == before) {
        sPending.clear();
        Cursed::Notify("Cannot spawn right now");
    }
}

Fire* FindFire(Actor* actor) {
    for (auto& fire : sFires) {
        if (fire.actor == actor) {
            return &fire;
        }
    }
    return nullptr;
}

void Register() {
    // Run the vanilla update ourselves, then pin the fire to its target point
    // (the vanilla code copies Link's position every frame)
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>(
        [](void* refActor, bool* should) {
            Actor* actor = static_cast<Actor*>(refActor);
            if (actor->id != ACTOR_MAGIC_FIRE || sFires.empty()) {
                return;
            }
            Fire* fire = FindFire(actor);
            if (fire == nullptr) {
                return;
            }
            *should = false;
            if (actor->update == nullptr) {
                return;
            }

            actor->update(actor, gPlayState);
            MagicFire* magic = reinterpret_cast<MagicFire*>(actor);

            // skip the 20 frame wind-up that waits for Link's cast
            if (fire->updates == 0) {
                magic->actionTimer = 0;
            }
            fire->updates++;

            actor->world.pos.x = fire->x;
            actor->world.pos.y = fire->y;
            actor->world.pos.z = fire->z;
            magic->collider.dim.pos.x = static_cast<s16>(fire->x);
            magic->collider.dim.pos.y = static_cast<s16>(fire->y);
            magic->collider.dim.pos.z = static_cast<s16>(fire->z);

            // only the centre fire may tint the screen, otherwise nine overlays turn it solid red
            if (!fire->center) {
                magic->screenTintIntensity = 0.0f;
            }
        });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        for (auto it = sFires.begin(); it != sFires.end(); ++it) {
            if (it->actor == actor) {
                sFires.erase(it);
                return;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (sPending.empty()) {
            return;
        }
        std::vector<Pending> due;
        for (auto it = sPending.begin(); it != sPending.end();) {
            if (--it->delay <= 0) {
                due.push_back(*it);
                it = sPending.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& p : due) {
            SpawnFire(p, false);
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        sPending.clear();
        sFires.clear();
    });

    Cursed::RegisterEffect({
        "dice_dins_fire",
        "Din's Fire",
        0.0f,
        [] { StartBurst(); },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc dinsFireInit(Register);
