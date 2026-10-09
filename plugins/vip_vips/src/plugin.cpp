// vip_vips — Plugify port of the Pisex VIP module "VIP_Vips" / "[VIP] VIPS"
// (ref/src/cs2-vip-modules/VIP_Vips/source/vip_vips.cpp, 156 lines).
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
// The list mode comes from addons/configs/vip/vip_vips.ini ("VIP" { "type" "0" }): 0 shows the VIP
// group in parentheses, 1 hides it, any other value prints nothing per player (the original's
// switch had no default) - while still printing the title, like the original.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin; the module calls it through the consumer
//     wrapper of plugins/vip/include/vip_api.hpp.
//   * CCSPlayerController::FromSlot + m_hPlayerPawn of the original become pisexcompat's
//     GetControllerEntity / GetPawnEntity (the "continue" of the original is kept: a VIP without
//     a controller or pawn is skipped and does not make bFound true).
//   * m_iszPlayerName() is pisex::g_pPlayers->GetPlayerName (a per-slot cache with stable
//     pointers, so the name and the group can be used in one call).
//   * The phrases are the vip core's (vip.phrases.txt), so vip must ship it; the command is
//     registered in VIP_OnVIPLoaded (once the core's database is up), like the original, with the
//     API's late-load poll (VIP_IsVIPLoaded).
//   * The original loaded vip_vips.ini in Load() and refused to start without it; the port fails
//     soft (logs the missing file, keeps type 0 as the production default).

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>

#include <vip_api.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>

namespace {

// The GetLogTag() of the original ("[VIP-VIPS]").
constexpr char kLogTag[] = "VIP-VIPS";

// The path of the original's KeyValues ("addons/configs/vip/vip_vips.ini").
constexpr char kConfigPath[] = "addons/configs/vip/vip_vips.ini";

// The "type" of the production vip_vips.ini - the default when the file cannot be read.
constexpr int kDefaultType = 0;

// CS2 has 64 player slots.
constexpr int kMaxSlots = 64;

// The object the ported module uses (the Pisex g_pVIPCore).
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

// The original's iType.
int g_iType = kDefaultType;

class VipVipsPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// VIP_OnVIPLoaded of the original: the core's database is up, !vips may run.
	void OnCoreReady();

private:
	void LoadConfig();
	bool OnVipsCommand(int iSlot);

	bool m_coreReady = false;
};

VipVipsPlugin g_plugin;

void VipVipsPlugin::LoadConfig() {
	pisex::KeyValues::AutoDelete config("VIP");
	if (!config->LoadFromFile(kConfigPath)) {
		pisex::ErrorLog("[%s] Failed to load %s (using type %i)", kLogTag, kConfigPath, kDefaultType);
		return;
	}
	g_iType = config->GetInt("type", kDefaultType);
}

bool VipVipsPlugin::OnVipsCommand(int iSlot) {
	bool bFound = false;
	for (int i = 0; i < kMaxSlots; ++i) {
		if (!g_pVIPCore->VIP_IsClientVIP(i)) {
			continue;
		}
		// CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(i);
		// if (!pPlayerController) continue; CCSPlayerPawn* pPlayerPawn = pPlayerController->m_hPlayerPawn();
		// if (!pPlayerPawn) continue;
		if (pisex::GetControllerEntity(i) == 0 || pisex::GetPawnEntity(i) == 0) {
			continue;
		}
		if (!bFound) {
			// g_pUtils->PrintToChat(iSlot, VIP_GetTranslate("VIPS_Title"))
			pisex::PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPS_Title"));
			bFound = true;
		}
		switch (g_iType) {
		case 0:
			// PrintToChat(iSlot, translate("VIPS_Player"), m_iszPlayerName(), VIP_GetClientVIPGroup(i))
			pisex::PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPS_Player"), pisex::g_pPlayers->GetPlayerName(i),
							   g_pVIPCore->VIP_GetClientVIPGroup(i));
			break;
		case 1:
			// PrintToChat(iSlot, translate("VIPS_Player2"), m_iszPlayerName())
			pisex::PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPS_Player2"), pisex::g_pPlayers->GetPlayerName(i));
			break;
		default:
			break; // the original's switch had no default arm either
		}
	}
	if (!bFound) {
		// g_pUtils->PrintToChat(iSlot, VIP_GetTranslate("VIPS_NoPlayers"))
		pisex::PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPS_NoPlayers"));
	}
	return false;
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipVipsPlugin::OnPluginStart() {
	try {
		pisex::Init("vip");
		LoadConfig();

		g_pVIPCore->VIP_OnVIPLoaded([] { g_plugin.OnCoreReady(); });
		if (g_pVIPCore->VIP_IsVIPLoaded()) {
			OnCoreReady();
		}

		return {};
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginStart failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginStart failed with an unknown exception", kLogTag);
	}
	return {}; // never Failed: that stops every plugify plugin
}

plg::PluginResult VipVipsPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		pisex::Poll();
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed with an unknown exception", kLogTag);
	}
	return {};
}

plg::PluginResult VipVipsPlugin::OnPluginEnd() {
	try {
		g_pVIPCore->VIP_OnVIPLoadedUnregister();
		pisex::Shutdown();
	} catch (...) {
		// nothing left to do at unload
	}
	return {};
}

void VipVipsPlugin::OnCoreReady() {
	if (m_coreReady) {
		return;
	}
	m_coreReady = true;

	// RegCommand(g_PLID, {"sm_vips", "mm_vips", "vips"}, {"!vips", "vips"}, OnVipsCommand).
	pisex::g_pUtils->RegCommand({"sm_vips", "mm_vips", "vips"}, {"!vips", "vips"},
								[](int iSlot, const char* szContent) {
									(void)szContent;
									return g_plugin.OnVipsCommand(iSlot);
								});
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
