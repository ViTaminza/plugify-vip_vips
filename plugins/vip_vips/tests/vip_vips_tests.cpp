// vip_vips_tests — the engine-free test binary of the pure-s2sdk vip_vips plugin.
//
// Two halves:
//
//   1. The pure helpers, no s2sdk binding touched: the KV1 reader on the production-shaped
//      vip_vips.ini (tests/data), the cs2-menus colour table, the printf-format phrase semantics
//      (FormatPhrase) and the bare chat word matching.
//   2. The plugin's own guards and command dispatch with fake __s2sdk_* bindings (the technique
//      the old s2guard engine tests used): no client query before the first map or after OnMapEnd
//      (PITFALLS B1/B2), HasPawn() rejecting the clients the Pisex controller/pawn lookup rejected
//      (C2), the guarded PrintToChat, and the console/chat source filter plus the say/say_team
//      word (C11) with a clean RemoveCommands() (A7).
//
// Run by ../../test.sh (build/tests/vip_vips_tests), or directly.

#include <plg/plugin.hpp>
#include <s2sdk/s2sdk.hpp>

#include "chat_words.hpp"
#include "commands.hpp"
#include "common.hpp"
#include "config.hpp"
#include "guards.hpp"
#include "kv1.hpp"
#include "text.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// A real plugin gets these from PLUGIFY_PLUGIN; the test leaves them null like a test binary
// without the Plugify module, except the two it drives itself (Log/GetConfigsDir).
namespace plg {
GetBaseDirFn GetBaseDir{nullptr};
GetExtensionsDirFn GetExtensionsDir{nullptr};
GetConfigsDirFn GetConfigsDir{nullptr};
GetDataDirFn GetDataDir{nullptr};
GetLogsDirFn GetLogsDir{nullptr};
GetCacheDirFn GetCacheDir{nullptr};
IsLoadedFn IsLoaded{nullptr};
LogFn Log{nullptr};
BeginZoneFn BeginZone{nullptr};
EndZoneFn EndZone{nullptr};
} // namespace plg

namespace {

int g_checks = 0;
int g_failures = 0;

// Non-printable bytes are shown as \xNN: the colour output is mostly control bytes.
std::string Printable(std::string_view text) {
	std::string out;
	for (const unsigned char c : text) {
		if (c >= 0x20 && c < 0x7f) {
			out.push_back(static_cast<char>(c));
		} else {
			char buffer[8];
			std::snprintf(buffer, sizeof(buffer), "\\x%02X", c);
			out.append(buffer);
		}
	}
	return out;
}

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEq(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected \"%s\", got \"%s\")\n", what.c_str(), Printable(expected).c_str(),
					Printable(actual).c_str());
	}
}

void CheckEqInt(long long actual, long long expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %lld, got %lld)\n", what.c_str(), expected, actual);
	}
}

// --- the plugify host fakes -------------------------------------------------------------------

std::vector<std::string> g_logLines;
plg::Severity g_lastSeverity = plg::Severity::Unknown;
std::string g_configsDir;

void FakeLog(std::string_view message, plg::Severity severity, const plg::source_location& /*location*/) {
	g_logLines.emplace_back(message);
	g_lastSeverity = severity;
}

plg::string FakeGetConfigsDir() {
	return plg::string(g_configsDir.data(), g_configsDir.size());
}

// ==============================================================================================
// Part 1: the pure helpers
// ==============================================================================================

// The production vip_vips.ini, byte for byte (the Russian comment included).
constexpr std::string_view kIniType0 = R"ini("VIP"
{
    "type"     "0"     //Скрыть ли вип группу?(1/0)
}
)ini";

