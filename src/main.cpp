#include "Version.h"

#include "Hint.h"
#include "Hooks.h"
#include "Input.h"
#include "Localization.h"
#include "Session.h"
#include "Settings.h"
#include "SettingsMenu.h"

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* a_message)
{
	switch (a_message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		// Settings resolve the required perk, so they need the forms loaded.
		ProjectileControl::Settings::Load();
		ProjectileControl::Localization::Load();
		ProjectileControl::Input::Register();
		ProjectileControl::Hooks::Install();
		ProjectileControl::Hint::Install();
		// Optional: registers nothing unless SKSE Menu Framework is installed.
		ProjectileControl::SettingsMenu::Register();
		break;

	case SKSE::MessagingInterface::kPreLoadGame:
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		// A ride cannot survive a save load: the arrow it was following is gone and the
		// camera state stack has been rebuilt underneath it.
		ProjectileControl::Session::Stop("game loaded"sv);
		break;

	default:
		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "STB_ProjectileControl";
	a_info->version = 1;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(1);
	v.PluginName("STB_ProjectileControl");
	v.AuthorName("STB");

	// Every address goes through the Address Library (the VTABLE_ constants), so the
	// plugin is not tied to one runtime build.
	v.UsesAddressLibrary(true);

	// Projectile runtime data moved at 1.6.629; CommonLibSSE-NG resolves the per-runtime
	// offset in GetProjectileRuntimeData. SE 1.5.97 still loads through the query above.
	v.UsesStructsPost629(true);

	// CompatibleVersions is deliberately NOT set: a non-empty list is a strict whitelist,
	// and RUNTIME_SSE_LATEST would lock out AE 1.7.x.

	return v;
}();

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("{} {}", Version::PROJECT, Version::NAME);
	SKSE::Init(a_skse);

	if (REL::Module::IsVR()) {
		// The camera state layout and the projectile vtable slots this plugin writes are
		// verified on SE and AE only.
		logger::error("Skyrim VR is not supported, the plugin stays inactive");
		return true;
	}

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		return false;
	}

	return true;
}
