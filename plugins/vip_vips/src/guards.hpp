// vip_vips — the plugin's own s2sdk safety guards (PITFALLS.md B1/B2, C2).
//
// The plugin calls s2sdk directly, so every rule s2guard used to enforce lives here:
//
//   * Map gate: no client or entity call is made before the first map, and none in the window
//     from OnMapEnd until the next OnMapStart (the client list is torn down there and s2sdk's
//     client lookups read an emptied list - PITFALLS B1/B2). The gate is registered in
//     OnPluginStart, opened by OnMapStart (and by the first OnPluginUpdate, which proves a live
//     map for a plugin loaded mid-map), and closed by OnMapEnd.
//   * Slot/controller/pawn: s2sdk exports do not validate their arguments (PITFALLS C2), so
//     HasPawn() layers the checks: slot range, IsClientConnected, IsClientInGame,
//     !IsClientSourceTV, a controller entity pointer, a valid controller handle, then the pawn
//     pointer and its valid entity handle.
//   * Output: PrintToChat() only prints through the same gate; a negative slot is the server
//     console, where the colour tags are stripped instead.

#pragma once

#include <cstdint>
#include <string>

namespace vips {

// An s2sdk entity handle is index | serial << 15; -1 is INVALID_EHANDLE_INDEX, 0 is "no entity"
// (the world). The sign means nothing: once the serial passes 65535 a valid handle is negative.
constexpr bool IsValidHandle(int32_t handle) {
	return handle != -1 && handle != 0;
}

// --- map gate ---------------------------------------------------------------------------------

// Registers the OnMapStart/OnMapEnd listeners. Touches no client; safe in OnPluginStart.
void InitMapGate();
// Per-frame pump (OnPluginUpdate): opens the gate for a plugin that loaded into a running map.
void PollMapGate();
// Removes the map listeners and closes the gate; called from OnPluginEnd so `plg reload` leaves
// nothing pointing into the unloaded library (PITFALLS A7).
void ShutdownMapGate();

// True while a map is up and client/entity calls are safe.
bool IsMapReady();

// --- client checks ----------------------------------------------------------------------------

bool IsSlotInRange(int32_t slot);
// A map is up and the slot is in range: any s2sdk client query is safe.
bool CanQueryClient(int32_t slot);
// ... and IsClientConnected (bots count as clients).
bool HasClient(int32_t slot);
// ... and the slot has a controller entity.
bool HasController(int32_t slot);
// ... and the client is in game, is not SourceTV, and has a pawn with a valid pointer and entity
// handle. This is the pre-rewrite HasPawn() (pisexcompat's controller + pawn lookups) and it
// implies HasClient/HasController.
bool HasPawn(int32_t slot);

// The player name; "" without a controller, and for s2sdk's "<unknown>" placeholder.
std::string ClientName(int32_t slot);

// --- output -----------------------------------------------------------------------------------

// Chat output behind HasClient() and through the colour table of the phrase files. A negative slot
// is the server console (always available): the text goes to PrintToServer with the tags stripped.
void PrintToChat(int32_t slot, const std::string& text);

} // namespace vips