void TestKv1() {
	// The shipped file shape: a single "VIP" block, case-insensitive key lookup.
	const std::optional<vips::KvNode> ini = vips::ParseKv1(kIniType0);
	Check(ini.has_value(), "vip_vips.ini parses");
	if (ini) {
		CheckEqInt(ini->GetInt("type", -1), 0, "vip_vips.ini type = 0");
		CheckEqInt(ini->GetInt("TYPE", -1), 0, "vip_vips.ini key lookup is case-insensitive");
		CheckEq(ini->GetString("type"), "0", "vip_vips.ini type value text");
		CheckEq(ini->GetString("missing", "fallback"), "fallback", "GetString default");
		CheckEqInt(ini->GetInt("missing", 7), 7, "GetInt default");
	}

	// The other list mode of the same file.
	const std::optional<vips::KvNode> type1 = vips::ParseKv1("\"VIP\"\n{\n    \"type\"     \"1\"\n}\n");
	Check(type1.has_value() && type1->GetInt("type", -1) == 1, "type 1 parses");

	// Comments, #base lines, unquoted tokens and a nested block.
	const std::optional<vips::KvNode> grammar = vips::ParseKv1(R"(
#base "something.txt" // skipped, never included
VIP
{
    type 1
    outer /* block comment */ { inner "x" }
    "tail" "y" // trailing comment
}
)");
	Check(grammar.has_value(), "unquoted tokens / comments / nested blocks parse");
	if (grammar) {
		CheckEqInt(grammar->GetInt("type", -1), 1, "unquoted type value");
		const vips::KvNode* outer = grammar->Find("outer");
		Check(outer != nullptr && outer->Find("inner") != nullptr &&
					  outer->GetString("inner") == "x",
			  "nested block");
		CheckEq(grammar->GetString("tail"), "y", "quoted key/value with a trailing comment");
	}

	// Escapes inside quoted tokens.
	const std::optional<vips::KvNode> escapes = vips::ParseKv1(R"("VIP" { "type" "1" "s" "a\"b\\c\td" })");
	Check(escapes.has_value() && escapes->GetString("s") == std::string("a\"b\\c\td"), "quoted escapes");
	Check(escapes.has_value() && escapes->GetInt("type", -1) == 1, "escaped sibling key");

	// CRLF files (the production config came from a Windows checkout once).
	const std::optional<vips::KvNode> crlf = vips::ParseKv1("\"VIP\"\r\n{\r\n    \"type\"    \"1\"\r\n}\r\n");
	Check(crlf.has_value() && crlf->GetInt("type", -1) == 1, "CRLF file");

	// Valve's atoi fallback ("abc" -> 0, "5x" -> 5).
	const std::optional<vips::KvNode> atoi = vips::ParseKv1("\"VIP\" { \"type\" \"abc\" \"other\" \"5x\" }");
	Check(atoi.has_value() && atoi->GetInt("type", -1) == 0, "non-numeric type falls back to atoi -> 0");
	Check(atoi.has_value() && atoi->GetInt("other", -1) == 5, "partially numeric value -> 5");

	// Malformed input is nullopt, not a half-read tree.
	Check(!vips::ParseKv1("\"VIP\" { \"type\" \"0\"").has_value(), "missing '}' is invalid");
	Check(!vips::ParseKv1("\"VIP\" { \"type\" \"0 }").has_value(), "unterminated string is invalid");
	Check(!vips::ParseKv1("\"VIP\" }").has_value(), "stray top-level '}' is invalid");

	// A file without the single "Root" block wrapper keeps its top level.
	const std::optional<vips::KvNode> flat = vips::ParseKv1("\"type\" \"1\"");
	Check(flat.has_value() && flat->GetInt("type", -1) == 1, "flat key/value file");

	// The shipped test data file.
	const std::string path = std::string(VIP_VIPS_TEST_DATA_DIR) + "/vip_vips.ini";
	const std::optional<vips::KvNode> file = vips::LoadKv1File(path);
	Check(file.has_value(), "tests/data/vip_vips.ini loads");
	Check(file.has_value() && file->GetInt("type", -1) == 0, "tests/data/vip_vips.ini type = 0");
	Check(!vips::LoadKv1File(std::string(VIP_VIPS_TEST_DATA_DIR) + "/missing.ini").has_value(),
		  "a missing file is nullopt");
}

void TestColorize() {
	struct ColorCase {
		const char* tag;
		const char* bytes;
	};
	// cs2-menus utils.h colors_text[]/colors_hex[], the table the Metamod servers sent.
	constexpr ColorCase kCases[] = {
			{"{DEFAULT}", "\x01"},
			{"{WHITE}", "\x01"},
			{"{RED}", "\x02"},
			{"{LIGHTPURPLE}", "\x03"},
			{"{GREEN}", "\x04"},
			{"{LIME}", "\x05"},
			{"{LIGHTGREEN}", "\x06"},
			{"{DARKRED}", "\x07"},
			{"{GRAY}", "\x08"},
			{"{LIGHTOLIVE}", "\x09"},
			{"{OLIVE}", "\x10"},
			{"{LIGHTBLUE}", "\x0B"},
			{"{BLUE}", "\x0C"},
			{"{PURPLE}", "\x0E"},
			{"{LIGHTRED}", "\x0F"},
			{"{GRAYBLUE}", "\x0A"},
			{"\\n", "\xe2\x80\xa9"},
	};
	for (const ColorCase& color : kCases) {
		CheckEq(vips::Colorize(color.tag), color.bytes, std::string("Colorize ") + color.tag);
	}

	CheckEq(vips::Colorize("{RED}Bob{DEFAULT}"), "\x02" "Bob" "\x01", "Colorize a whole line");
	CheckEq(vips::Colorize("{RED}"), "\x02", "Colorize a single tag");
	CheckEq(vips::Colorize("plain"), "plain", "Colorize leaves plain text alone");
	CheckEq(vips::Colorize(""), "", "Colorize of an empty string");
	CheckEq(vips::Colorize("{MAGENTA}x"), "{MAGENTA}x", "an unknown tag is left alone");
	CheckEq(vips::Colorize("a\\nb"), "a\xe2\x80\xa9" "b", "the \\n tag is U+2029 in chat");
	CheckEq(vips::StripColorTags("{RED}hi{DEFAULT}"), "hi", "StripColorTags removes tags");
	CheckEq(vips::StripColorTags("a\\nb"), "a\nb", "StripColorTags turns \\n into a newline");
	CheckEq(vips::StripColorTags("plain"), "plain", "StripColorTags leaves plain text alone");
}

