// Ganondorf: appears floating above the impact point, laughs, charges up and slams down on the floor.
// Three shockwave rings run outwards from the impact. A ring hurts Link (unless he jumps over it) and
// every enemy it passes. After the last ring he laughs once more and vanishes in a puff of smoke.
//
// The model is drawn by a small carrier actor (the unused vase, which does nothing on its own): we spawn it,
// replace its draw function with the Ganondorf skeleton and drive everything from the actor update hook.
// He has no hitbox, cannot be targeted and cannot be talked to.
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "CursedScript_Ally.h"
#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h" // OPEN_DISPS needs its C linkage declarations
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/object_ganon/object_ganon.h"
#include "objects/object_ganon_anime1/object_ganon_anime1.h"
#include "objects/object_ganon_anime2/object_ganon_anime2.h"
extern PlayState* gPlayState;
}

// extern "C" on purpose: OPEN_DISPS declares helper functions inside the function body, and they only link
// against the game's C functions when the surrounding function has C linkage.
extern "C" void CursedGanon_Draw(Actor* actor, PlayState* play);

namespace {

// Timing (the game runs 20 updates per second)
constexpr int kAppearFrames = 30;
constexpr int kLaughFrames = 50;
constexpr int kChargeFrames = 16;
constexpr int kLeaveFrames = 30;

constexpr float kHoverHeight = 250.0f;   // above the impact point, lowered under low ceilings
constexpr float kMinHover = 110.0f;
constexpr float kFootOffset = 60.0f;     // Ganondorf's origin is this high above the floor when he stands
constexpr float kScale = 0.01f;

// Shockwaves
constexpr int kRingCount = 3;
constexpr int kRingGapFrames = 8;
constexpr float kRingSpeed = 16.0f;      // units per frame
constexpr float kRingMaxRadius = 480.0f;
constexpr float kRingBand = 45.0f;       // how thick a ring is for hitting things
constexpr float kRingHeight = 50.0f;     // Link must be lower than this above the floor to be hit
constexpr u32 kLinkDamage = 0x20;        // 2 hearts (a heart is 0x10)
constexpr int kEnemyDamage = 8;
constexpr int kEnemyDamageTag = 0x00000329;

// Animations, easy to swap: laugh = raised hand, hover = the boss fight float
const char* const kAnimHover = gGanondorfFloatAnim;
const char* const kAnimLaugh = gGanondorfRaiseHandLoopAnim;
const char* const kAnimCharge = gGanondorfPoundAnim;
const char* const kAnimSlam = gGanondorfPoundEndAnim;

enum Phase { PHASE_APPEAR, PHASE_LAUGH, PHASE_CHARGE, PHASE_SLAM, PHASE_WAVES, PHASE_LEAVE };

struct HitBox {
    ColliderCylinder collider;
    int frames;
};

struct Ring {
    int delay;
    float radius;
    bool started;
    bool finished;
    bool linkHit;
    std::vector<Actor*> hit;
};

struct Ganon {
    Actor* actor;
    SkelAnime skel;
    Phase phase;
    int timer;
    float hover;  // height of the hover point (world y)
    float floorY;
    float velY;
    Ring rings[kRingCount];
    std::vector<std::unique_ptr<HitBox>> boxes;
};

std::vector<std::unique_ptr<Ganon>> sGanons;

// Damages enemies and objects, never Link
ColliderCylinderInit sBoxInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { kEnemyDamageTag, 0x00, kEnemyDamage }, { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_NORMAL, BUMP_NONE, OCELEM_NONE },
    { 40, 80, 0, { 0, 0, 0 } },
};

AnimationHeader* Anim(const char* name) {
    return reinterpret_cast<AnimationHeader*>(const_cast<char*>(name));
}

Ganon* Find(Actor* actor) {
    for (auto& g : sGanons) {
        if (g->actor == actor) {
            return g.get();
        }
    }
    return nullptr;
}

void Puff(const Vec3f& at) {
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    Vec3f pos = at;
    EffectSsDeadDb_Spawn(gPlayState, &pos, &zero, &zero, 300, 0, 220, 220, 220, 255, 120, 120, 120, 1, 9, true);
}

