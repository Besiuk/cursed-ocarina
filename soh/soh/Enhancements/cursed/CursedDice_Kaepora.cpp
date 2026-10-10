#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "CursedScript_Effects.h"
#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Kaepora Gaebora on a signpost. The conversation is forced, but the world keeps running:
// there is no "talking" state (which would freeze every actor). Instead Link's input is blocked,
// he cannot be hurt, and enemies creep towards him without attacking.

namespace {

// ---- tuning ----
constexpr float kSpawnDistance = 140.0f;  // in front of Link
constexpr float kOwlHeight = 62.0f;       // owl sits this far above the sign's base
constexpr float kCreepSpeed = 1.0f;       // enemy units per update while the conversation runs
constexpr float kCreepStopDistance = 50.0f;
constexpr int kNoOnlyRounds = 2;          // rounds 1 and 2 only offer "No", round 3 contains the "Yes"

constexpr uint16_t kTextOwl = 0x7F00;     // conversation, rebuilt on every open
constexpr uint16_t kTextSign = 0x7F10;    // sign text

// "No" in 30 languages, Latin transliteration because the game font has no other scripts
const std::array<const char*, 30> kNoWords = {
    "Nein",  "No",    "Non",    "Nee",   "Nej",  "Nei",   "Ei",    "Nie",   "Ne",    "Nu",
    "Nem",   "Hayir", "Ochi",   "Nyet",  "Iie",  "Bu",    "Aniyo", "Laa",   "Lo",    "Nahin",
    "Tidak", "Hindi", "Khong",  "Mai",   "Hapana", "Nil", "Nao",   "Jo",    "Ez",    "Na",
};

enum class Phase { Idle, Loading, Dialog, Done };

Phase sPhase = Phase::Idle;
Actor* sOwl = nullptr;
Actor* sSign = nullptr;
bool sLocked = false;          // Link is locked, enemies creep
int sRound = 0;                // 0-based
std::vector<int> sDeck;        // shuffled indices into kNoWords
size_t sDeckPos = 0;
std::array<std::string, 3> sOptions;
int sYesIndex = -1;            // which of the 3 lines is "Yes" in this round, -1 = none
s16 sCamIdx = 0;               // one-point camera that looks at the owl
Input sSavedInput;
bool sInputSaved = false;

std::mt19937& Rng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

void ShuffleDeck() {
    sDeck.resize(kNoWords.size());
    for (size_t i = 0; i < sDeck.size(); i++) {
        sDeck[i] = static_cast<int>(i);
    }
    std::shuffle(sDeck.begin(), sDeck.end(), Rng());
    sDeckPos = 0;
}

int NextNo() {
    if (sDeckPos >= sDeck.size()) {
        ShuffleDeck();
    }
    return sDeck[sDeckPos++];
}

// Prepares the three answers of the current round
void BuildRound() {
    const bool hasYes = sRound >= kNoOnlyRounds;
    sYesIndex = hasYes ? static_cast<int>(Rng()() % 3) : -1;
    for (int i = 0; i < 3; i++) {
        sOptions[i] = (i == sYesIndex) ? "Yes" : kNoWords[NextNo()];
    }
}

std::string Green() {
    return std::string("\x05") + std::string(1, '\x42');
}

std::string White() {
    return std::string("\x05") + std::string(1, '\x40');
}

// Text boxes: 3 pages of placeholder text, then the question with 3 answers
void LoadOwlText() {
    BuildRound();
    std::string text;
    text += "Lorem ipsum dolor sit amet,&consectetur adipiscing elit.^";
    text += "Sed do eiusmod tempor&incididunt ut labore et&dolore magna aliqua.^";
    text += "Ut enim ad minim veniam,&quis nostrud exercitation&ullamco laboris nisi.^";
    text += "Did you understand?&";
    text += CustomMessage::THREE_WAY_CHOICE();
    text += Green() + sOptions[0] + "&" + sOptions[1] + "&" + sOptions[2] + White();

    CustomMessage message(text, TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM);
    message.Format();
    message.LoadIntoFont();
}

void LoadSignText() {
    CustomMessage message("Lorem ipsum dolor sit amet,&consectetur adipiscing elit,&sed do eiusmod tempor.",
                          TEXTBOX_TYPE_WOODEN, TEXTBOX_POS_BOTTOM);
    message.Format();
    message.LoadIntoFont();
}

void StartConversation() {
    sRound = 0;
    ShuffleDeck();
    sPhase = Phase::Dialog;
    sLocked = true;
    Message_StartTextbox(gPlayState, kTextOwl, sOwl);
    if (sCamIdx == 0 && sOwl != nullptr) {
        // same camera the original owl uses
        sCamIdx = OnePointCutscene_Init(gPlayState, 8700, -99, sOwl, CAM_ID_MAIN);
    }
}

void EndConversation(bool owlLeaves) {
    if (gPlayState != nullptr && sLocked) {
        Message_CloseTextbox(gPlayState);
    }
    if (gPlayState != nullptr && sCamIdx > 0) {
        OnePointCutscene_EndCutscene(gPlayState, sCamIdx);
    }
    sCamIdx = 0;
    sLocked = false;
    sPhase = Phase::Done;
    if (owlLeaves && sOwl != nullptr) {
        Cursed::Despawn(sOwl);
        sOwl = nullptr;
    }
}

void SpawnScene(float x, float y, float z, s16 yaw) {
    sSign = static_cast<Actor*>(Cursed::SpawnActorAt(ACTOR_EN_KANBAN, 0, x, y, z, yaw));
    if (sSign == nullptr) {
        Cursed::Notify("Cannot spawn right now");
        sPhase = Phase::Idle;
        return;
    }
    sSign->textId = kTextSign;

    sOwl = static_cast<Actor*>(Cursed::SpawnActorAt(ACTOR_EN_OWL, 0, x, y + kOwlHeight, z, yaw));
    if (sOwl != nullptr) {
        Cursed::ShowInfinite(sOwl, "Owl");
    }
    if (sOwl == nullptr) {
        Cursed::Notify("Kaepora could not appear");
        Actor_Kill(sSign);
        sSign = nullptr;
        sPhase = Phase::Idle;
        return;
    }
    StartConversation();
}

// Keeps Link safe and lets enemies approach without attacking
void UpdateLock() {
    Player* player = GET_PLAYER(gPlayState);
    player->invincibilityTimer = 20;
    player->actor.freezeTimer = 0;
    gSaveContext.health = gSaveContext.healthCapacity;

    for (Actor* enemy = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; enemy != nullptr; enemy = enemy->next) {
        if (enemy->update == nullptr) {
            continue;
        }
        enemy->freezeTimer = 3; // skips its whole update: no attacks, no animation
        if (enemy->xzDistToPlayer > kCreepStopDistance) {
            const s16 yaw = Math_Vec3f_Yaw(&enemy->world.pos, &player->actor.world.pos);
            enemy->world.pos.x += Math_SinS(yaw) * kCreepSpeed;
            enemy->world.pos.z += Math_CosS(yaw) * kCreepSpeed;
            enemy->world.rot.y = enemy->shape.rot.y = yaw;
        }
    }
}

void HandleAnswer() {
    MessageContext* msgCtx = &gPlayState->msgCtx;
    if (Message_GetState(msgCtx) != TEXT_STATE_CHOICE || !Message_ShouldAdvance(gPlayState)) {
        return;
    }
    const int chosen = msgCtx->choiceIndex;
    if (chosen == sYesIndex) {
        EndConversation(true);
        return;
    }
    // "No": next round. After the round that offered a "Yes", start over completely.
    if (sYesIndex >= 0) {
        sRound = 0;
        ShuffleDeck();
    } else {
        sRound++;
    }
    Message_ContinueTextbox(gPlayState, kTextOwl);
}

void Reset() {
    sPhase = Phase::Idle;
    sOwl = nullptr;
    sSign = nullptr;
    sLocked = false;
    sInputSaved = false;
    sCamIdx = 0;
}

void Register() {
    Cursed::RegisterEffect({
        "dice_kaepora",
        "Kaepora Gaebora",
        0.0f,
        [] {
            if (sPhase == Phase::Loading || sPhase == Phase::Dialog) {
                Cursed::Notify("Kaepora is already here");
                return;
            }
            if (gPlayState == nullptr) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const s16 facing = player->actor.shape.rot.y;
            const float x = player->actor.world.pos.x + Math_SinS(facing) * kSpawnDistance;
            const float z = player->actor.world.pos.z + Math_CosS(facing) * kSpawnDistance;
            const float y = player->actor.world.pos.y;
            const s16 yaw = static_cast<s16>(facing + 0x8000); // both look at Link

            sPhase = Phase::Loading;
            Cursed::RequestObject(OBJECT_OWL, [x, y, z, yaw](bool owlOk) {
                if (!owlOk) {
                    Cursed::Notify("Kaepora could not be loaded");
                    sPhase = Phase::Idle;
                    return;
                }
                Cursed::RequestObject(OBJECT_KANBAN, [x, y, z, yaw](bool signOk) {
                    if (!signOk) {
                        Cursed::Notify("The sign could not be loaded");
                        sPhase = Phase::Idle;
                        return;
                    }
                    SpawnScene(x, y, z, yaw);
                });
            });
        },
        nullptr,
    });

    // Our own texts
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(
        [](uint16_t* textId, bool* loadFromMessageTable) {
            if (*textId == kTextOwl) {
                LoadOwlText();
                *loadFromMessageTable = false;
            } else if (*textId == kTextSign) {
                LoadSignText();
                *loadFromMessageTable = false;
            }
        });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr || sPhase != Phase::Dialog) {
            return;
        }
        if (sOwl == nullptr || sOwl->update == nullptr) {
            EndConversation(false); // owl is gone (room change): release Link
            return;
        }
        UpdateLock();

