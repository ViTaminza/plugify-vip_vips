// vip_vips — Plugify port of the Pisex VIP module "VIP_Vips" / "[VIP] VIPS"
// (the original's vip_vips.cpp, 156 lines).
//
// Behaviour kept identical: !vips / vips / mm_vips / sm_vips lists every VIP that is currently on
// the server, slot by slot:
//
//   "VIP players:"                                       (VIPS_Title, printed once, only when a
//                                                         listable VIP exists)
//   "name (group)"                                       (VIPS_Player, type 0)
//   "name"                                               (VIPS_Player2, type 1)
//   "No VIP players found"                               (VIPS_NoPlayers, when bFound stayed false)
//
// The list mode comes from addons/plugify/configs/vip/vip_vips.ini ("VIP" { "type" "0" }): 0 shows
// the VIP group in parentheses, 1 hides it, any other value prints nothing per player (the
// original's switch had no default) - while still printing the title, like the original.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin; the module calls it through the consumer
//     wrapper of plugins/vip/include/vip_api.hpp.
//   * Pure s2sdk: there is no s2guard and no pisexcompat. Every safety rule the plugin needs
//     lives in src/:
//       guards.cpp     map gate (OnMapStart/OnMapEnd listeners, first update for a late load) and
//                      the slot/controller/pawn checks (PITFALLS B1/B2, C2) plus guarded output;
//       commands.cpp   console/chat command registration and the say/say_team word listener
//                      (PITFALLS C11), detached in OnPluginEnd (A7);
//       text.cpp       Format/FormatV/FormatPhrase and the cs2-menus colour tags;
//       kv1.cpp        the KeyValues reader for vip_vips.ini;
//       chat_words.cpp the bare chat word matching;
//       config.cpp     <GetConfigsDir()>/vip/vip_vips.ini.
//   * The original's CCSPlayerController::FromSlot + m_hPlayerPawn lookups (pisexcompat's
//     GetControllerEntity/GetPawnEntity != 0) become one vips::HasPawn(i) check: it is false
//     before the first map and from OnMapEnd until the next OnMapStart (no client or entity call
//     in those windows), and it validates the controller entity pointer/handle and the pawn
//     pointer/handle. The "continue" of the original is kept: a VIP without a controller or pawn
//     is skipped and does not make bFound true.
//   * m_iszPlayerName() is vips::ClientName(i) (a std::string, "" instead of s2sdk's
//     "<unknown>"); the group is VIP_GetClientVIPGroup(i).
//   * Printing goes through vips::PrintToChat(iSlot, ...) (the map/slot guard plus the colour
//     tags of the phrase files). pisexcompat's PrintToChat took the phrase itself as the printf
//     FORMAT, so a "%%" in a phrase printed "%": the "name (group)" phrases are formatted with
//     vips::Format(phrase, args...), the two phrases without arguments go through
//     FormatPhrase() (a va_list wrapper, see text.hpp), which keeps the same semantics.
//   * Commands: AddCommand("sm_vips"/"mm_vips", Console), AddCommand("vips", Both) and
//     AddChatWord("vips"), one handler that always answers Handled. They replace pisexcompat's
//     RegCommand({"sm_vips","mm_vips","vips"}, {"!vips","vips"}); the source filter keeps
//     "!sm_vips"/"!mm_vips" from answering in chat, which is what the original's separate
//     registrations did.
//   * Errors go to plg::Log with the "[Error] " prefix pisexcompat's ErrorLog added.
//   * The config is the same vip_vips.ini in the Plugify config tree:
//     <GetConfigsDir()>/vip/vip_vips.ini = addons/plugify/configs/vip/vip_vips.ini
//     (pisexcompat read addons/configs/vip/vip_vips.ini). The original loaded the file in Load()
//     and refused to start without it; the port fails soft (logs the missing file, keeps type 0
//     as the default, PITFALLS A1).
//   * The phrases are the vip core's (vip.phrases.txt), so vip must ship it; the command is
//     registered in VIP_OnVIPLoaded (once the core's database is up), like the original, with the
//     API's late-load poll (VIP_IsVIPLoaded).
//   * The handler answers Handled: unlike Metamod, s2sdk only swallows the public "!" say line
//     when the callback result is Handled, not on the original's "no supercede".

#include <plg/plugin.hpp>
#include <s2sdk/s2sdk/enums.hpp>

#include <vip_api.hpp>

#include <chrono>
#include <cstdint>
#include <exception>

#include "commands.hpp"
#include "common.hpp"
#include "config.hpp"
#include "guards.hpp"
#include "log.hpp"
#include "text.hpp"

namespace {

// The object the ported module uses (the Pisex g_pVIPCore).
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

// The original's iType, read from vip_vips.ini in OnPluginStart.
int g_iType = vips::kDefaultType;

class VipVipsPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// VIP_OnVIPLoaded of the original: the core's database is up, !vips may run.
	void OnCoreReady();

