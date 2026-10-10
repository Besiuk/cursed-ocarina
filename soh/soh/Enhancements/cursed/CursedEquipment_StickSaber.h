#pragma once

// The Deku Stick as a lightsaber ("Stick Saber"). No edits of the SoH sources needed.
//
// A normal stick behaves normally. As soon as it catches fire (Din's Fire, torch, ...) it becomes a Stick Saber:
// it never burns out, every swing uses up one stick, everything it hits or touches catches fire. The last swing
// puts the fire out and the stick breaks. The saber can be put away and keeps its state.

#ifdef __cplusplus
namespace Cursed {

bool IsStickSaberActive();
void ResetStickSaber(); // e.g. for the debug window

} // namespace Cursed
#endif
