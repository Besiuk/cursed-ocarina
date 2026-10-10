#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Cursed {

struct Effect {
    std::string id;
    std::string name;
    float durationSeconds; // 0 = instant
    std::function<void()> start;
    std::function<void()> stop;
};

// Where the current effect was triggered; invalid means "use Link's position"
struct Origin {
    float x;
    float y;
    float z;
    bool valid;
};

void RegisterEffect(Effect effect);
const std::vector<Effect>& GetEffects();
bool Trigger(const std::string& id);
void Stop(const std::string& id);
void StopAll();
bool IsActive(const std::string& id);

// Dice pool: only enabled effects can be rolled (manual "Trigger" always works)
bool IsEnabled(const std::string& id);
void SetEnabled(const std::string& id, bool enabled);
void SetAllEnabled(bool enabled);
float RemainingSeconds(const std::string& id);
void RollDice();
void RollDiceAt(float x, float y, float z);
Origin GetOrigin();
void Notify(const std::string& message);

// Name of the effect that was triggered last (used to label the actors it spawns)
const std::string& CurrentEffectName();

// Everything that currently runs: timed effects, and effect-spawned actors that are still around.
// Every trigger is its own entry, so the same effect rolled twice shows two parallel lines.
struct TimerInfo {
    std::string name;
    float seconds;
    bool infinite; // shown with an infinity sign instead of a time
};
int CurrentInstance(); // counts up with every triggered effect; groups the actors one trigger spawned
std::vector<TimerInfo> GetActiveTimers();

} // namespace Cursed
