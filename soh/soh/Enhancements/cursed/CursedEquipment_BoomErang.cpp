// Boom-Erang: while Link's boomerang is in the air, it drops a bomb explosion at its position
// roughly once per second. The explosions hurt enemies and objects, never Link.
#include <string>

#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
extern PlayState* gPlayState;
}

namespace {

constexpr int kExplosionInterval = 5; // frames, the game runs at 20 fps => about 1 second
constexpr uint16_t kBoomerangGetItemText = 0x0035;

int sFlightFrames = 0;

void Explode(const Vec3f& pos) {
    void* spawn = Cursed::SpawnActorAt(ACTOR_EN_BOM, BOMB_BODY, pos.x, pos.y, pos.z, 0, true);
    if (spawn == nullptr) {
        return;
    }
    EnBom* bomb = static_cast<EnBom*>(spawn);
    // Fuse of one frame: the bomb goes off on its next update
    bomb->timer = 1;
    // Player-aligned damage: hits enemies and objects, but not Link
    bomb->explosionCollider.base.atFlags = AT_ON | AT_TYPE_PLAYER;
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorSpawn>([](void* refActor) {
        if (static_cast<Actor*>(refActor)->id == ACTOR_EN_BOOM) {
            sFlightFrames = 0;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool*) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_EN_BOOM) {
            return;
        }
        sFlightFrames++;
        if (sFlightFrames % kExplosionInterval == 0) {
            Explode(actor->world.pos);
        }
    });

    // "Boom-Erang" in the item get text
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(
        kBoomerangGetItemText, [](uint16_t* textId, bool* loadFromMessageTable) {
            CustomMessage msg(std::string("You got the %rBoom-Erang%w!&Lorem ipsum dolor sit amet, it goes boom."), TEXTBOX_TYPE_BLUE);
            msg.AutoFormat();
            *loadFromMessageTable = false;
            msg.LoadIntoFont();
        });
}

} // namespace

static RegisterShipInitFunc boomErangInit(Register);