void TestFormatPhrase() {
	// A phrase with arguments: the "name (group)" format of vip.phrases.txt.
	CheckEq(vips::FormatPhrase("%s (%s)", "Bob", "Gold"), "Bob (Gold)", "FormatPhrase %s (%s)");
	CheckEq(vips::FormatPhrase("%s", "Bob"), "Bob", "FormatPhrase %s");
	CheckEq(vips::FormatPhrase("%d", 42), "42", "FormatPhrase %d");

	// A phrase without arguments is still the printf FORMAT (the original passed it to
	// pisex::PrintToChat, which took it as the format), so "%%" prints "%".
	CheckEq(vips::FormatPhrase("VIP players:"), "VIP players:", "FormatPhrase without arguments");
	CheckEq(vips::FormatPhrase("100%%"), "100%", "FormatPhrase keeps the %% semantics");
	CheckEq(vips::FormatPhrase(""), "", "FormatPhrase of an empty phrase");
	CheckEq(vips::FormatPhrase(nullptr), "", "FormatPhrase(nullptr) is empty like the old PrintToChat");

	// Format() is the printf-checked sibling used for the phrases that take the name/group.
	CheckEq(vips::Format("%s", "Bob"), "Bob", "Format %s");
	CheckEq(vips::Format("%s (%s)", "Bob", "Gold"), "Bob (Gold)", "Format %s (%s)");
	CheckEq(vips::Format("%d", 42), "42", "Format %d");

	// The GetTranslate() result goes through FormatPhrase, then PrintToChat colorizes it.
	CheckEq(vips::Colorize(vips::FormatPhrase("%s", "{RED}Bob{DEFAULT}")), "\x02" "Bob" "\x01",
			"a formatted phrase is colorized afterwards");
}

void TestChatWords() {
	CheckEq(vips::FirstChatWord("vips"), "vips", "FirstChatWord vips");
	CheckEq(vips::FirstChatWord("VIPS"), "vips", "FirstChatWord is case-insensitive");
	CheckEq(vips::FirstChatWord("   vips"), "vips", "FirstChatWord skips leading spaces");
	CheckEq(vips::FirstChatWord("vips\t\t"), "vips", "FirstChatWord stops at any whitespace");
	CheckEq(vips::FirstChatWord("\"vips\" extra"), "vips", "quoted first word");
	CheckEq(vips::FirstChatWord(""), "", "FirstChatWord of an empty line");
	CheckEq(vips::FirstChatWord("   "), "", "FirstChatWord of whitespace only");

	const std::vector<std::string> words = vips::SplitChatWords("  one  \"two three\" four ");
	CheckEqInt(static_cast<long long>(words.size()), 3, "SplitChatWords count");
	Check(words.size() == 3 && words[0] == "one" && words[1] == "two three" && words[2] == "four",
		  "SplitChatWords quote grouping");

	Check(vips::IsBareChatWord("vips"), "vips is a bare word");
	Check(!vips::IsBareChatWord("!vips"), "!vips is not a bare word");
	Check(!vips::IsBareChatWord("/vips"), "/vips is not a bare word");
	Check(!vips::IsBareChatWord(""), "an empty word is not a bare word");

	Check(vips::MatchesChatWord("vips", "vips"), "the bare word matches itself");
	Check(vips::MatchesChatWord("VIPS", "vips"), "the bare word matches case-insensitively");
	Check(vips::MatchesChatWord("  vips  ", "vips"), "the bare word matches with whitespace");
	Check(vips::MatchesChatWord("\"vips\"", "vips"), "the bare word matches when quoted");
	Check(vips::MatchesChatWord("vips top", "vips"), "only the first word decides");

	Check(!vips::MatchesChatWord("top vips", "vips"), "a later word does not match");
	Check(!vips::MatchesChatWord("!vips", "vips"), "!vips is handled by the chat command, not the word");
	Check(!vips::MatchesChatWord("/vips", "vips"), "/vips is handled by the chat command");
	Check(!vips::MatchesChatWord("svips", "vips"), "a different word does not match");
	Check(!vips::MatchesChatWord("vipsx", "vips"), "a longer word does not match");
	Check(!vips::MatchesChatWord("", "vips"), "an empty line does not match");
	Check(!vips::MatchesChatWord("vips", "!vips"), "the registered word must itself be bare");

	// The already-tokenised console path (s2sdk split the line itself).
	Check(vips::MatchesChatWordToken("vips", "vips"), "a token matches itself");
	Check(vips::MatchesChatWordToken("VIPS", "vips"), "a token matches case-insensitively");
	Check(!vips::MatchesChatWordToken("!vips", "vips"), "a trigger token is not a bare word");
	Check(!vips::MatchesChatWordToken("vips top", "vips"), "a token is not split again");
	Check(!vips::MatchesChatWordToken("", "vips"), "an empty token does not match");
}

