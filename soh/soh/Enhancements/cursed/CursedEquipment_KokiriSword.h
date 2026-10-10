#pragma once

// The Kokiri sword ("Modern Zelda Sword") breaks after 10 hits and stays broken until repaired.
//
// Needs two tiny edits in the SoH sources (C side), see the instructions in the chat:
//   z_player.c:            call Cursed_OnSwordHit() at the start of func_80842B7C
//   z_kaleido_equipment.c: use the broken sword icon while Cursed_IsKokiriSwordBroken()

#ifdef __cplusplus
namespace Cursed {

bool IsKokiriSwordBroken();
void RepairKokiriSword(); // e.g. for a freshly found sword (sword rain), or the debug window

} // namespace Cursed
#endif