void FaceLink(Ganon& g) {
    Player* player = GET_PLAYER(gPlayState);
    const s16 yaw = Math_Vec3f_Yaw(&g.actor->world.pos, &player->actor.world.pos);
    Math_SmoothStepToS(&g.actor->shape.rot.y, yaw, 4, 0x1000, 0x100);
    g.actor->world.rot.y = g.actor->shape.rot.y;
}

float FloorBelow(float x, float y, float z) {
    Vec3f pos = { x, y, z };
    CollisionPoly* poly = nullptr;
    const float floor = BgCheck_EntityRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
    return floor > BGCHECK_Y_MIN ? floor : y - kHoverHeight;
}

// A short-lived attack box on one enemy; the game then handles damage, reactions and death as usual
void HitEnemy(Ganon& g, Actor* enemy) {
    auto box = std::make_unique<HitBox>();
    Collider_InitCylinder(gPlayState, &box->collider);
    Collider_SetCylinder(gPlayState, &box->collider, g.actor, &sBoxInit);
    box->collider.dim.pos.x = static_cast<s16>(enemy->world.pos.x);
    box->collider.dim.pos.y = static_cast<s16>(enemy->world.pos.y);
    box->collider.dim.pos.z = static_cast<s16>(enemy->world.pos.z);
    box->collider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
    box->frames = 2;
    g.boxes.push_back(std::move(box));
}

void UpdateBoxes(Ganon& g) {
    for (auto it = g.boxes.begin(); it != g.boxes.end();) {
        HitBox& box = **it;
        if (box.frames-- > 0) {
            CollisionCheck_SetAT(gPlayState, &gPlayState->colChkCtx, &box.collider.base);
            ++it;
        } else {
            Collider_DestroyCylinder(gPlayState, &box.collider);
            it = g.boxes.erase(it);
        }
    }
}

void SpawnRingVisual(const Ganon& g) {
    static Color_RGBA8 prim = { 255, 255, 220, 255 };
    static Color_RGBA8 env = { 255, 200, 80, 0 };
    Vec3f pos = { g.actor->world.pos.x, g.floorY, g.actor->world.pos.z };
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    // 100 start size, then a constant growth; tuned to roughly follow kRingSpeed
    EffectSsBlast_Spawn(gPlayState, &pos, &zero, &zero, &prim, &env, 100, static_cast<s16>(kRingSpeed * 4.0f), 0,
                        static_cast<s16>(kRingMaxRadius / kRingSpeed));
}

void Impact(Ganon& g) {
    PlayState* play = gPlayState;
    Actor* actor = g.actor;
    Actor_RequestQuakeAndRumble(actor, play, 10, 20);
    Audio_PlayActorSound2(actor, NA_SE_EN_GANON_HIT_GND_IMP);
    Vec3f at = { actor->world.pos.x, g.floorY + 10.0f, actor->world.pos.z };
    Puff(at);
    Animation_MorphToPlayOnce(&g.skel, Anim(kAnimSlam), 0.0f);
    for (int i = 0; i < kRingCount; i++) {
        g.rings[i] = { i * kRingGapFrames, 0.0f, false, false, false, {} };
    }
}

void UpdateRing(Ganon& g, Ring& ring) {
    PlayState* play = gPlayState;
    Player* player = GET_PLAYER(play);
    if (ring.finished) {
        return;
    }
    if (!ring.started) {
        if (--ring.delay > 0) {
            return;
        }
        ring.started = true;
        SpawnRingVisual(g);
    }
    ring.radius += kRingSpeed;
    if (ring.radius > kRingMaxRadius) {
        ring.finished = true;
        return;
    }

    const float cx = g.actor->world.pos.x;
    const float cz = g.actor->world.pos.z;

    // Link: only while on or near the floor, so jumping over a ring works
    if (!ring.linkHit && player->invincibilityTimer == 0) {
        const float dx = player->actor.world.pos.x - cx;
        const float dz = player->actor.world.pos.z - cz;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (std::fabs(dist - ring.radius) < kRingBand && player->actor.world.pos.y - g.floorY < kRingHeight) {
            const s16 away = Math_Vec3f_Yaw(&g.actor->world.pos, &player->actor.world.pos);
            Actor_SetPlayerKnockbackLarge(play, g.actor, 8.0f, away, 6.0f, kLinkDamage);
            ring.linkHit = true;
        }
    }

    // Enemies: each one once per ring
    for (Actor* enemy = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; enemy != nullptr; enemy = enemy->next) {
        if (enemy->update == nullptr || Cursed::IsAlly(enemy)) {
            continue;
        }
        const float dx = enemy->world.pos.x - cx;
        const float dz = enemy->world.pos.z - cz;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (std::fabs(dist - ring.radius) >= kRingBand || enemy->world.pos.y - g.floorY >= kRingHeight * 2.0f) {
            continue;
        }
        bool already = false;
        for (Actor* h : ring.hit) {
            if (h == enemy) {
                already = true;
                break;
            }
        }
        if (!already) {
            ring.hit.push_back(enemy);
            HitEnemy(g, enemy);
        }
    }
}

