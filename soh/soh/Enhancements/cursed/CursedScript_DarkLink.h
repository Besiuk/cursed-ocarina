#pragma once

namespace Cursed {

enum class DarkLinkMode {
    Side, // one shadow floating at Link's right side, facing like Link
    Ring, // shadows orbiting Link in world coordinates, always facing away from him
};

// Replaces any existing Dark Link shadows with `count` new ones. They copy Link's inputs, are noclip,
// always have the sword drawn and only damage enemies.
void SpawnDarkLinks(DarkLinkMode mode, int count);

} // namespace Cursed
