#include "Settings.h"

#include <SimpleIni.hpp>

namespace ProjectileControl
{
	namespace
	{
		constexpr auto kIniPath = L"Data/SKSE/Plugins/STB_ProjectileControl.ini";

		float GetFloat(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, float a_default)
		{
			return static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, static_cast<double>(a_default)));
		}

		std::uint32_t GetKey(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t a_default)
		{
			return static_cast<std::uint32_t>(a_ini.GetLongValue(a_section, a_key, static_cast<long>(a_default)));
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\"");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\"");
			return std::string(a_text.substr(first, last - first + 1));
		}

		Settings::CostPool ParsePool(std::string_view a_text)
		{
			std::string lower = Trim(a_text);
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower == "stamina") {
				return Settings::CostPool::kStamina;
			}
			if (lower == "magicka") {
				return Settings::CostPool::kMagicka;
			}
			if (lower == "health") {
				return Settings::CostPool::kHealth;
			}
			if (!lower.empty() && lower != "none") {
				logger::warn("sActorValue '{}' is not Stamina, Magicka, Health or None; cost is off", lower);
			}
			return Settings::CostPool::kNone;
		}

		const char* PoolName(Settings::CostPool a_pool)
		{
			switch (a_pool) {
			case Settings::CostPool::kStamina:
				return "Stamina";
			case Settings::CostPool::kMagicka:
				return "Magicka";
			case Settings::CostPool::kHealth:
				return "Health";
			default:
				return "None";
			}
		}

		void ReadCost(const CSimpleIniA& a_ini, const char* a_section, const char* a_defaultPool, Settings::Cost& a_cost)
		{
			a_cost.pool = ParsePool(a_ini.GetValue(a_section, "sActorValue", a_defaultPool));
			a_cost.start = GetFloat(a_ini, a_section, "fStartCost", a_cost.start);
			a_cost.perSecond = GetFloat(a_ini, a_section, "fPerSecond", a_cost.perSecond);
			a_cost.cancelOnShortage = a_ini.GetBoolValue(a_section, "bCancelOnShortage", a_cost.cancelOnShortage);
		}

		// ---- writing: values only, the file's comments are kept by SimpleIni ----

		void PutFloat(CSimpleIniA& a_ini, const char* a_section, const char* a_key, float a_value)
		{
			// Shortest text that reads back to the same float: 0.12, not 0.120000.
			a_ini.SetValue(a_section, a_key, fmt::format("{}", a_value).c_str());
		}

		void PutKey(CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t a_value)
		{
			a_ini.SetValue(a_section, a_key, fmt::format("0x{:X}", a_value).c_str());
		}

		void PutBool(CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool a_value)
		{
			a_ini.SetValue(a_section, a_key, a_value ? "1" : "0");
		}

		void PutCost(CSimpleIniA& a_ini, const char* a_section, const Settings::Cost& a_cost)
		{
			a_ini.SetValue(a_section, "sActorValue", PoolName(a_cost.pool));
			PutFloat(a_ini, a_section, "fStartCost", a_cost.start);
			PutFloat(a_ini, a_section, "fPerSecond", a_cost.perSecond);
			PutBool(a_ini, a_section, "bCancelOnShortage", a_cost.cancelOnShortage);
		}

		// Comma or semicolon separated, like the keep-keyword list in STB Quick Sell.
		std::vector<std::string> SplitList(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (start <= a_text.size()) {
				const auto  stop = a_text.find_first_of(",;", start);
				std::string piece = Trim(a_text.substr(start, stop == std::string_view::npos ? stop : stop - start));
				if (!piece.empty()) {
					out.push_back(std::move(piece));
				}
				if (stop == std::string_view::npos) {
					break;
				}
				start = stop + 1;
			}
			return out;
		}

		// "Plugin.esp|0x00ABCD" (load-order independent, always works) or a bare editor id.
		// Editor ids are only kept in memory with a plugin that retains them (powerofthree's
		// Tweaks); so both are accepted.
		RE::TESForm* ResolveForm(std::string_view a_text)
		{
			const std::string text = Trim(a_text);
			if (text.empty()) {
				return nullptr;
			}

			const auto bar = text.find('|');
			if (bar == std::string::npos) {
				return RE::TESForm::LookupByEditorID(text);
			}

			const std::string plugin = Trim(std::string_view(text).substr(0, bar));
			const std::string id = Trim(std::string_view(text).substr(bar + 1));
			char*             end = nullptr;
			const auto        rawID = static_cast<RE::FormID>(std::strtoul(id.c_str(), &end, 16));
			auto*             data = RE::TESDataHandler::GetSingleton();
			const auto*       file = data ? data->LookupModByName(plugin) : nullptr;
			if (!file || !end || *end != '\0') {
				return nullptr;
			}
			// LookupForm adds the id to the plugin's index without masking it, so the full id
			// xEdit shows (load-order byte included) would land on a wrong form. Keep only the
			// plugin-local part: 12 bits in a light plugin.
			const RE::FormID localID = rawID & (file->IsLight() ? 0xFFFu : 0xFFFFFFu);
			return data->LookupForm(localID, plugin);
		}

		RE::BGSPerk* ResolvePerk(std::string_view a_text, const char* a_what)
		{
			const std::string text = Trim(a_text);
			if (text.empty()) {
				return nullptr;
			}

			auto* form = ResolveForm(text);
			auto* perk = form ? form->As<RE::BGSPerk>() : nullptr;
			if (perk) {
				logger::info("perk required for {}: {} ({:08X})", a_what, perk->GetName() ? perk->GetName() : "",
					perk->GetFormID());
			} else {
				// Fail open, loudly: a typo must not silently lock the whole mod away.
				logger::error("perk '{}' for {} not found -- that ride is NOT gated on a perk", text, a_what);
			}
			return perk;
		}
	}

	void Settings::Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kIniPath) < 0) {
			logger::info("no INI found, using defaults");
			Apply();
			return;
		}

		auto& v = _v;

		v.triggerKeyboard = GetKey(ini, "Keys", "iTriggerKeyboard", v.triggerKeyboard);
		v.triggerGamepad = GetKey(ini, "Keys", "iTriggerGamepad", v.triggerGamepad);
		v.cancelKeyboard = GetKey(ini, "Keys", "iCancelKeyboard", v.cancelKeyboard);
		v.cancelGamepad = GetKey(ini, "Keys", "iCancelGamepad", v.cancelGamepad);
		v.mouseSteerKey = GetKey(ini, "Keys", "iMouseSteerKey", v.mouseSteerKey);
		v.slowerKey = GetKey(ini, "Keys", "iSlowerKey", v.slowerKey);
		v.fasterKey = GetKey(ini, "Keys", "iFasterKey", v.fasterKey);

		v.arrows = ini.GetBoolValue("Projectiles", "bArrows", v.arrows);
		v.bolts = ini.GetBoolValue("Projectiles", "bBolts", v.bolts);
		v.spells = ini.GetBoolValue("Projectiles", "bSpells", v.spells);
		v.alwaysRide = Trim(ini.GetValue("Projectiles", "sAlwaysRide", ""));

		v.showHint = ini.GetBoolValue("Hint", "bShow", v.showHint);
		v.hintShowSteer = ini.GetBoolValue("Hint", "bShowSteer", v.hintShowSteer);
		v.hintScale = GetFloat(ini, "Hint", "fScale", v.hintScale);
		v.hintFontSize = GetFloat(ini, "Hint", "fFontSize", v.hintFontSize);
		v.hintBottom = GetFloat(ini, "Hint", "fBottom", v.hintBottom);

		ReadCost(ini, "Cost", "Stamina", v.arrowCost);
		ReadCost(ini, "CostSpells", "Magicka", v.spellCost);

		v.arrowPerk = Trim(ini.GetValue("Requirements", "sPerk", ""));
		v.spellPerk = Trim(ini.GetValue("Requirements", "sPerkSpells", ""));

		v.distance = GetFloat(ini, "Camera", "fDistance", v.distance);
		v.height = GetFloat(ini, "Camera", "fHeight", v.height);
		v.followLag = GetFloat(ini, "Camera", "fFollowLag", v.followLag);
		v.bankAngle = GetFloat(ini, "Camera", "fBankAngle", v.bankAngle);
		v.bankLag = GetFloat(ini, "Camera", "fBankLag", v.bankLag);
		v.mouseLook = ini.GetBoolValue("Camera", "bMouseLook", v.mouseLook);
		v.mouseSensitivity = GetFloat(ini, "Camera", "fMouseSensitivity", v.mouseSensitivity);
		v.invertMouseY = ini.GetBoolValue("Camera", "bInvertMouseY", v.invertMouseY);
		v.mousePitchLimit = GetFloat(ini, "Camera", "fMousePitchLimit", v.mousePitchLimit);
		v.recenterSpeed = GetFloat(ini, "Camera", "fRecenterSpeed", v.recenterSpeed);
		v.recenterDelay = GetFloat(ini, "Camera", "fRecenterDelay", v.recenterDelay);

		v.impactLinger = GetFloat(ini, "Impact", "fLinger", v.impactLinger);
		v.lingerEaseTime = ini.GetBoolValue("Impact", "bEaseTimeBack", v.lingerEaseTime);

		v.turnRate = GetFloat(ini, "Steering", "fTurnRate", v.turnRate);
		v.turnResponse = GetFloat(ini, "Steering", "fTurnResponse", v.turnResponse);
		v.invertSteerY = ini.GetBoolValue("Steering", "bInvertY", v.invertSteerY);
		v.pitchLimit = GetFloat(ini, "Steering", "fPitchLimit", v.pitchLimit);
		v.allowLoops = ini.GetBoolValue("Steering", "bAllowLoops", v.allowLoops);
		v.mouseSteerSensitivity = GetFloat(ini, "Steering", "fMouseSensitivity", v.mouseSteerSensitivity);
		v.gravityScale = GetFloat(ini, "Steering", "fGravityScale", v.gravityScale);
		v.speedScale = GetFloat(ini, "Steering", "fSpeedScale", v.speedScale);

		v.worldScale = GetFloat(ini, "Time", "fWorldScale", v.worldScale);
		v.playerScale = GetFloat(ini, "Time", "fPlayerScale", v.playerScale);

		v.maxDistance = GetFloat(ini, "Range", "fMaxDistance", v.maxDistance);
		v.lifeScale = GetFloat(ini, "Range", "fLifeScale", v.lifeScale);

		v.maxDuration = GetFloat(ini, "General", "fMaxDurationRealSeconds", v.maxDuration);
		v.debugLog = ini.GetBoolValue("General", "bDebugLog", v.debugLog);

		Apply();
	}

	void Settings::Clamp()
	{
		auto& v = _v;
		// The engine clamps hard below 0.01 anyway, and a 0 scale would stop the arrow the
		// camera is riding -- the session would then only end on the safety backstop.
		v.worldScale = std::clamp(v.worldScale, 0.01f, 1.f);
		v.playerScale = std::clamp(v.playerScale, 0.01f, 1.f);
		v.pitchLimit = std::clamp(v.pitchLimit, 0.f, 89.f);
		v.mousePitchLimit = std::clamp(v.mousePitchLimit, 0.f, 89.f);
		v.mouseSteerSensitivity = std::clamp(v.mouseSteerSensitivity, 0.01f, 2.f);
		// Parenthesised: windows.h brings a max macro in through the engine headers.
		v.maxDuration = (std::max)(v.maxDuration, 1.f);
		v.followLag = (std::max)(v.followLag, 0.f);
		v.bankLag = (std::max)(v.bankLag, 0.f);
		v.bankAngle = std::clamp(v.bankAngle, -60.f, 60.f);
		v.turnResponse = (std::max)(v.turnResponse, 0.f);
		v.recenterDelay = (std::max)(v.recenterDelay, 0.f);
		v.impactLinger = std::clamp(v.impactLinger, 0.f, 10.f);
		v.maxDistance = (std::max)(v.maxDistance, 0.f);
		v.lifeScale = std::clamp(v.lifeScale, 0.f, 100.f);
		v.hintScale = std::clamp(v.hintScale, 10.f, 400.f);
		v.hintFontSize = std::clamp(v.hintFontSize, 6.f, 72.f);
		for (auto* cost : { &v.arrowCost, &v.spellCost }) {
			cost->start = (std::max)(cost->start, 0.f);
			cost->perSecond = (std::max)(cost->perSecond, 0.f);
		}
	}

	void Settings::ResolveForms()
	{
		std::unordered_set<RE::FormID> always;
		for (const auto& entry : SplitList(_v.alwaysRide)) {
			if (auto* form = ResolveForm(entry)) {
				always.insert(form->GetFormID());
				logger::info("always rides, no key needed: {} ({:08X})", entry, form->GetFormID());
			} else {
				logger::error("sAlwaysRide entry '{}' not found, skipped", entry);
			}
		}
		_alwaysRide = std::move(always);

		_arrowPerk = ResolvePerk(_v.arrowPerk, "arrows");
		_spellPerk = ResolvePerk(_v.spellPerk, "spells");
	}

	void Settings::Apply(bool a_resolveForms)
	{
		Clamp();
		if (a_resolveForms) {
			ResolveForms();
		}

		const auto level = _v.debugLog ? spdlog::level::debug : spdlog::level::info;
		spdlog::set_level(level);
		spdlog::flush_on(level);

		Touch();
	}

	void Settings::SetTimeScale(float a_world, float a_player)
	{
		_v.worldScale = std::clamp(a_world, 0.01f, 1.f);
		_v.playerScale = std::clamp(a_player, 0.01f, 1.f);
		Touch();
	}

	void Settings::Save()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		// A missing file is fine: SaveFile creates it, just without the explanations.
		ini.LoadFile(kIniPath);

		const auto& v = _v;

		PutKey(ini, "Keys", "iTriggerKeyboard", v.triggerKeyboard);
		PutKey(ini, "Keys", "iTriggerGamepad", v.triggerGamepad);
		PutKey(ini, "Keys", "iCancelKeyboard", v.cancelKeyboard);
		PutKey(ini, "Keys", "iCancelGamepad", v.cancelGamepad);
		PutKey(ini, "Keys", "iMouseSteerKey", v.mouseSteerKey);
		PutKey(ini, "Keys", "iSlowerKey", v.slowerKey);
		PutKey(ini, "Keys", "iFasterKey", v.fasterKey);

		PutBool(ini, "Projectiles", "bArrows", v.arrows);
		PutBool(ini, "Projectiles", "bBolts", v.bolts);
		PutBool(ini, "Projectiles", "bSpells", v.spells);
		ini.SetValue("Projectiles", "sAlwaysRide", v.alwaysRide.c_str());

		PutBool(ini, "Hint", "bShow", v.showHint);
		PutBool(ini, "Hint", "bShowSteer", v.hintShowSteer);
		PutFloat(ini, "Hint", "fScale", v.hintScale);
		PutFloat(ini, "Hint", "fFontSize", v.hintFontSize);
		PutFloat(ini, "Hint", "fBottom", v.hintBottom);

		PutFloat(ini, "Camera", "fDistance", v.distance);
		PutFloat(ini, "Camera", "fHeight", v.height);
		PutFloat(ini, "Camera", "fFollowLag", v.followLag);
		PutFloat(ini, "Camera", "fBankAngle", v.bankAngle);
		PutFloat(ini, "Camera", "fBankLag", v.bankLag);
		PutBool(ini, "Camera", "bMouseLook", v.mouseLook);
		PutFloat(ini, "Camera", "fMouseSensitivity", v.mouseSensitivity);
		PutBool(ini, "Camera", "bInvertMouseY", v.invertMouseY);
		PutFloat(ini, "Camera", "fMousePitchLimit", v.mousePitchLimit);
		PutFloat(ini, "Camera", "fRecenterSpeed", v.recenterSpeed);
		PutFloat(ini, "Camera", "fRecenterDelay", v.recenterDelay);

		PutFloat(ini, "Steering", "fTurnRate", v.turnRate);
		PutFloat(ini, "Steering", "fTurnResponse", v.turnResponse);
		PutBool(ini, "Steering", "bInvertY", v.invertSteerY);
		PutFloat(ini, "Steering", "fPitchLimit", v.pitchLimit);
		PutBool(ini, "Steering", "bAllowLoops", v.allowLoops);
		PutFloat(ini, "Steering", "fMouseSensitivity", v.mouseSteerSensitivity);
		PutFloat(ini, "Steering", "fGravityScale", v.gravityScale);
		PutFloat(ini, "Steering", "fSpeedScale", v.speedScale);

		PutFloat(ini, "Impact", "fLinger", v.impactLinger);
		PutBool(ini, "Impact", "bEaseTimeBack", v.lingerEaseTime);

		PutCost(ini, "Cost", v.arrowCost);
		PutCost(ini, "CostSpells", v.spellCost);

		ini.SetValue("Requirements", "sPerk", v.arrowPerk.c_str());
		ini.SetValue("Requirements", "sPerkSpells", v.spellPerk.c_str());

		PutFloat(ini, "Time", "fWorldScale", v.worldScale);
		PutFloat(ini, "Time", "fPlayerScale", v.playerScale);

		PutFloat(ini, "Range", "fMaxDistance", v.maxDistance);
		PutFloat(ini, "Range", "fLifeScale", v.lifeScale);

		PutFloat(ini, "General", "fMaxDurationRealSeconds", v.maxDuration);
		PutBool(ini, "General", "bDebugLog", v.debugLog);

		// No UTF-8 signature: SimpleIni adds one by default, and the file did not have it.
		if (ini.SaveFile(kIniPath, false) < 0) {
			logger::error("could not write the INI");
		} else {
			logger::debug("INI saved");
		}
	}
}
