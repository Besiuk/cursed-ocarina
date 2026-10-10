#include "CursedScript_Effects.h"

#include <chrono>
#include <map>
#include <random>

#include "CursedScript_Spawn.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Notification/Notification.h"

namespace Cursed {

namespace {

using Clock = std::chrono::steady_clock;

Origin sOrigin = { 0.0f, 0.0f, 0.0f, false };
std::string sCurrentEffectName;
int sInstance = 0;

std::vector<Effect>& Registry() {
    static std::vector<Effect> effects;
    return effects;
}

std::map<std::string, Clock::time_point>& Active() {
    static std::map<std::string, Clock::time_point> active;
    return active;
}

std::map<std::string, bool>& EnabledMap() {
    static std::map<std::string, bool> enabled;
    return enabled;
}

const Effect* Find(const std::string& id) {
    for (const Effect& effect : Registry()) {
        if (effect.id == id) {
            return &effect;
        }
    }
    return nullptr;
}

bool TriggerInternal(const std::string& id) {
    const Effect* effect = Find(id);
    if (effect == nullptr) {
        return false;
    }
    if (!GameInteractor::IsSaveLoaded()) {
        Notify("Load a save first");
        return false;
    }

    sCurrentEffectName = effect->name;
    sInstance++;
    auto& active = Active();
    if (active.count(id) == 0 && effect->start) {
        effect->start();
    }
    if (effect->durationSeconds > 0.0f) {
        active[id] = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<float>(effect->durationSeconds));
    }
    return true;
}

void RollInternal() {
    const auto& effects = Registry();
    std::vector<size_t> pool;
    for (size_t i = 0; i < effects.size(); i++) {
        if (IsEnabled(effects[i].id)) {
            pool.push_back(i);
        }
    }
    if (pool.empty()) {
        Notify("No effects enabled");
        return;
    }

    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
    const size_t pick = dist(rng);
    const Effect& effect = effects[pool[pick]];

    if (TriggerInternal(effect.id)) {
        Notify("d" + std::to_string(pool.size()) + ": you rolled " + std::to_string(pick + 1) + " - " + effect.name);
    }
}

void Update() {
    const auto now = Clock::now();
    std::vector<std::string> expired;
    for (const auto& entry : Active()) {
        if (now >= entry.second) {
            expired.push_back(entry.first);
        }
    }
    for (const std::string& id : expired) {
        Stop(id);
    }
}

void Register() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(Update);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t fileNum) { StopAll(); });
}

} // namespace

void RegisterEffect(Effect effect) {
    Registry().push_back(std::move(effect));
}

const std::vector<Effect>& GetEffects() {
    return Registry();
}

void Notify(const std::string& message) {
    Notification::Options notification;
    notification.prefix = "Cursed Ocarina";
    notification.message = message;
    // The game fades notifications out over their last 3 seconds and they are invisible for the last one,
    // so this is about 4 s fully readable plus 3 s fading
    notification.remainingTime = 8.0f;
    Notification::Emit(notification);
}

const std::string& CurrentEffectName() {
    return sCurrentEffectName;
}

int CurrentInstance() {
    return sInstance;
}

std::vector<TimerInfo> GetActiveTimers() {
    std::vector<TimerInfo> timers;
    for (const auto& entry : Active()) {
        const Effect* effect = Find(entry.first);
        if (effect != nullptr) {
            timers.push_back({ effect->name, RemainingSeconds(entry.first), false });
        }
    }
    for (const SpawnTimer& t : GetSpawnTimers()) {
        timers.push_back({ t.label, t.seconds, t.infinite });
    }
    return timers;
}

bool Trigger(const std::string& id) {
    sOrigin.valid = false;
    return TriggerInternal(id);
}

void Stop(const std::string& id) {
    auto& active = Active();
    auto it = active.find(id);
    if (it == active.end()) {
        return;
    }
    active.erase(it);
    const Effect* effect = Find(id);
    if (effect != nullptr && effect->stop) {
        effect->stop();
    }
}

void StopAll() {
    std::vector<std::string> ids;
    for (const auto& entry : Active()) {
        ids.push_back(entry.first);
    }
    for (const std::string& id : ids) {
        Stop(id);
    }
}

bool IsEnabled(const std::string& id) {
    auto it = EnabledMap().find(id);
    return it == EnabledMap().end() || it->second;
}

void SetEnabled(const std::string& id, bool enabled) {
    EnabledMap()[id] = enabled;
}

void SetAllEnabled(bool enabled) {
    for (const Effect& effect : Registry()) {
        EnabledMap()[effect.id] = enabled;
    }
}

bool IsActive(const std::string& id) {
    return Active().count(id) > 0;
}

float RemainingSeconds(const std::string& id) {
    auto it = Active().find(id);
    if (it == Active().end()) {
        return 0.0f;
    }
    const float remaining = std::chrono::duration<float>(it->second - Clock::now()).count();
    return remaining > 0.0f ? remaining : 0.0f;
}

void RollDice() {
    sOrigin.valid = false;
    RollInternal();
}

void RollDiceAt(float x, float y, float z) {
    sOrigin = { x, y, z, true };
    RollInternal();
}

Origin GetOrigin() {
    return sOrigin;
}

} // namespace Cursed

static RegisterShipInitFunc cursedEffectsInit(Cursed::Register);
