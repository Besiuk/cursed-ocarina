// Floating Bean Leaf: a magic bean platform appears at the impact point and drifts around for 30 seconds
// in completely random directions on all three axes. Link can ride it. It only turns away from walls,
// floors and ceilings.
#include <cmath>
#include <memory>
#include <random>
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
#include "overlays/actors/ovl_Obj_Bean/z_obj_bean.h"
extern PlayState* gPlayState;
// Not in a header, but not static in the actor either
void ObjBean_SetupFly(ObjBean* bean);
void ObjBean_UpdatePosition(ObjBean* bean);
}

namespace {

constexpr float kLifetimeSeconds = 30.0f;
constexpr float kSpeed = 3.5f;          // units per frame
constexpr float kSteering = 0.08f;      // how fast the velocity follows the wanted direction
constexpr float kClearance = 90.0f;     // keep this much distance to walls, floor and ceiling (room for Link on top)
constexpr float kMaxRadius = 1200.0f;   // never drift further than this from the spawn point
constexpr float kMaxRise = 500.0f;
constexpr float kMaxDrop = 60.0f;
constexpr int kMinTurnFrames = 30;
constexpr int kMaxTurnFrames = 80;
constexpr uint32_t kSwitchFlag = 0x3F;  // borrowed while spawning, the actor wants it set

std::mt19937& Rng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

float Rand(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(Rng());
}

struct Leaf {
    Actor* actor;
    Vec3f pos; // the real position; the actor's own position adds the leaf wobble
    Vec3f vel;
    Vec3f dir; // unit vector, where it wants to go
    Vec3f home;
    int turnTimer;
};

std::vector<std::unique_ptr<Leaf>> sLeaves;

Leaf* FindLeaf(Actor* actor) {
    for (auto& l : sLeaves) {
        if (l->actor == actor) {
            return l.get();
        }
    }
    return nullptr;
}

void PickDirection(Leaf& leaf) {
    const float yaw = Rand(0.0f, 6.2831853f);
    const float up = Rand(-0.8f, 0.8f); // sine of the pitch
    const float flat = std::sqrt(1.0f - up * up);
    leaf.dir = { std::sin(yaw) * flat, up, std::cos(yaw) * flat };
    leaf.turnTimer = static_cast<int>(Rand(kMinTurnFrames, kMaxTurnFrames));
}

// Is there level geometry within `length` units from `from` along the unit vector `dir`?
bool Blocked(PlayState* play, Actor* self, const Vec3f& from, const Vec3f& dir, float length) {
    Vec3f a = from;
    Vec3f b = { from.x + dir.x * length, from.y + dir.y * length, from.z + dir.z * length };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    return BgCheck_EntityLineTest2(&play->colCtx, &a, &b, &hit, &poly, true, true, true, true, &bgId, self) != 0;
}

void UpdateLeaf(Leaf& leaf) {
    PlayState* play = gPlayState;
    ObjBean* bean = reinterpret_cast<ObjBean*>(leaf.actor);

    if (--leaf.turnTimer <= 0) {
        PickDirection(leaf);
    }

    // Stay inside the allowed volume around the spawn point
    const float dx = leaf.pos.x - leaf.home.x;
    const float dz = leaf.pos.z - leaf.home.z;
    if (dx * dx + dz * dz > kMaxRadius * kMaxRadius || leaf.pos.y > leaf.home.y + kMaxRise ||
        leaf.pos.y < leaf.home.y - kMaxDrop) {
        Vec3f back = { leaf.home.x - leaf.pos.x, leaf.home.y - leaf.pos.y, leaf.home.z - leaf.pos.z };
        const float len = std::sqrt(back.x * back.x + back.y * back.y + back.z * back.z);
        if (len > 1.0f) {
            leaf.dir = { back.x / len, back.y / len, back.z / len };
            leaf.turnTimer = kMinTurnFrames;
        }
    }

    // Ease the velocity toward the wanted direction
    leaf.vel.x += (leaf.dir.x * kSpeed - leaf.vel.x) * kSteering;
    leaf.vel.y += (leaf.dir.y * kSpeed - leaf.vel.y) * kSteering;
    leaf.vel.z += (leaf.dir.z * kSpeed - leaf.vel.z) * kSteering;

    const float speed = std::sqrt(leaf.vel.x * leaf.vel.x + leaf.vel.y * leaf.vel.y + leaf.vel.z * leaf.vel.z);
    if (speed > 0.01f) {
        const Vec3f heading = { leaf.vel.x / speed, leaf.vel.y / speed, leaf.vel.z / speed };
        // Two probes: at platform level and at about Link's head height
        Vec3f upper = leaf.pos;
        upper.y += 60.0f;
        if (Blocked(play, leaf.actor, leaf.pos, heading, speed + kClearance) ||
            Blocked(play, leaf.actor, upper, heading, speed + kClearance)) {
            // Turn around: new random direction that points against the blocked one
            for (int tries = 0; tries < 8; tries++) {
                PickDirection(leaf);
                if (leaf.dir.x * heading.x + leaf.dir.y * heading.y + leaf.dir.z * heading.z < 0.0f) {
                    break;
                }
            }
            leaf.vel = { 0.0f, 0.0f, 0.0f };
        } else {
            leaf.pos.x += leaf.vel.x;
            leaf.pos.y += leaf.vel.y;
            leaf.pos.z += leaf.vel.z;
        }
    }

    // The leaf's own wobble animation, then place the actor (the platform collision follows it)
    ObjBean_UpdatePosition(bean);
    leaf.actor->world.pos.x = leaf.pos.x + bean->posOffsetX;
    leaf.actor->world.pos.y = leaf.pos.y;
    leaf.actor->world.pos.z = leaf.pos.z + bean->posOffsetZ;

    if (DynaPolyActor_IsPlayerOnTop(&bean->dyna)) {
        Audio_PlayActorSound2(leaf.actor, NA_SE_PL_PLANT_MOVE - SFX_FLAG);
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_OBJ_BEAN || sLeaves.empty()) {
            return;
        }
        Leaf* leaf = FindLeaf(actor);
        if (leaf == nullptr) {
            return;
        }
        *should = false;
        UpdateLeaf(*leaf);
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        for (auto it = sLeaves.begin(); it != sLeaves.end(); ++it) {
            if ((*it)->actor == actor) {
                sLeaves.erase(it);
                return;
            }
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sLeaves.clear(); });