void Update(Ganon& g) {
    Actor* actor = g.actor;
    g.timer++;
    SkelAnime_Update(&g.skel);
    UpdateBoxes(g);

    switch (g.phase) {
        case PHASE_APPEAR: {
            FaceLink(g);
            Actor_SetScale(actor, kScale * std::fmin(1.0f, static_cast<float>(g.timer) / 15.0f));
            if (g.timer >= kAppearFrames) {
                g.phase = PHASE_LAUGH;
                g.timer = 0;
            }
            break;
        }
        case PHASE_LAUGH: {
            FaceLink(g);
            if (g.timer == 1) {
                Animation_MorphToLoop(&g.skel, Anim(kAnimLaugh), -8.0f);
                Cursed::Notify("Ganondorf: Lorem ipsum dolor sit amet, little hero!");
            }
            if (g.timer == 4 || g.timer == 30) {
                Audio_PlayActorSound2(actor, NA_SE_EN_GANON_LAUGH);
            }
            actor->world.pos.y = g.hover + std::sin(g.timer * 0.35f) * 6.0f;
            if (g.timer >= kLaughFrames) {
                g.phase = PHASE_CHARGE;
                g.timer = 0;
                Animation_MorphToPlayOnce(&g.skel, Anim(kAnimCharge), -4.0f);
            }
            break;
        }
        case PHASE_CHARGE: {
            FaceLink(g);
            Math_ApproachF(&actor->world.pos.y, g.hover + 70.0f, 0.2f, 8.0f);
            if (g.timer >= kChargeFrames) {
                g.phase = PHASE_SLAM;
                g.timer = 0;
                g.velY = 0.0f;
                Audio_PlayActorSound2(actor, NA_SE_EN_GANON_HIT_GND);
            }
            break;
        }
        case PHASE_SLAM: {
            Math_ApproachF(&g.velY, -50.0f, 1.0f, 10.0f);
            actor->world.pos.y += g.velY;
            if (actor->world.pos.y <= g.floorY + kFootOffset) {
                actor->world.pos.y = g.floorY + kFootOffset;
                g.phase = PHASE_WAVES;
                g.timer = 0;
                Impact(g);
            }
            break;
        }
        case PHASE_WAVES: {
            bool allDone = true;
            for (Ring& ring : g.rings) {
                UpdateRing(g, ring);
                allDone = allDone && ring.finished;
            }
            if (allDone) {
                g.phase = PHASE_LEAVE;
                g.timer = 0;
                Animation_MorphToLoop(&g.skel, Anim(kAnimHover), -8.0f);
                Audio_PlayActorSound2(actor, NA_SE_EN_GANON_LAUGH);
            }
            break;
        }
        case PHASE_LEAVE: {
            FaceLink(g);
            Math_ApproachF(&actor->world.pos.y, g.hover, 0.15f, 8.0f);
            if (g.timer >= kLeaveFrames) {
                Cursed::Despawn(actor);
            }
            break;
        }
    }
    actor->focus.pos = actor->world.pos;
}

// Loads the vase (needed to spawn the carrier) and the Ganondorf objects one after the other.
// Only the vase is mandatory.
void LoadObjects(std::vector<int> ids, size_t index, std::function<void(bool)> done) {
    if (index >= ids.size()) {
        done(true);
        return;
    }
    Cursed::RequestObject(ids[index], [ids, index, done](bool ok) {
        if (!ok && index == 0) {
            done(false);
            return;
        }
        LoadObjects(ids, index + 1, done);
    });
}