void TestConfig() {
	// A throwaway configs dir; the plugin reads <configs>/vip/vip_vips.ini.
	std::error_code ec;
	const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / "vip_vips_tests";
	if (ec) {
		Check(false, "std::filesystem::temp_directory_path");
		return;
	}
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root / "vip", ec);
	g_configsDir = root.string();

	plg::GetConfigsDir = &FakeGetConfigsDir;
	plg::Log = &FakeLog;

	CheckEq(vips::ConfigPath("vip/vip_vips.ini"), root.string() + "/vip/vip_vips.ini", "ConfigPath joins");

	// No file: fail soft to type 0 with one error line (the original refused to start; the port
	// follows PITFALLS A1).
	g_logLines.clear();
	g_lastSeverity = plg::Severity::Unknown;
	CheckEqInt(vips::LoadVipVipsType(), vips::kDefaultType, "a missing vip_vips.ini keeps type 0");
	Check(!g_logLines.empty(), "a missing vip_vips.ini logs one error line");
	Check(g_lastSeverity == plg::Severity::Error, "the missing-config line is an error");

	// The real content, type 1.
	{
		std::ofstream file(root / "vip" / "vip_vips.ini", std::ios::binary | std::ios::trunc);
		file << "\"VIP\"\n{\n    \"type\"     \"1\"     //hide the group\n}\n";
	}
	g_logLines.clear();
	CheckEqInt(vips::LoadVipVipsType(), 1, "vip_vips.ini type 1 is read");
	Check(g_logLines.empty(), "a valid vip_vips.ini logs nothing");

	// Broken file: fail soft again.
	{
		std::ofstream file(root / "vip" / "vip_vips.ini", std::ios::binary | std::ios::trunc);
		file << "\"VIP\" { \"type\" \"1\"\n";
	}
	g_logLines.clear();
	CheckEqInt(vips::LoadVipVipsType(), vips::kDefaultType, "a malformed vip_vips.ini keeps type 0");
	Check(!g_logLines.empty(), "a malformed vip_vips.ini logs one error line");

	// No Plugify host (the configs dir is unavailable).
	plg::GetConfigsDir = nullptr;
	g_logLines.clear();
	Check(vips::ConfigPath("vip/vip_vips.ini").empty(), "ConfigPath without a host is empty");
	CheckEqInt(vips::LoadVipVipsType(), vips::kDefaultType, "no configs dir keeps type 0");
	Check(!g_logLines.empty(), "no configs dir logs one error line");

	plg::GetConfigsDir = &FakeGetConfigsDir;
	g_configsDir = root.string();
	std::filesystem::remove_all(root, ec);
}

// ==============================================================================================
// Part 2: the guards and the command dispatch with fake s2sdk bindings
// ==============================================================================================

struct FakeClient {
	bool connected = false;
	bool inGame = false;
	bool sourceTV = false;
	bool controllerPointer = false; // PlayerSlotToEntPointer() answers a non-null entity
	bool pawn = false;              // GetClientPawn() answers a non-null pawn
	bool pawnPointerValid = true;   // IsValidEntPointer(pawn)
	int32_t controllerHandle = 1000;
	int32_t pawnHandle = 2000;
	std::string name;
};

std::vector<FakeClient> g_clients(vips::kMaxSlots);

int g_clientQueries = 0;      // IsClientConnected / IsClientInGame / IsClientSourceTV
int g_controllerPointerCalls = 0;
int g_controllerHandleCalls = 0;
int g_pawnCalls = 0;
int g_nameCalls = 0;

void ResetCounters() {
	g_clientQueries = 0;
	g_controllerPointerCalls = 0;
	g_controllerHandleCalls = 0;
	g_pawnCalls = 0;
	g_nameCalls = 0;
}

void ResetClients() {
	std::fill(g_clients.begin(), g_clients.end(), FakeClient{});
}

FakeClient& ConnectedClient(int32_t slot, const std::string& name = "Bob") {
	FakeClient& client = g_clients[static_cast<std::size_t>(slot)];
	client = FakeClient{};
	client.connected = true;
	client.inGame = true;
	client.controllerPointer = true;
	client.pawn = true;
	client.name = name;
	return client;
}

bool FakeIsClientConnected(int32_t slot) {
	++g_clientQueries;
	return g_clients[static_cast<std::size_t>(slot)].connected;
}

bool FakeIsClientInGame(int32_t slot) {
	++g_clientQueries;
	return g_clients[static_cast<std::size_t>(slot)].inGame;
}

bool FakeIsClientSourceTV(int32_t slot) {
	++g_clientQueries;
	return g_clients[static_cast<std::size_t>(slot)].sourceTV;
}

void* FakePlayerSlotToEntPointer(int32_t slot) {
	++g_controllerPointerCalls;
	if (!g_clients[static_cast<std::size_t>(slot)].controllerPointer) {
		return nullptr;
	}
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + slot));
}

int32_t FakePlayerSlotToEntHandle(int32_t slot) {
	++g_controllerHandleCalls;
	return g_clients[static_cast<std::size_t>(slot)].controllerHandle;
}

void* FakeGetClientPawn(int32_t slot) {
	++g_pawnCalls;
	if (!g_clients[static_cast<std::size_t>(slot)].pawn) {
		return nullptr;
	}
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2000 + slot));
}

int32_t FakeEntPointerToEntHandle(void* entity) {
	const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(entity);
	if (value < 0x2000 || value >= 0x2000 + static_cast<std::uintptr_t>(vips::kMaxSlots)) {
		return -1;
	}
	return g_clients[static_cast<std::size_t>(value - 0x2000)].pawnHandle;
}

bool FakeIsValidEntHandle(int32_t handle) {
	return handle != -1 && handle != 0;
}

bool FakeIsValidEntPointer(void* entity) {
	const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(entity);
	if (entity == nullptr || value < 0x2000 || value >= 0x2000 + static_cast<std::uintptr_t>(vips::kMaxSlots)) {
		return entity != nullptr;
	}
	return g_clients[static_cast<std::size_t>(value - 0x2000)].pawnPointerValid;
}

plg::string FakeGetClientName(int32_t slot) {
	++g_nameCalls;
	const std::string& name = g_clients[static_cast<std::size_t>(slot)].name;
	return plg::string(name.data(), name.size());
}

struct FakePrint {
	int32_t slot = 0;
	std::string text;
};