        // The text box was closed by something else (e.g. B): never leave Link locked, ask again
        if (Message_GetState(&gPlayState->msgCtx) == TEXT_STATE_NONE) {
            sRound = 0;
            ShuffleDeck();
            Message_StartTextbox(gPlayState, kTextOwl, sOwl);
        }
    });

    // Block Link's controls: his update sees an empty controller, the text box still sees the real one
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorUpdate>([](void* refActor, bool* should) {
        Actor* actor = static_cast<Actor*>(refActor);

        // The answer must be read inside the actor update: the message system itself runs afterwards
        // and would close a choice box on the same button press.
        if (sPhase == Phase::Dialog && actor == sOwl && sOwl != nullptr) {
            HandleAnswer();
            // "Yes" kills the owl right here; the engine must not call its (now NULL) update afterwards
            if (actor->update == nullptr) {
                *should = false;
            }
            return;
        }

        if (!sLocked || actor->id != ACTOR_PLAYER) {
            return;
        }
        Input* input = &gPlayState->state.input[0];
        sSavedInput = *input;
        sInputSaved = true;
        std::memset(&input->cur, 0, sizeof(input->cur));
        std::memset(&input->press, 0, sizeof(input->press));
        std::memset(&input->rel, 0, sizeof(input->rel));
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorUpdate>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        if (actor->id != ACTOR_PLAYER || !sInputSaved) {
            return;
        }
        gPlayState->state.input[0] = sSavedInput;
        sInputSaved = false;
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        if (refActor == sOwl) {
            sOwl = nullptr;
        }
        if (refActor == sSign) {
            sSign = nullptr;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { Reset(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) { Reset(); });
}

} // namespace

static RegisterShipInitFunc diceKaeporaInit(Register);
