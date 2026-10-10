#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Cursed {

// Spawns an actor at the current effect origin (or at Link), scattered within spreadRadius.
// Returns the spawned Actor* (as void*) or nullptr.
void* SpawnActorAtOrigin(int actorId, int params, float spreadRadius);

bool SpawnAtOrigin(int actorId, int params, float spreadRadius);

// Spawns an actor at an exact position (no spread, no height offset).
// ignoreRoomClear: also spawn enemies in rooms whose enemies are already defeated.
void* SpawnActorAt(int actorId, int params, float x, float y, float z, short rotY, bool ignoreRoomClear = false);

// Every actor spawned through SpawnActorAt / SpawnActorAtOrigin is tracked: it stays until Link
// enters another room (or the scene changes). This lets it despawn earlier, after the given time.
void SetLifetimeSeconds(void* actor, float seconds);

// What the timer list on screen shows for tracked actors (one line per effect trigger):
//  - actors with SetLifetimeSeconds: their remaining time
//  - ShowTimerOnly: a remaining time that does not despawn anything (e.g. Din's Fire burns out by itself)
//  - ShowInfinite: the label with an infinity sign for as long as the actor exists in this room
void ShowTimerOnly(void* actor, float seconds);
void ShowInfinite(void* actor, const char* label);

struct SpawnTimer {
    std::string label;
    float seconds;
    bool infinite;
};
std::vector<SpawnTimer> GetSpawnTimers();

// The one way effect-spawned actors go away: small smoke puff, sound, then Actor_Kill.
// Use this instead of Actor_Kill for anything an effect spawned (enemies, NPCs, items).
void Despawn(void* actor);

// Current effect origin (impact point, else Link). Always fills x/y/z.
void GetOriginPoint(float& x, float& y, float& z);

// Makes sure an object file is loaded, then calls done(true) from the frame update.
// done(false) if it could not be loaded. Dropped silently on scene change.
void RequestObject(int objectId, std::function<void(bool)> done);

} // namespace Cursed