std::vector<FakePrint> g_chatPrints;
std::vector<std::string> g_serverPrints;

void FakePrintToChat(int32_t slot, const plg::string& message) {
	g_chatPrints.push_back({slot, std::string(message.data(), message.size())});
}

void FakePrintToServer(const plg::string& message) {
	g_serverPrints.emplace_back(message.data(), message.size());
}

// --- map listeners ----------------------------------------------------------------------------

s2sdk::OnMapStartCallback g_mapStartCallback = nullptr;
s2sdk::OnMapEndCallback g_mapEndCallback = nullptr;
int g_mapStartRegistrations = 0;
int g_mapStartUnregistrations = 0;
int g_mapEndRegistrations = 0;
int g_mapEndUnregistrations = 0;

void FakeOnMapStartRegister(s2sdk::OnMapStartCallback callback) {
	g_mapStartCallback = callback;
	++g_mapStartRegistrations;
}

void FakeOnMapStartUnregister(s2sdk::OnMapStartCallback callback) {
	if (g_mapStartCallback == callback) {
		g_mapStartCallback = nullptr;
	}
	++g_mapStartUnregistrations;
}

void FakeOnMapEndRegister(s2sdk::OnMapEndCallback callback) {
	g_mapEndCallback = callback;
	++g_mapEndRegistrations;
}

void FakeOnMapEndUnregister(s2sdk::OnMapEndCallback callback) {
	if (g_mapEndCallback == callback) {
		g_mapEndCallback = nullptr;
	}
	++g_mapEndUnregistrations;
}

void FireMapStart() {
	if (g_mapStartCallback != nullptr) {
		g_mapStartCallback();
	}
}

void FireMapEnd() {
	if (g_mapEndCallback != nullptr) {
		g_mapEndCallback();
	}
}

// --- command bindings -------------------------------------------------------------------------

s2sdk::ConCommandCallback g_commandCallback = nullptr;
std::vector<std::string> g_commandNames;
std::vector<std::string> g_existingCommands; // AddConsoleCommand refuses these names (returns false)
std::vector<std::pair<std::string, s2sdk::ConCommandCallback>> g_listeners;
int g_addedCommands = 0;
int g_removedCommands = 0;
int g_removedListeners = 0;

std::string ToStd(const plg::string& text) {
	return std::string(text.data(), text.size());
}

bool FakeAddConsoleCommand(const plg::string& name, const plg::string& /*description*/, s2sdk::ConVarFlag /*flags*/,
						   s2sdk::ConCommandCallback callback, s2sdk::HookMode /*type*/) {
	++g_addedCommands;
	g_commandCallback = callback;
	g_commandNames.push_back(ToStd(name));
	return std::find(g_existingCommands.begin(), g_existingCommands.end(), ToStd(name)) ==
		   g_existingCommands.end();
}

bool FakeRemoveCommand(const plg::string& /*name*/, s2sdk::ConCommandCallback /*callback*/) {
	++g_removedCommands;
	return true;
}

bool FakeAddCommandListener(const plg::string& name, s2sdk::ConCommandCallback callback, s2sdk::HookMode /*type*/) {
	g_listeners.emplace_back(ToStd(name), callback);
	return true;
}

bool FakeRemoveCommandListener(const plg::string& /*name*/, s2sdk::ConCommandCallback /*callback*/,
							   s2sdk::HookMode /*type*/) {
	++g_removedListeners;
	return true;
}

void InstallFakes() {
	__s2sdk_IsClientConnected = &FakeIsClientConnected;
	__s2sdk_IsClientInGame = &FakeIsClientInGame;
	__s2sdk_IsClientSourceTV = &FakeIsClientSourceTV;
	__s2sdk_PlayerSlotToEntPointer = &FakePlayerSlotToEntPointer;
	__s2sdk_PlayerSlotToEntHandle = &FakePlayerSlotToEntHandle;
	__s2sdk_GetClientPawn = &FakeGetClientPawn;
	__s2sdk_EntPointerToEntHandle = &FakeEntPointerToEntHandle;
	__s2sdk_IsValidEntHandle = &FakeIsValidEntHandle;
	__s2sdk_IsValidEntPointer = &FakeIsValidEntPointer;
	__s2sdk_GetClientName = &FakeGetClientName;
	__s2sdk_PrintToChat = &FakePrintToChat;
	__s2sdk_PrintToServer = &FakePrintToServer;
	__s2sdk_OnMapStart_Register = &FakeOnMapStartRegister;
	__s2sdk_OnMapStart_Unregister = &FakeOnMapStartUnregister;
	__s2sdk_OnMapEnd_Register = &FakeOnMapEndRegister;
	__s2sdk_OnMapEnd_Unregister = &FakeOnMapEndUnregister;
	__s2sdk_AddConsoleCommand = &FakeAddConsoleCommand;
	__s2sdk_RemoveCommand = &FakeRemoveCommand;
	__s2sdk_AddCommandListener = &FakeAddCommandListener;
	__s2sdk_RemoveCommandListener = &FakeRemoveCommandListener;
}