    Cursed::RegisterEffect({
        "dice_floating_bean_leaf",
        "Floating Bean Leaf",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            Cursed::RequestObject(OBJECT_MAMENOKI, [x, y, z](bool ok) {
                if (!ok) {
                    Cursed::Notify("The bean leaf could not be loaded");
                    return;
                }
                PlayState* play = gPlayState;

                // The platform only exists for the adult, needs its switch set and a path of three points.
                // Provide all three just for the moment of spawning, then put everything back.
                static Vec3s sPoints[3];
                static Path sPath;
                for (Vec3s& point : sPoints) {
                    point.x = static_cast<s16>(x);
                    point.y = static_cast<s16>(y + 20.0f);
                    point.z = static_cast<s16>(z);
                }
                sPath.count = 3;
                sPath.points = sPoints;

                Path* oldPaths = play->setupPathList;
                const auto oldAge = gSaveContext.linkAge;
                const bool oldSwitch = Flags_GetSwitch(play, kSwitchFlag) != 0;
                play->setupPathList = &sPath;
                gSaveContext.linkAge = LINK_AGE_ADULT;
                Flags_SetSwitch(play, kSwitchFlag);

                // Switch in the low 6 bits, path index 0 in bits 8-12
                void* spawn = Cursed::SpawnActorAt(ACTOR_OBJ_BEAN, kSwitchFlag, x, y + 20.0f, z, 0);

                if (!oldSwitch) {
                    Flags_UnsetSwitch(play, kSwitchFlag);
                }
                gSaveContext.linkAge = oldAge;
                play->setupPathList = oldPaths;

                if (spawn == nullptr || static_cast<Actor*>(spawn)->update == nullptr) {
                    Cursed::Notify("The bean leaf cannot appear here");
                    return;
                }
                Actor* actor = static_cast<Actor*>(spawn);
                ObjBean* bean = static_cast<ObjBean*>(spawn);
                ObjBean_SetupFly(bean); // draw as the flying platform, never stop updating
                Cursed::SetLifetimeSeconds(spawn, kLifetimeSeconds);

                auto leaf = std::make_unique<Leaf>();
                leaf->actor = actor;
                leaf->pos = { x, y + 20.0f, z };
                leaf->home = leaf->pos;
                leaf->vel = { 0.0f, 0.0f, 0.0f };
                PickDirection(*leaf);
                sLeaves.push_back(std::move(leaf));
            });
        },
        nullptr,
    });
}

} // namespace

static RegisterShipInitFunc diceFloatingBeanLeafInit(Register);
