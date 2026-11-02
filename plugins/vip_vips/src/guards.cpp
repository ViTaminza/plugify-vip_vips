#include "guards.hpp"

#include "common.hpp"
#include "log.hpp"
#include "text.hpp"

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <s2sdk/s2sdk.hpp>

#include <string>

namespace vips {
namespace {

// --- map gate state ---------------------------------------------------------------------------
//
// The same three flags (and the same state machine) pisexcompat and s2guard used, driven by the
// s2sdk listeners directly:
//   g_mapReady       true while a map is up: every client/entity call is allowed
//   g_mapStartSeen   an OnMapStart arrived since start / the last OnMapEnd
//   g_mapEndSeen     an OnMapEnd arrived since start
bool g_mapReady = false;
bool g_mapStartSeen = false;
bool g_mapEndSeen = false;
bool g_listenersRegistered = false;

// s2sdk listeners: plain function pointers, called on the game thread. Nothing here may throw,
// and no client/entity call is made: only state changes.
void OnMapStartListener() {
	// OnMapStart is the map's first live frame; the gate is open for everything that follows.
	// (A plugin loaded into a running map never sees this callback - PollMapGate() covers it.)
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
}

void OnMapEndListener() {
	// From here until the next OnMapStart every s2sdk client query can crash (PITFALLS B2): the
	// client list is being torn down. State only, no client call.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

} // namespace

void InitMapGate() {
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	if (g_listenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register && __s2sdk_OnMapEnd_Register) {
		s2sdk::OnMapStart_Register(&OnMapStartListener);
		s2sdk::OnMapEnd_Register(&OnMapEndListener);
		g_listenersRegistered = true;
	} else {
		// Without the map-end listener nothing could take readiness away again, so the gate stays
		// closed for good (PITFALLS A1: log and stay inactive, never return Failed).
		LogError(Format("[Error] [%s] the s2sdk map listeners are unavailable - client calls stay disabled",
						kLogTag));
	}
}

void PollMapGate() {
	if (!g_listenersRegistered) {
		return;
	}
	// A plugin loaded into a running map never saw an OnMapStart: the first OnPluginUpdate after
	// OnPluginStart is the proof of a live map (the host pumps updates from the server frame).
	// After an OnMapEnd only a new OnMapStart re-opens the gate: the host keeps pumping updates
	// through a changelevel, so `!g_mapEndSeen` must not be enough once one was seen.
	if (!g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
	}
}

void ShutdownMapGate() {
	if (g_listenersRegistered) {
		g_listenersRegistered = false;
		if (__s2sdk_OnMapStart_Unregister) {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		}
		if (__s2sdk_OnMapEnd_Unregister) {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		}
	}
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
}

bool IsMapReady() {
	return g_mapReady;
}

// --- client checks ----------------------------------------------------------------------------

bool IsSlotInRange(int32_t slot) {
	return slot >= 0 && slot < kMaxSlots;
}

bool CanQueryClient(int32_t slot) {
	return g_mapReady && IsSlotInRange(slot);
}

bool HasClient(int32_t slot) {
	return CanQueryClient(slot) && __s2sdk_IsClientConnected && s2sdk::IsClientConnected(slot);
}

bool HasController(int32_t slot) {
	return HasClient(slot) && __s2sdk_PlayerSlotToEntPointer && s2sdk::PlayerSlotToEntPointer(slot) != nullptr;
}

bool HasPawn(int32_t slot) {
	if (!HasClient(slot)) {
		return false;
	}
	// PITFALLS C2: connected -> in game -> not SourceTV -> controller -> pawn. Each call is
	// guarded by its binding and by the layer above it, so s2sdk never answers a warning or reads
	// a stale client record.
	if (!__s2sdk_IsClientInGame || !s2sdk::IsClientInGame(slot)) {
		return false;
	}
	if (!__s2sdk_IsClientSourceTV || s2sdk::IsClientSourceTV(slot)) {
		return false;
	}

	// The controller entity: the pointer must exist and its handle must be valid (C2). The old
	// port's GetControllerEntity(i) != 0 check maps onto exactly this pair.
	if (!__s2sdk_PlayerSlotToEntPointer || s2sdk::PlayerSlotToEntPointer(slot) == nullptr) {
		return false;
	}
	if (!__s2sdk_PlayerSlotToEntHandle || !__s2sdk_IsValidEntHandle) {
		return false;
	}
	const int32_t controller = s2sdk::PlayerSlotToEntHandle(slot);
	if (!IsValidHandle(controller) || !s2sdk::IsValidEntHandle(controller)) {
		return false;
	}

	// The pawn. s2sdk names the old GetPlayerPawn(slot)/GetPlayerPawnEntity(slot) pair
	// GetClientPawn(slot) (the pointer) + EntPointerToEntHandle() (the entity handle): both are
	// validated, and the old "GetPawnEntity(i) == 0" skip is the invalid-handle/!pointer case.
	if (!__s2sdk_GetClientPawn || !__s2sdk_EntPointerToEntHandle || !__s2sdk_IsValidEntHandle ||
		!__s2sdk_IsValidEntPointer) {
		return false;
	}
	void* pawn = s2sdk::GetClientPawn(slot);
	if (pawn == nullptr) {
		return false;
	}
	const int32_t pawnEntity = s2sdk::EntPointerToEntHandle(pawn);
	if (!IsValidHandle(pawnEntity) || !s2sdk::IsValidEntHandle(pawnEntity)) {
		return false;
	}
	return s2sdk::IsValidEntPointer(pawn);
}

std::string ClientName(int32_t slot) {
	if (!HasController(slot) || !__s2sdk_GetClientName) {
		return {};
	}
	const plg::string name = s2sdk::GetClientName(slot);
	std::string out(name.data(), name.size());
	if (out == "<unknown>") {
		out.clear(); // s2sdk's placeholder for a client without a name
	}
	return out;
}

// --- output -----------------------------------------------------------------------------------

void PrintToChat(int32_t slot, const std::string& text) {
	if (slot < 0) {
		// The server console: no client query at all, only the tags removed. PrintToServer is
		// ConMsg("%s\n"), so one trailing newline of the text is dropped to stay on one line.
		if (!__s2sdk_PrintToServer) {
			return;
		}
		std::string line = StripColorTags(text);
		if (!line.empty() && line.back() == '\n') {
			line.pop_back();
		}
		s2sdk::PrintToServer(plg::string(line.data(), line.size()));
		return;
	}
	if (!HasClient(slot) || !__s2sdk_PrintToChat) {
		return;
	}
	const std::string colored = Colorize(text);
	s2sdk::PrintToChat(slot, plg::string(colored.data(), colored.size()));
}

} // namespace vips