void TestMapGateAndPawn() {
	ResetClients();
	ResetCounters();

	// No map listeners: the gate must stay shut instead of guessing readiness from updates.
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;
	g_logLines.clear();
	vips::InitMapGate();
	vips::PollMapGate();
	Check(!vips::IsMapReady(), "without the map listeners the gate stays shut");
	Check(!g_logLines.empty(), "without the map listeners one error line is logged");
	Check(!vips::HasPawn(0), "without the map listeners HasPawn is false");
	CheckEqInt(g_clientQueries, 0, "without the map listeners no client query is made");

	// Bindings available: OnPluginStart registers both listeners.
	InstallFakes();
	vips::InitMapGate();
	CheckEqInt(g_mapStartRegistrations, 1, "OnMapStart listener registered once");
	CheckEqInt(g_mapEndRegistrations, 1, "OnMapEnd listener registered once");
	Check(!vips::IsMapReady(), "the gate is shut until a map start or the first update");

	// A plugin loaded into a running map: the first update proves a live map.
	vips::PollMapGate();
	Check(vips::IsMapReady(), "the first update after OnPluginStart opens the gate");

	// A real, in-game, non-SourceTV client with a controller and a pawn.
	ConnectedClient(1, "Bob");
	Check(vips::HasClient(1), "a connected client passes HasClient");
	Check(vips::HasPawn(1), "a client with a controller and a pawn passes HasPawn");

	// Out-of-range slots: no s2sdk call at all.
	ResetCounters();
	Check(!vips::HasClient(-1), "slot -1 is refused");
	Check(!vips::HasClient(vips::kMaxSlots), "slot 64 is refused");
	Check(!vips::HasPawn(-1), "HasPawn(-1) is false");
	Check(!vips::HasPawn(vips::kMaxSlots), "HasPawn(64) is false");
	CheckEqInt(g_clientQueries, 0, "no client query for an out-of-range slot");

	// Not connected.
	ResetCounters();
	Check(!vips::HasPawn(2), "an empty slot has no pawn");
	CheckEqInt(g_pawnCalls, 0, "an empty slot never reaches GetClientPawn");

	// Connected but not in game.
	ResetCounters();
	g_clients[3].connected = true;
	Check(!vips::HasPawn(3), "a client that is not in game has no pawn");
	CheckEqInt(g_pawnCalls, 0, "a client that is not in game never reaches GetClientPawn");

	// SourceTV (connected, in game, controller and pawn present): refused.
	ResetCounters();
	ConnectedClient(4, "SourceTV");
	g_clients[4].sourceTV = true;
	Check(!vips::HasPawn(4), "SourceTV has no pawn");
	CheckEqInt(g_pawnCalls, 0, "SourceTV never reaches GetClientPawn");

	// No controller entity pointer: refused, and the pawn is never queried.
	ResetCounters();
	ConnectedClient(5);
	g_clients[5].controllerPointer = false;
	Check(!vips::HasPawn(5), "a client without a controller entity has no pawn");
	CheckEqInt(g_pawnCalls, 0, "a client without a controller never reaches GetClientPawn");

	// Invalid controller handle (0 = the world, -1 = invalid): refused.
	ResetCounters();
	ConnectedClient(6);
	g_clients[6].controllerHandle = 0;
	Check(!vips::HasPawn(6), "a zero controller handle is not a valid controller");
	CheckEqInt(g_pawnCalls, 0, "an invalid controller handle never reaches GetClientPawn");
	ConnectedClient(6);
	g_clients[6].controllerHandle = -1;
	Check(!vips::HasPawn(6), "an INVALID_EHANDLE_INDEX controller handle is refused");

	// A controller without a pawn.
	ResetCounters();
	ConnectedClient(7);
	g_clients[7].pawn = false;
	Check(!vips::HasPawn(7), "a controller without a pawn has no pawn");
	CheckEqInt(g_pawnCalls, 1, "the pawn pointer is queried once");

	// A pawn whose entity handle is 0 (the old "GetPawnEntity(i) == 0" case).
	ConnectedClient(8);
	g_clients[8].pawnHandle = 0;
	Check(!vips::HasPawn(8), "a pawn with entity handle 0 is skipped");

	// A pawn whose entity handle is -1.
	ConnectedClient(9);
	g_clients[9].pawnHandle = -1;
	Check(!vips::HasPawn(9), "a pawn with an invalid entity handle is skipped");

	// A pawn pointer that s2sdk does not consider a valid entity.
	ConnectedClient(10);
	g_clients[10].pawnPointerValid = false;
	Check(!vips::HasPawn(10), "a pawn pointer that is not a valid entity is skipped");

	// Names: s2sdk's placeholder becomes an empty string (the pre-rewrite s2guard::ClientName).
	CheckEq(vips::ClientName(1), "Bob", "ClientName returns the name");
	ResetCounters();
	g_clients[1].name = "<unknown>";
	CheckEq(vips::ClientName(1), "", "s2sdk's <unknown> name becomes empty");
	g_clients[1].name = "Bob";
	ResetCounters();
	g_clients[1].controllerPointer = false;
	CheckEq(vips::ClientName(1), "", "no controller -> empty name");
	CheckEqInt(g_nameCalls, 0, "no controller -> GetClientName is not called");
	ConnectedClient(1, "Bob");

	// Printing: chat behind HasClient, server console for a negative slot.
	g_chatPrints.clear();
	g_serverPrints.clear();
	vips::PrintToChat(1, "{RED}hi{DEFAULT}");
	CheckEqInt(static_cast<long long>(g_chatPrints.size()), 1, "an in-game client gets the chat line");
	Check(g_chatPrints.size() == 1 && g_chatPrints[0].slot == 1, "the chat line goes to the caller slot");
	Check(g_chatPrints.size() == 1 && g_chatPrints[0].text == "\x02" "hi" "\x01",
		  "the chat line is colorized");

	vips::PrintToChat(20, "{RED}hi{DEFAULT}");
	CheckEqInt(static_cast<long long>(g_chatPrints.size()), 1, "an empty slot gets nothing");

	g_serverPrints.clear();
	vips::PrintToChat(-1, "{RED}hi{DEFAULT}");
	CheckEqInt(static_cast<long long>(g_serverPrints.size()), 1, "a negative slot prints to the server");
	Check(g_serverPrints.size() == 1 && g_serverPrints[0] == "hi", "the server line has its tags stripped");
	vips::PrintToChat(-1, "line\\n");
	Check(g_serverPrints.size() == 2 && g_serverPrints[1] == "line",
		  "the server line drops the trailing chat line break");

	// OnMapEnd: closed gate, and not one client query afterwards (PITFALLS B2).
	FireMapEnd();
	Check(!vips::IsMapReady(), "OnMapEnd closes the gate");
	ResetCounters();
	Check(!vips::HasClient(1), "no client after OnMapEnd");
	Check(!vips::HasPawn(1), "no pawn after OnMapEnd");
	CheckEq(vips::ClientName(1), "", "no name after OnMapEnd");
	CheckEqInt(g_clientQueries, 0, "no s2sdk client query in the map-change window");
	CheckEqInt(g_pawnCalls, 0, "no GetClientPawn in the map-change window");

	g_chatPrints.clear();
	vips::PrintToChat(1, "{RED}hi");
	CheckEqInt(static_cast<long long>(g_chatPrints.size()), 0, "no chat line in the map-change window");

	// The host keeps pumping updates through a changelevel: they must not reopen the gate.
	vips::PollMapGate();
	Check(!vips::IsMapReady(), "updates alone do not reopen the gate after OnMapEnd");

	// The next OnMapStart reopens it.
	FireMapStart();
	Check(vips::IsMapReady(), "OnMapStart reopens the gate");
	Check(vips::HasPawn(1), "the client passes HasPawn on the next map");
}

