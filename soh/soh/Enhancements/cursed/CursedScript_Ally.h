#pragma once

struct Actor;

namespace Cursed {

// Allies are effect-spawned fighters that only attack enemies. They leave (with a smoke puff)
// after kAllyLifetimeSeconds. Change the value here to change it for all of them.
constexpr float kAllyLifetimeSeconds = 30.0f;

void MarkAlly(Actor* actor);
bool IsAlly(Actor* actor);

// Nearest living enemy within `range` of (homeX, homeZ), measured from `from`. Allies, their
// projectiles and attacking cuccos are never returned.
Actor* FindNearestEnemy(Actor* from, float homeX, float homeZ, float range);

} // namespace Cursed