	// One handler for every spelling; always Handled (the old callback answered true).
	s2sdk::ResultType OnVipsCommand(int32_t iSlot);

private:
	// VIP_OnVIPLoaded may fire once; the late-load path must not register a second time.
	bool m_coreReady = false;
};

VipVipsPlugin g_plugin;

// The command/chat-word handler registered in OnCoreReady (the commands module takes a plain
// function pointer, so it cannot point into a member function).
s2sdk::ResultType VipsCommandHandler(int32_t iSlot) {
	return g_plugin.OnVipsCommand(iSlot);
}

s2sdk::ResultType VipVipsPlugin::OnVipsCommand(int32_t iSlot) {
	bool bFound = false;
	for (int32_t i = 0; i < vips::kMaxSlots; ++i) {
		if (!g_pVIPCore->VIP_IsClientVIP(i)) {
			continue;
		}
		// The original's controller + pawn lookups; a VIP without them is skipped (the "continue").
		// HasPawn also refuses every call outside a live map (PITFALLS B1/B2).
		if (!vips::HasPawn(i)) {
			continue;
		}
		if (!bFound) {
			// The title phrase, printed once, for the first listable VIP.
			vips::PrintToChat(iSlot, vips::FormatPhrase(g_pVIPCore->VIP_GetTranslate("VIPS_Title")));
			bFound = true;
		}
		switch (g_iType) {
		case 0:
			// "name (group)" - the original printed the player name and the VIP group.
			vips::PrintToChat(iSlot, vips::Format(g_pVIPCore->VIP_GetTranslate("VIPS_Player"),
												  vips::ClientName(i).c_str(), g_pVIPCore->VIP_GetClientVIPGroup(i)));
			break;
		case 1:
			// "name" - the name only.
			vips::PrintToChat(iSlot, vips::Format(g_pVIPCore->VIP_GetTranslate("VIPS_Player2"),
												  vips::ClientName(i).c_str()));
			break;
		default:
			break; // the original's switch had no default arm either
		}
	}
	if (!bFound) {
		// No listable VIP: the "VIPS_NoPlayers" phrase.
		vips::PrintToChat(iSlot, vips::FormatPhrase(g_pVIPCore->VIP_GetTranslate("VIPS_NoPlayers")));
	}
	// Handled: s2sdk only stops the public "!" say line from being broadcast on this result (the
	// old callback's true; the original's "no supercede" echoed every "!vips" to all players).
	return s2sdk::ResultType::Handled;
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipVipsPlugin::OnPluginStart() {
	try {
		// Map listeners first: they gate every client call this plugin ever makes.
		vips::InitMapGate();
		g_iType = vips::LoadVipVipsType();

		g_pVIPCore->VIP_OnVIPLoaded([] { g_plugin.OnCoreReady(); });
		if (g_pVIPCore->VIP_IsVIPLoaded()) {
			OnCoreReady(); // vip was already up (plugin reloaded mid-map)
		}

		return {};
	} catch (const std::exception& ex) {
		vips::LogError(vips::Format("[Error] [%s] OnPluginStart failed: %s", vips::kLogTag, ex.what()));
	} catch (...) {
		vips::LogError(vips::Format("[Error] [%s] OnPluginStart failed with an unknown exception",
									vips::kLogTag));
	}
	return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
}

plg::PluginResult VipVipsPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		vips::PollMapGate();
	} catch (const std::exception& ex) {
		vips::LogError(vips::Format("[Error] [%s] OnPluginUpdate failed: %s", vips::kLogTag, ex.what()));
	} catch (...) {
		vips::LogError(vips::Format("[Error] [%s] OnPluginUpdate failed with an unknown exception",
									vips::kLogTag));
	}
	return {};
}

plg::PluginResult VipVipsPlugin::OnPluginEnd() {
	try {
		g_pVIPCore->VIP_OnVIPLoadedUnregister();
	} catch (...) {
		// the vip core is going away too; nothing left to do for its callback
	}
	try {
		vips::RemoveCommands(); // every command and say/say_team listener (PITFALLS A7)
	} catch (...) {
	}
	try {
		vips::ShutdownMapGate(); // the map listeners point into this library
	} catch (...) {
	}
	return {};
}

void VipVipsPlugin::OnCoreReady() {
	if (m_coreReady) {
		return;
	}
	m_coreReady = true;

	// The original's registration: the console names and the chat names ("!vips", "vips").
	// "sm_vips"/"mm_vips" answer in a console only; "vips" in both; the bare word comes from the
	// say/say_team listeners (PITFALLS C11).
	vips::AddCommand("sm_vips", vips::CommandFrom::Console, &VipsCommandHandler, "Lists the VIP players");
	vips::AddCommand("mm_vips", vips::CommandFrom::Console, &VipsCommandHandler, "Lists the VIP players");
	vips::AddCommand("vips", vips::CommandFrom::Both, &VipsCommandHandler, "Lists the VIP players");
	vips::AddChatWord("vips", &VipsCommandHandler);
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