int g_handlerCalls = 0;
int32_t g_lastCaller = -99;

s2sdk::ResultType CountingHandler(int32_t slot) {
	++g_handlerCalls;
	g_lastCaller = slot;
	return s2sdk::ResultType::Handled;
}

plg::vector<plg::string> Args(std::initializer_list<std::string_view> words) {
	plg::vector<plg::string> out;
	for (const std::string_view word : words) {
		out.push_back(plg::string(word.data(), word.size()));
	}
	return out;
}

void TestCommands() {
	// The map fakes are from TestMapGateAndPawn; the command fakes were installed there too.
	g_existingCommands = {"mm_vips"}; // the engine/another plugin already owns "mm_vips"
	g_addedCommands = 0;
	g_removedCommands = 0;
	g_removedListeners = 0;
	g_listeners.clear();
	g_commandNames.clear();
	g_handlerCalls = 0;

	Check(vips::AddCommand("sm_vips", vips::CommandFrom::Console, &CountingHandler, "Lists the VIP players"),
		  "sm_vips registers for the console");
	Check(vips::AddCommand("mm_vips", vips::CommandFrom::Console, &CountingHandler, "Lists the VIP players"),
		  "mm_vips registers for the console (an existing name is reused)");
	Check(vips::AddCommand("vips", vips::CommandFrom::Both, &CountingHandler, "Lists the VIP players"),
		  "vips registers for both sources");
	Check(vips::AddChatWord("vips", &CountingHandler), "the bare chat word registers");

	CheckEqInt(g_addedCommands, 3, "three console names go through AddConsoleCommand");
	CheckEqInt(static_cast<long long>(g_listeners.size()), 2, "say and say_team listeners installed");
	Check(g_listeners.size() == 2 && g_listeners[0].first == "say" && g_listeners[1].first == "say_team",
		  "the listeners are on say and say_team");

	// Duplicate registrations are refused.
	Check(!vips::AddCommand("vips", vips::CommandFrom::Chat, &CountingHandler, ""),
		  "a duplicate command source is refused");
	Check(!vips::AddChatWord("vips", &CountingHandler), "a duplicate chat word is refused");
	Check(!vips::AddCommand("!vips", vips::CommandFrom::Both, &CountingHandler, ""),
		  "a name with a chat trigger is refused");
	Check(!vips::AddCommand("", vips::CommandFrom::Both, &CountingHandler, ""), "an empty name is refused");

	Check(g_commandCallback != nullptr, "the command trampoline was captured");
	if (g_commandCallback == nullptr) {
		return;
	}

	// "sm_vips" in the console: the handler runs as the caller.
	g_handlerCalls = 0;
	g_lastCaller = -99;
	Check(g_commandCallback(-1, s2sdk::ConCommandContext::Console, Args({"sm_vips"})) ==
				  s2sdk::ResultType::Handled,
		  "sm_vips answers in the console with Handled");
	CheckEqInt(g_handlerCalls, 1, "the sm_vips handler ran once");
	CheckEqInt(g_lastCaller, -1, "the server console caller is forwarded");
	Check(g_commandNames.size() == 3 && g_commandNames[0] == "sm_vips" && g_commandNames[1] == "mm_vips" &&
				  g_commandNames[2] == "vips",
		  "the console names are registered in order");

	// "sm_vips"/"mm_vips" in chat ("!sm_vips"): Console only, so the handler must not run.
	g_handlerCalls = 0;
	Check(g_commandCallback(5, s2sdk::ConCommandContext::Chat, Args({"sm_vips"})) ==
				  s2sdk::ResultType::Continue,
		  "console-only sm_vips is left to the engine in chat");
	Check(g_commandCallback(5, s2sdk::ConCommandContext::Chat, Args({"mm_vips"})) ==
				  s2sdk::ResultType::Continue,
		  "console-only mm_vips is left to the engine in chat");
	CheckEqInt(g_handlerCalls, 0, "the console-only names never ran in chat");

	// "vips" in both sources.
	g_handlerCalls = 0;
	g_lastCaller = -99;
	Check(g_commandCallback(-1, s2sdk::ConCommandContext::Console, Args({"vips"})) ==
				  s2sdk::ResultType::Handled,
		  "vips answers in the console");
	CheckEqInt(g_handlerCalls, 1, "the vips handler ran once in the console");
	Check(g_commandCallback(6, s2sdk::ConCommandContext::Chat, Args({"vips"})) ==
				  s2sdk::ResultType::Handled,
		  "vips answers in chat");
	CheckEqInt(g_handlerCalls, 2, "the vips handler ran once in chat");
	CheckEqInt(g_lastCaller, 6, "the chat caller is forwarded");

	// An unknown command is left to the engine; empty args are safe.
	Check(g_commandCallback(6, s2sdk::ConCommandContext::Chat, Args({"nosuchcommand"})) ==
				  s2sdk::ResultType::Continue,
		  "an unknown command is Continue");
	Check(g_commandCallback(6, s2sdk::ConCommandContext::Chat, plg::vector<plg::string>{}) ==
				  s2sdk::ResultType::Continue,
		  "empty command args are Continue");

	// The bare chat word through the say/say_team listener.
	const s2sdk::ConCommandCallback sayCallback = g_listeners[0].second;
	const s2sdk::ConCommandCallback sayTeamCallback = g_listeners[1].second;
	g_handlerCalls = 0;
	g_lastCaller = -99;
	Check(sayCallback(7, s2sdk::ConCommandContext::Chat, Args({"say", "vips"})) ==
				  s2sdk::ResultType::Handled,
		  "say vips answers with Handled");
	CheckEqInt(g_handlerCalls, 1, "the bare word ran the handler");
	CheckEqInt(g_lastCaller, 7, "the say caller is forwarded");
	Check(sayTeamCallback(8, s2sdk::ConCommandContext::Chat, Args({"say_team", "VIPS"})) ==
				  s2sdk::ResultType::Handled,
		  "say_team VIPS answers case-insensitively");
	CheckEqInt(g_lastCaller, 8, "the say_team caller is forwarded");
	Check(sayCallback(-1, s2sdk::ConCommandContext::Chat, Args({"say", "vips", "top"})) ==
				  s2sdk::ResultType::Handled,
		  "a console-style say with several words still matches the first word");

	g_handlerCalls = 0;
	Check(sayCallback(7, s2sdk::ConCommandContext::Chat, Args({"say", "vips top", "x"})) ==
				  s2sdk::ResultType::Continue,
		  "an already-tokenised quoted first argument is not split again");
	Check(sayCallback(7, s2sdk::ConCommandContext::Chat, Args({"say", "!vips"})) ==
				  s2sdk::ResultType::Continue,
		  "say !vips is left to the chat command (no double handling)");
	Check(sayCallback(7, s2sdk::ConCommandContext::Chat, Args({"say", "top vips"})) ==
				  s2sdk::ResultType::Continue,
		  "a later word does not trigger the word");
	Check(sayCallback(7, s2sdk::ConCommandContext::Chat, Args({"say"})) ==
				  s2sdk::ResultType::Continue,
		  "a say line without a message is Continue");
	CheckEqInt(g_handlerCalls, 0, "the non-matching lines never ran the handler");

	// OnPluginEnd: every name and listener is detached (PITFALLS A7). "mm_vips" already existed,
	// so only its listener may be removed - never RemoveCommand on someone else's command.
	vips::RemoveCommands();
	CheckEqInt(g_removedCommands, 2, "the two owned commands are removed with RemoveCommand");
	CheckEqInt(g_removedListeners, 3, "mm_vips and the say/say_team listeners are detached");
	vips::RemoveCommands();
	CheckEqInt(g_removedCommands, 2, "RemoveCommands is idempotent (commands)");
	CheckEqInt(g_removedListeners, 3, "RemoveCommands is idempotent (listeners)");
}

void ShutdownFakes() {
	vips::RemoveCommands();
	vips::ShutdownMapGate();
	CheckEqInt(g_mapStartUnregistrations, 1, "the OnMapStart listener is unregistered once");
	CheckEqInt(g_mapEndUnregistrations, 1, "the OnMapEnd listener is unregistered once");
	Check(!vips::IsMapReady(), "the gate is shut after shutdown");
}

void PrintSummary() {
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
}

} // namespace

int main() {
	TestKv1();
	TestColorize();
	TestFormatPhrase();
	TestChatWords();
	TestConfig();
	TestMapGateAndPawn();
	TestCommands();
	ShutdownFakes();
	PrintSummary();
	return g_failures == 0 ? 0 : 1;
}