void Start(float x, float y, float z) {
    PlayState* play = gPlayState;
    Player* player = GET_PLAYER(play);

    // Hover point: 250 up, lower if the ceiling is in the way
    float hover = y + kHoverHeight;
    Vec3f from = { x, y + 20.0f, z };
    Vec3f to = { x, hover + 80.0f, z };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, false, true, false, true, &bgId)) {
        hover = std::fmax(y + kMinHover, hit.y - 90.0f);
    }

    const s16 yaw = Math_Vec3f_Yaw(&from, &player->actor.world.pos);
    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_VASE, 0, x, hover, z, yaw);
    if (spawn == nullptr || static_cast<Actor*>(spawn)->update == nullptr) {
        Cursed::Notify("Ganondorf cannot appear here");
        return;
    }
    Actor* actor = static_cast<Actor*>(spawn);

    auto g = std::make_unique<Ganon>();
    g->actor = actor;
    g->phase = PHASE_APPEAR;
    g->timer = 0;
    g->hover = hover;
    g->floorY = FloorBelow(x, y + 40.0f, z);
    g->velY = 0.0f;
    SkelAnime_InitFlex(play, &g->skel,
                       reinterpret_cast<FlexSkeletonHeader*>(const_cast<char*>(gGanondorfSkel)), nullptr, nullptr,
                       nullptr, 0);
    Animation_PlayLoop(&g->skel, Anim(kAnimHover));

    actor->draw = CursedGanon_Draw;
    actor->shape.shadowDraw = nullptr;
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    actor->uncullZoneForward = 1500.0f;
    actor->uncullZoneScale = 1000.0f;
    actor->uncullZoneDownward = 1000.0f;
    Actor_SetScale(actor, 0.0001f);

    Vec3f at = actor->world.pos;
    Puff(at);

    // Show the remaining time in the timer list; the sequence ends by itself
    const int total = kAppearFrames + kLaughFrames + kChargeFrames + 12 +
                      static_cast<int>(kRingMaxRadius / kRingSpeed) + (kRingCount - 1) * kRingGapFrames +
                      kLeaveFrames;
    Cursed::ShowTimerOnly(spawn, static_cast<float>(total) / 20.0f);
    sGanons.push_back(std::move(g));
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_VASE || sGanons.empty()) {
            return;
        }
        Ganon* g = Find(actor);
        if (g == nullptr) {
            return;
        }
        *should = false;
        Update(*g);
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        for (auto it = sGanons.begin(); it != sGanons.end(); ++it) {
            if ((*it)->actor == actor) {
                if (gPlayState != nullptr) {
                    for (auto& box : (*it)->boxes) {
                        Collider_DestroyCylinder(gPlayState, &box->collider);
                    }
                    SkelAnime_Free(&(*it)->skel, gPlayState);
                }
                sGanons.erase(it);
                return;
            }
        }
    });

    // The actors are gone with the scene; their skeletons are freed with the scene's arena
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sGanons.clear(); });

    Cursed::RegisterEffect({
        "dice_ganondorf",
        "Ganondorf",
        0.0f,
        [] {
            float x, y, z;
            Cursed::GetOriginPoint(x, y, z);
            LoadObjects({ OBJECT_VASE, OBJECT_GANON, OBJECT_GANON_ANIME1, OBJECT_GANON_ANIME2 }, 0,
                        [x, y, z](bool ok) {
                            if (!ok) {
                                Cursed::Notify("Ganondorf could not be loaded");
                                return;
                            }
                            Start(x, y, z);
                        });
        },
        nullptr,
    });
}

} // namespace

extern "C" void CursedGanon_Draw(Actor* actor, PlayState* play) {
    Ganon* g = Find(actor);
    if (g == nullptr) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08, reinterpret_cast<uintptr_t>(gGanondorfNormalEyeTex));
    SkelAnime_DrawSkeletonOpa(play, &g->skel, nullptr, nullptr, actor);
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc diceGanondorfInit(Register);
