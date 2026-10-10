#include "CursedScript_Spawn.h"

#include <chrono>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include "CursedScript_Effects.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
// not declared in functions.h
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
}

namespace Cursed {

namespace {

struct PendingObject {
    int bankIndex;
    int framesLeft;
    std::function<void(bool)> done;
};

std::vector<PendingObject> sPendingObjects;

using Clock = std::chrono::steady_clock;

struct Tracked {
    Actor* actor;
    s16 room;
    bool hasDeadline;
    Clock::time_point deadline;
    std::string label; // the effect that spawned it
    int instance;      // which trigger of that effect
    bool hasShowDeadline = false;
    Clock::time_point showDeadline;
    bool showInfinite = false;
};

std::vector<Tracked> sTracked;

void Track(Actor* actor) {
    if (actor == nullptr) {
        return;
    }
    // Our own rules decide when it goes away (room change or timer), not the engine's room unloading
    sTracked.push_back({ actor, gPlayState->roomCtx.curRoom.num, false, Clock::time_point{}, CurrentEffectName(), CurrentInstance() });
    actor->room = -1;
}

} // namespace

void GetOriginPoint(float& x, float& y, float& z) {
    const Origin origin = GetOrigin();
    if (origin.valid) {
        x = origin.x;
        y = origin.y;
        z = origin.z;
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    x = player->actor.world.pos.x;
    y = player->actor.world.pos.y;
    z = player->actor.world.pos.z;
}

void* SpawnActorAt(int actorId, int params, float x, float y, float z, short rotY, bool ignoreRoomClear) {
    if (gPlayState == nullptr || !GameInteractor::CanSpawnActor()) {
        return nullptr;
    }
    // The engine refuses to spawn enemies in a room that was already cleared
    const s32 room = gPlayState->roomCtx.curRoom.num;
    const bool wasClear = ignoreRoomClear && Flags_GetClear(gPlayState, room);
    if (wasClear) {
        Flags_UnsetClear(gPlayState, room);
    }
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, x, y, z, 0, rotY, 0, params);
    if (wasClear) {
        Flags_SetClear(gPlayState, room);
    }
    Track(actor);
    return actor;
}

void Despawn(void* refActor) {
    Actor* actor = static_cast<Actor*>(refActor);
    if (actor == nullptr || actor->update == nullptr) {
        return; // already dying
    }
    if (gPlayState != nullptr) {
        Vec3f zero = { 0.0f, 0.0f, 0.0f };
        Vec3f pos = actor->world.pos;
        pos.y += 30.0f;
        EffectSsDeadDb_Spawn(gPlayState, &pos, &zero, &zero, 200, 0, 220, 220, 220, 255, 120, 120, 120, 1, 9, true);
        Audio_PlayActorSound2(actor, NA_SE_EN_EXTINCT);
    }
    Actor_Kill(actor);
}

void SetLifetimeSeconds(void* actor, float seconds) {
    for (Tracked& t : sTracked) {
        if (t.actor == actor) {
            t.hasDeadline = true;
            t.deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
            return;
        }
    }
}

void ShowTimerOnly(void* actor, float seconds) {
    for (Tracked& t : sTracked) {
        if (t.actor == actor) {
            t.hasShowDeadline = true;
            t.showDeadline =
                Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
            return;
        }
    }
}

void ShowInfinite(void* actor, const char* label) {
    for (Tracked& t : sTracked) {
        if (t.actor == actor) {
            t.showInfinite = true;
            t.label = label;
            return;
        }
    }
}

std::vector<SpawnTimer> GetSpawnTimers() {
    struct Group {
        int instance;
        SpawnTimer timer;
    };
    std::vector<Group> groups;
    if (gPlayState == nullptr) {
        return {};
    }
    const auto now = Clock::now();
    const s16 room = gPlayState->roomCtx.curRoom.num;
    for (const Tracked& t : sTracked) {
        if (t.actor->update == nullptr || t.room != room) {
            continue; // leaving, or left behind in another room
        }
        float left = 0.0f;
        bool infinite = false;
        if (t.hasDeadline) {
            left = std::chrono::duration<float>(t.deadline - now).count();
        } else if (t.hasShowDeadline) {
            left = std::chrono::duration<float>(t.showDeadline - now).count();
        } else if (t.showInfinite) {
            infinite = true;
        } else {
            continue;
        }
        if (!infinite && left <= 0.0f) {
            continue;
        }
        bool found = false;
        for (Group& g : groups) {
            if (g.instance == t.instance && g.timer.infinite == infinite && g.timer.label == t.label) {
                if (left > g.timer.seconds) {
                    g.timer.seconds = left;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            groups.push_back({ t.instance, { t.label, left, infinite } });
        }
    }
    std::vector<SpawnTimer> result;
    for (const Group& g : groups) {
        result.push_back(g.timer);
    }
    return result;
}

void* SpawnActorAtOrigin(int actorId, int params, float spreadRadius) {
    if (gPlayState == nullptr || !GameInteractor::CanSpawnActor()) {
        return nullptr;
    }

    float x;
    float y;
    float z;
    GetOriginPoint(x, y, z);

    static std::mt19937 rng{ std::random_device{}() };
    if (spreadRadius > 0.0f) {
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> radius(0.0f, spreadRadius);
        const float a = angle(rng);
        const float r = radius(rng);
        x += std::cos(a) * r;
        z += std::sin(a) * r;
    }

    std::uniform_int_distribution<int> rotation(-0x7FFF, 0x7FFF);
    return SpawnActorAt(actorId, params, x, y + 20.0f, z, static_cast<s16>(rotation(rng)));
}

bool SpawnAtOrigin(int actorId, int params, float spreadRadius) {
    return SpawnActorAtOrigin(actorId, params, spreadRadius) != nullptr;
}

void RequestObject(int objectId, std::function<void(bool)> done) {
    if (gPlayState == nullptr) {
        done(false);
        return;
    }
    ObjectContext* objectCtx = &gPlayState->objectCtx;
    s32 index = Object_GetIndex(objectCtx, static_cast<s16>(objectId));
    if (index < 0) {
        if (objectCtx->num >= OBJECT_EXCHANGE_BANK_MAX - 1) {
            done(false);
            return;
        }
        index = Object_Spawn(objectCtx, static_cast<s16>(objectId));
    }
    sPendingObjects.push_back({ index, 120, std::move(done) });
}

namespace {

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (sPendingObjects.empty() || gPlayState == nullptr) {
            return;
        }
        std::vector<std::pair<std::function<void(bool)>, bool>> finished;
        for (auto it = sPendingObjects.begin(); it != sPendingObjects.end();) {
            if (Object_IsLoaded(&gPlayState->objectCtx, it->bankIndex)) {
                finished.emplace_back(std::move(it->done), true);
                it = sPendingObjects.erase(it);
            } else if (--it->framesLeft <= 0) {
                finished.emplace_back(std::move(it->done), false);
                it = sPendingObjects.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& entry : finished) {
            entry.first(entry.second);
        }
    });

    // Despawn rules for everything the effects spawn
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (sTracked.empty() || gPlayState == nullptr) {
            return;
        }
        const auto now = Clock::now();
        const s16 room = gPlayState->roomCtx.curRoom.num;
        for (Tracked& t : sTracked) {
            if (t.actor->update == nullptr) {
                continue; // already dying
            }
            if (t.room != room || (t.hasDeadline && now >= t.deadline)) {
                Despawn(t.actor);
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        for (auto it = sTracked.begin(); it != sTracked.end(); ++it) {
            if (it->actor == refActor) {
                sTracked.erase(it);
                return;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        sPendingObjects.clear();
        sTracked.clear();
    });
}

} // namespace

} // namespace Cursed

static RegisterShipInitFunc cursedSpawnInit(Cursed::Register);
