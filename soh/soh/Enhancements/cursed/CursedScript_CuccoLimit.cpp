#include "CursedScript_Spawn.h"
#include <chrono>
#include <vector>

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

// An angry cucco swarm (small and giant) never lasts longer than this.
// Killing the swarm alone is not enough: the angry "mother" cucco replaces every attack cucco that
// disappears, so after the time is up she is removed together with her swarm.
constexpr float kMaxSeconds = 15.0f;

using Clock = std::chrono::steady_clock;

struct Child {
    Actor* actor;
    Clock::time_point spawned;
};

struct Mother {
    Actor* actor;
    Clock::time_point swarmStart;
};

std::vector<Child> sChildren;
std::vector<Mother> sMothers;

bool Expired(Clock::time_point start, Clock::time_point now) {
    return std::chrono::duration<float>(now - start).count() >= kMaxSeconds;
}

Mother* FindMother(Actor* actor) {
    for (Mother& mother : sMothers) {
        if (mother.actor == actor) {
            return &mother;
        }
    }
    return nullptr;
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorSpawn>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id == ACTOR_EN_ATTACK_NIW) {
            sChildren.push_back({ actor, Clock::now() });
        }
    });

    // The parent is only set after the spawn, so the swarm is matched up here, at the end of the frame
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (sChildren.empty()) {
            return;
        }
        const auto now = Clock::now();
        for (Child& child : sChildren) {
            Actor* mother = child.actor->parent;
            if (mother != nullptr && mother->id == ACTOR_EN_NIW) {
                if (FindMother(mother) == nullptr) {
                    sMothers.push_back({ mother, child.spawned });
                }
            } else if (Expired(child.spawned, now) && child.actor->update != nullptr) {
                Cursed::Despawn(child.actor);
            }
        }
        for (Mother& mother : sMothers) {
            if (Expired(mother.swarmStart, now)) {
                for (Child& child : sChildren) {
                    if (child.actor->parent == mother.actor && child.actor->update != nullptr) {
                        Cursed::Despawn(child.actor);
                    }
                }
                if (mother.actor->update != nullptr) {
                    Cursed::Despawn(mother.actor);
                }
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        for (auto it = sChildren.begin(); it != sChildren.end(); ++it) {
            if (it->actor == refActor) {
                sChildren.erase(it);
                break;
            }
        }
        for (auto it = sMothers.begin(); it != sMothers.end(); ++it) {
            if (it->actor == refActor) {
                sMothers.erase(it);
                break;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        sChildren.clear();
        sMothers.clear();
    });
}

} // namespace

static RegisterShipInitFunc cuccoLimitInit(Register);
