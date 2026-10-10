#pragma once

#include <vector>

namespace Cursed {

// One upgradeable special item (bags, gauntlets, scale, wallet, deku capacities)
struct DebugUpgrade {
    const char* name;
    int upgradeId;
    int maxLevel;
    std::vector<const char*> levelNames;
};

const std::vector<DebugUpgrade>& GetDebugUpgrades();
int GetUpgradeLevel(const DebugUpgrade& upgrade);
void ChangeUpgradeLevel(const DebugUpgrade& upgrade, int delta);

// All inventory items, swords, shields, tunics, boots and magic. Upgrades stay untouched.
void GiveAllEquipment();

// Puts every item with a "first time" story back to its first state (Stick Saber: not lit, no pick-up or
// ignition text shown yet; Kokiri sword: intact). So the first pick-up dialog can be triggered again.
void ResetItemStates();

// Switches between child and adult and reloads the current area (needs a loaded game)
void DebugSwitchAge();
bool DebugIsAdult();

} // namespace Cursed
