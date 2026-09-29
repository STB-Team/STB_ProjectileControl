#include "SettingsMenu.h"

#include "Localization.h"
#include "Settings.h"

#include <filesystem>
#include <span>

#pragma warning(push)
#pragma warning(disable: 4996)  // codecvt inside the framework header
#include "SKSEMenuFramework.h"
#pragma warning(pop)

namespace ProjectileControl
{
	namespace
	{
		namespace ig = ImGuiMCP;

		// ---- translation ----------------------------------------------------------------
		//
		// Every string on the pages goes through the mod's own string files
		// (Interface/STB_ProjectileControl_<language>.txt, tokens $STB_PC_M_*). The English
		// sits next to the code that shows it and is used when the file has no entry.
		// Results are cached: the tables do not change after load, and ImGui wants char
		// pointers that stay valid. Translated text is only ever passed as a "%s" argument,
		// never as a format -- a '%' in a translation would otherwise break the output.
		//
		// The framework's font only has the glyphs its own INI asks for: out of the box that
		// is SkyrimMenuFont.ttf with no Cyrillic, and every missing letter is drawn as '?'.
		// A translation the current font cannot draw in full is replaced by the English.

		bool FontCanDraw(std::string_view a_text)
		{
			auto* font = ig::GetFont();
			if (!font) {
				return true;
			}
			for (std::size_t i = 0; i < a_text.size();) {
				const auto lead = static_cast<unsigned char>(a_text[i]);
				const std::size_t len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
				if (i + len > a_text.size()) {
					return false;
				}
				std::uint32_t cp = len == 1 ? lead : len == 2 ? (lead & 0x1Fu) : len == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
				for (std::size_t k = 1; k < len; ++k) {
					cp = (cp << 6) | (static_cast<unsigned char>(a_text[i + k]) & 0x3Fu);
				}
				i += len;
				if (cp < 0x80) {
					continue;
				}
				if (cp > 0xFFFF || !ig::ImFontManger::FindGlyphNoFallback(font, static_cast<ig::ImWchar>(cp))) {
					return false;
				}
			}
			return true;
		}

		const char* T(const char* a_token, const char* a_english)
		{
			static std::unordered_map<std::string, std::string> cache;
			auto [it, inserted] = cache.try_emplace(a_token);
			if (inserted) {
				it->second = Localization::Get(a_token, a_english);
				if (!FontCanDraw(it->second)) {
					static bool logged = false;
					if (!std::exchange(logged, true)) {
						SKSE::log::info("SKSE Menu Framework's font cannot draw the translated menu; showing English. "
										"Enable the language's glyphs in SKSEMenuFramework.ini [Fonts].");
					}
					it->second = a_english;
				}
			}
			return it->second.c_str();
		}

		// The same text as an ImGui label whose id is the token, so widget state does not
		// depend on the language.
		const char* L(const char* a_token, const char* a_english)
		{
			static std::unordered_map<std::string, std::string> cache;
			auto [it, inserted] = cache.try_emplace(a_token);
			if (inserted) {
				it->second = std::string(T(a_token, a_english)) + "###" + a_token;
			}
			return it->second.c_str();
		}

		// A slider format with a translated unit: "%.2f" + " s". Any '%' in the unit is
		// escaped, since the result is a printf format.
		const char* UnitFormat(const char* a_number, const char* a_token, const char* a_english)
		{
			static std::unordered_map<std::string, std::string> cache;
			auto [it, inserted] = cache.try_emplace(std::string(a_number) + a_token);
			if (inserted) {
				std::string unit;
				for (const char c : std::string_view(T(a_token, a_english))) {
					unit += c;
					if (c == '%') {
						unit += '%';
					}
				}
				it->second = std::string(a_number) + " " + unit;
			}
			return it->second.c_str();
		}

		const char* Seconds(const char* a_number) { return UnitFormat(a_number, "$STB_PC_M_UnitSec", "s"); }
		const char* DegPerSec(const char* a_number) { return UnitFormat(a_number, "$STB_PC_M_UnitDegSec", "°/s"); }

		// ---- key names -----------------------------------------------------------------

		std::string KeyboardName(std::uint32_t a_code)
		{
			if (a_code >= Settings::kMouseBase && a_code < Settings::kMouseBase + 8) {
				static constexpr const char* kMouse[] = { "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4",
					"Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8" };
				return kMouse[a_code - Settings::kMouseBase];
			}
			struct Named
			{
				std::uint32_t code;
				const char*   name;
			};
			static constexpr Named kNamed[] = {
				{ 0x01, "Esc" }, { 0x0C, "-" }, { 0x0D, "=" }, { 0x0E, "Backspace" }, { 0x0F, "Tab" },
				{ 0x1A, "[" }, { 0x1B, "]" }, { 0x1C, "Enter" }, { 0x1D, "Left Ctrl" }, { 0x27, ";" },
				{ 0x28, "'" }, { 0x29, "`" }, { 0x2A, "Left Shift" }, { 0x2B, "\\" }, { 0x33, "," },
				{ 0x34, "." }, { 0x35, "/" }, { 0x36, "Right Shift" }, { 0x37, "Num *" }, { 0x38, "Left Alt" },
				{ 0x39, "Space" }, { 0x3A, "Caps Lock" }, { 0x45, "Num Lock" }, { 0x46, "Scroll Lock" },
				{ 0x4A, "Num -" }, { 0x4E, "Num +" }, { 0x53, "Num ." }, { 0x57, "F11" }, { 0x58, "F12" },
				{ 0x9C, "Num Enter" }, { 0x9D, "Right Ctrl" }, { 0xB5, "Num /" }, { 0xB8, "Right Alt" },
				{ 0xC7, "Home" }, { 0xC8, "Up" }, { 0xC9, "Page Up" }, { 0xCB, "Left" }, { 0xCD, "Right" },
				{ 0xCF, "End" }, { 0xD0, "Down" }, { 0xD1, "Page Down" }, { 0xD2, "Insert" }, { 0xD3, "Delete" },
			};
			for (const auto& [code, name] : kNamed) {
				if (code == a_code) {
					return name;
				}
			}
			// The rows of letters and digits, in DirectInput order.
			const auto row = [a_code](std::uint32_t a_first, const char* a_keys) -> std::string {
				const auto count = static_cast<std::uint32_t>(std::char_traits<char>::length(a_keys));
				return a_code >= a_first && a_code < a_first + count ? std::string(1, a_keys[a_code - a_first]) : "";
			};
			for (auto name : { row(0x02, "1234567890"), row(0x10, "QWERTYUIOP"), row(0x1E, "ASDFGHJKL"),
					 row(0x2C, "ZXCVBNM") }) {
				if (!name.empty()) {
					return name;
				}
			}
			if (a_code >= 0x3B && a_code <= 0x44) {
				return fmt::format("F{}", a_code - 0x3B + 1);
			}
			if (a_code >= 0x47 && a_code <= 0x52) {
				static constexpr const char* kPad[] = { "7", "8", "9", "", "4", "5", "6", "", "1", "2", "3", "0" };
				if (const char* digit = kPad[a_code - 0x47]; *digit) {
					return fmt::format("Num {}", digit);
				}
			}
			return fmt::format("0x{:X}", a_code);
		}

		std::string GamepadName(std::uint32_t a_mask)
		{
			struct Named
			{
				std::uint32_t mask;
				const char*   name;
			};
			static constexpr Named kNamed[] = {
				{ 0x0001, "D-pad Up" }, { 0x0002, "D-pad Down" }, { 0x0004, "D-pad Left" }, { 0x0008, "D-pad Right" },
				{ 0x0010, "Start" }, { 0x0020, "Back" }, { 0x0040, "Left Stick" }, { 0x0080, "Right Stick" },
				{ 0x0100, "LB" }, { 0x0200, "RB" }, { 0x1000, "A" }, { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" },
				{ 0x0009, "LT" }, { 0x000A, "RT" },
			};
			for (const auto& [mask, name] : kNamed) {
				if (mask == a_mask) {
					return name;
				}
			}
			return fmt::format("0x{:X}", a_mask);
		}

		// ---- key capture ---------------------------------------------------------------
		//
		// "Bind" arms a capture; the next key (or gamepad button, for the gamepad rows) that
		// the framework's input hook sees is written into the setting. Esc cancels. The
		// hook runs off the render thread, hence the atomics.

		enum Capture : int
		{
			kNone,
			kTriggerKeyboard,
			kTriggerGamepad,
			kCancelKeyboard,
			kCancelGamepad,
			kMouseSteer,  // keyboard or a mouse button
			kSlower,
			kFaster,
			kCaptureCount
		};

		std::array<std::uint32_t*, kCaptureCount> g_targets{};
		std::atomic<int>                          g_capture{ kNone };
		std::atomic<bool>                         g_captured{ false };

		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			const int which = g_capture.load();
			if (which == kNone) {
				return false;
			}
			const bool wantPad = which == kTriggerGamepad || which == kCancelGamepad;

			for (auto* event = a_event; event; event = event->next) {
				const auto* button = event->AsButtonEvent();
				if (!button || !button->IsDown()) {
					continue;
				}
				const auto device = button->GetDevice();
				if (device == RE::INPUT_DEVICE::kKeyboard && button->GetIDCode() == 0x01) {
					g_capture.store(kNone);
					return true;
				}
				const bool isPad = device == RE::INPUT_DEVICE::kGamepad;
				// Mouse buttons only for the one row that takes them (and not the wheel,
				// which reports as buttons 8 and 9). Everywhere else the mouse stays free.
				if (device == RE::INPUT_DEVICE::kMouse && which == kMouseSteer && button->GetIDCode() < 8) {
					*g_targets[which] = Settings::kMouseBase + button->GetIDCode();
					g_capture.store(kNone);
					g_captured.store(true);
					return true;
				}
				if (device != RE::INPUT_DEVICE::kKeyboard && !isPad) {
					continue;
				}
				if (isPad != wantPad) {
					continue;
				}
				*g_targets[which] = button->GetIDCode();
				g_capture.store(kNone);
				g_captured.store(true);
				return true;
			}
			// While waiting, keys and buttons belong to the capture, not to the menu: Esc
			// must not close the page, a letter must not land in a text field.
			return a_event && a_event->GetDevice() != RE::INPUT_DEVICE::kMouse;
		}

		// ---- text fields, owned by the render thread ------------------------------------
		//
		// The authoritative strings live in Settings on the main thread. The menu edits its
		// own copies and hands a finished edit over as a task; after a reset the task asks
		// for the copies to be refreshed.

		std::array<char, 1024> g_alwaysBuf{};
		std::array<char, 256>  g_arrowPerkBuf{};
		std::array<char, 256>  g_spellPerkBuf{};
		std::atomic<bool>      g_reloadBuffers{ false };

		void Fill(std::span<char> a_buf, const std::string& a_text)
		{
			const auto n = (std::min)(a_text.size(), a_buf.size() - 1);
			std::copy_n(a_text.data(), n, a_buf.data());
			a_buf[n] = '\0';
		}

		// ---- save debouncing ------------------------------------------------------------

		bool g_dirty = false;  // render thread only

		void QueueCommit(bool a_resolveForms)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_resolveForms]() {
					Settings::Apply(a_resolveForms);
					Settings::Save();
				});
			}
		}

		// ---- widgets --------------------------------------------------------------------

		void Help(const char* a_text)
		{
			ig::SameLine();
			ig::TextDisabled("(?)");
			if (ig::IsItemHovered()) {
				ig::SetTooltip("%s", a_text);
			}
		}

		bool KeyRow(const char* a_label, std::uint32_t* a_value, Capture a_which, bool a_gamepad, bool a_mouse = false)
		{
			bool changed = false;
			ig::PushID(a_label);

			ig::Text("%s", a_label);
			ig::SameLine(200.f);

			const bool        waiting = g_capture.load() == a_which;
			const std::string shown =
				waiting ? std::string(a_gamepad ? T("$STB_PC_M_PressButton", "press a button... (Esc cancels)") :
			                          a_mouse   ? T("$STB_PC_M_PressKeyOrMouse", "press a key or mouse button... (Esc cancels)") :
			                                      T("$STB_PC_M_PressKey", "press a key... (Esc cancels)")) :
				*a_value == 0 ? std::string(T("$STB_PC_M_NotSet", "not set")) :
				a_gamepad     ? GamepadName(*a_value) :
			                    KeyboardName(*a_value);
			if (ig::Button(fmt::format("{}###bind", shown).c_str(), ig::ImVec2{ 230.f, 0.f })) {
				g_capture.store(waiting ? kNone : a_which);
			}
			ig::SameLine();
			if (ig::Button(L("$STB_PC_M_Clear", "Clear"))) {
				*a_value = 0;
				if (waiting) {
					g_capture.store(kNone);
				}
				changed = true;
			}

			ig::PopID();
			return changed;
		}

		bool PoolCombo(const char* a_label, Settings::CostPool* a_pool)
		{
			const char* pools[] = {
				T("$STB_PC_M_PoolNone", "None"),
				T("$STB_PC_M_PoolStamina", "Stamina"),
				T("$STB_PC_M_PoolMagicka", "Magicka"),
				T("$STB_PC_M_PoolHealth", "Health"),
			};
			int index = static_cast<int>(*a_pool);
			if (ig::Combo(a_label, &index, pools, 4)) {
				*a_pool = static_cast<Settings::CostPool>(index);
				return true;
			}
			return false;
		}

		constexpr auto kClamp = ig::ImGuiSliderFlags_AlwaysClamp;

		// ---- perk picker ----------------------------------------------------------------

		// Case folding for the search box: ASCII, plus the Cyrillic capitals in UTF-8, since
		// perk names come in the game's own language and tolower leaves those alone.
		// U+0410..U+041F -> U+0430..U+043F, U+0420..U+042F -> U+0440..U+044F, U+0401 -> U+0451.
		std::string Fold(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const auto c = static_cast<unsigned char>(a_text[i]);
				if (c == 0xD0 && i + 1 < a_text.size()) {
					const auto d = static_cast<unsigned char>(a_text[i + 1]);
					if (d >= 0x90 && d <= 0x9F) {
						out += static_cast<char>(0xD0);
						out += static_cast<char>(d + 0x20);
						++i;
						continue;
					}
					if (d >= 0xA0 && d <= 0xAF) {
						out += static_cast<char>(0xD1);
						out += static_cast<char>(d - 0x20);
						++i;
						continue;
					}
					if (d == 0x81) {
						out += static_cast<char>(0xD1);
						out += static_cast<char>(0x91);
						++i;
						continue;
					}
				}
				out += static_cast<char>(c < 0x80 ? std::tolower(c) : c);
			}
			return out;
		}

		// ---- pickable forms ---------------------------------------------------------------

		struct PickEntry
		{
			std::string label;    // "<name>  [Plugin.esp|0x000801]"
			std::string iniText;  // "Plugin.esp|0x000801" -- exactly what the INI takes
			std::string folded;   // label, case-folded for the search
		};

		enum Catalog : int
		{
			kPerks,
			kSpells,   // spells and scrolls
			kStaves,   // staff enchantments
			kAmmo,
			kWeapons,  // bows and crossbows
			kCatalogCount
		};

		// Built once on the main thread at registration, read-only after that.
		std::array<std::vector<PickEntry>, kCatalogCount> g_catalogs;
		// "Plugin|0xID" -> name, to show the always-ride list by name.
		std::unordered_map<std::string, std::string> g_nameByIni;

		// The "Plugin|local id" text the INI takes, so a pick is saved exactly as if it had
		// been typed.
		std::string IniTextOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form->GetFile(0);
			if (!file) {
				return {};
			}
			const RE::FormID local = a_form->GetFormID() & (file->IsLight() ? 0xFFFu : 0xFFFFFFu);
			return fmt::format("{}|0x{:06X}", file->GetFilename(), local);
		}

		// Named forms only: unnamed ones are technical, and can still be typed by hand.
		template <class T, class Filter>
		void Collect(Catalog a_into, Filter a_filter)
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			auto& list = g_catalogs[a_into];
			for (auto* form : data->GetFormArray<T>()) {
				const char* name = form ? form->GetName() : nullptr;
				if (!name || !*name || !a_filter(form)) {
					continue;
				}
				std::string ini = IniTextOf(form);
				if (ini.empty()) {
					continue;
				}
				std::string label = fmt::format("{}  [{}]", name, ini);
				std::string folded = Fold(label);
				if (a_into != kPerks) {
					g_nameByIni[ini] = name;
				}
				list.push_back({ std::move(label), std::move(ini), std::move(folded) });
			}
		}

		void BuildCatalogs()
		{
			for (auto& list : g_catalogs) {
				list.clear();
			}
			g_nameByIni.clear();

			const auto always = [](auto*) { return true; };
			// What the mod can take: aimed and cast in one go. Concentration and touch/self
			// magic never produce a projectile to ride; weapon enchantments are touch.
			const auto flies = [](RE::MagicItem* a_item) {
				return a_item->GetDelivery() == RE::MagicSystem::Delivery::kAimed &&
				       a_item->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget;
			};

			Collect<RE::BGSPerk>(kPerks, always);
			Collect<RE::SpellItem>(kSpells, flies);
			Collect<RE::ScrollItem>(kSpells, flies);
			Collect<RE::EnchantmentItem>(kStaves, flies);
			Collect<RE::TESAmmo>(kAmmo, always);
			Collect<RE::TESObjectWEAP>(kWeapons, [](RE::TESObjectWEAP* a_weap) {
				return a_weap->IsBow() || a_weap->IsCrossbow();
			});

			for (auto& list : g_catalogs) {
				std::sort(list.begin(), list.end(),
					[](const PickEntry& a, const PickEntry& b) { return a.folded < b.folded; });
			}
			logger::info("menu: {} perks, {} spells and scrolls, {} staves, {} ammo, {} bows and crossbows to pick",
				g_catalogs[kPerks].size(), g_catalogs[kSpells].size(), g_catalogs[kStaves].size(),
				g_catalogs[kAmmo].size(), g_catalogs[kWeapons].size());
		}

		// A combo with a search box on top. Returns true when an entry was picked; its INI
		// text is then in a_chosen. Entries in a_marked are shown as selected.
		bool Picker(const char* a_id, const char* a_preview, const std::vector<PickEntry>& a_list,
			const std::vector<std::string>& a_marked, std::string& a_chosen)
		{
			bool picked = false;
			ig::SetNextItemWidth(320.f);
			if (!ig::BeginCombo(a_id, a_preview, ig::ImGuiComboFlags_HeightLarge)) {
				return false;
			}

			static std::array<char, 96> filter{};
			if (ig::IsWindowAppearing()) {
				filter[0] = '\0';
				ig::SetKeyboardFocusHere();
			}
			ig::InputTextWithHint("##filter", T("$STB_PC_M_Search", "search by name or plugin"), filter.data(),
				filter.size());

			// A long load order has thousands of entries; drawing them all every frame is
			// pointless when nobody can scroll that far, so the list stops and asks for a
			// narrower search.
			constexpr int kMaxShown = 300;
			const auto    needle = Fold(filter.data());
			int           shown = 0;
			for (const auto& entry : a_list) {
				if (!needle.empty() && entry.folded.find(needle) == std::string::npos) {
					continue;
				}
				if (++shown > kMaxShown) {
					ig::TextDisabled("%s", T("$STB_PC_M_More", "... more, type to narrow the search"));
					break;
				}
				const bool marked = std::find(a_marked.begin(), a_marked.end(), entry.iniText) != a_marked.end();
				if (ig::Selectable(entry.label.c_str(), marked)) {
					a_chosen = entry.iniText;
					picked = true;
				}
			}
			if (shown == 0) {
				ig::TextDisabled("%s", a_list.empty() ? T("$STB_PC_M_NothingLoaded", "nothing of this kind is loaded") :
				                                        T("$STB_PC_M_NothingMatches", "nothing matches"));
			}

			ig::EndCombo();
			return picked;
		}

		// The always-ride list as it is kept in the INI: comma separated.
		std::vector<std::string> SplitEntries(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (start <= a_text.size()) {
				const auto stop = a_text.find_first_of(",;", start);
				auto       piece = a_text.substr(start, stop == std::string_view::npos ? stop : stop - start);
				const auto first = piece.find_first_not_of(" \t");
				if (first != std::string_view::npos) {
					const auto last = piece.find_last_not_of(" \t");
					out.emplace_back(piece.substr(first, last - first + 1));
				}
				if (stop == std::string_view::npos) {
					break;
				}
				start = stop + 1;
			}
			return out;
		}

		std::string JoinEntries(const std::vector<std::string>& a_entries)
		{
			std::string out;
			for (const auto& entry : a_entries) {
				if (!out.empty()) {
					out += ", ";
				}
				out += entry;
			}
			return out;
		}

		// ---- numbers --------------------------------------------------------------------

		// The number part of a slider format, for the typing box: "%.2f s" -> "%.2f".
		std::string InputFormat(const char* a_format)
		{
			const std::string_view format(a_format);
			const auto             start = format.find('%');
			const auto             end = start == std::string_view::npos ? start : format.find('f', start);
			return end == std::string_view::npos ? std::string("%.2f") : std::string(format.substr(start, end - start + 1));
		}

		// A slider with a box to type the number into, on one line, as STB Widgets does it.
		// The typed value is clamped only when the box is left: clamping on every keystroke
		// would turn the "1" of "150" into the minimum before the rest could be typed.
		bool Number(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format)
		{
			bool changed = false;
			ig::PushID(a_label);
			ig::SetNextItemWidth(230.f);
			changed |= ig::SliderFloat("##slider", a_value, a_min, a_max, a_format, kClamp);
			ig::SameLine();
			ig::SetNextItemWidth(80.f);
			changed |= ig::InputFloat("##typed", a_value, 0.f, 0.f, InputFormat(a_format).c_str());
			if (ig::IsItemDeactivatedAfterEdit()) {
				*a_value = std::clamp(*a_value, a_min, a_max);
				changed = true;
			}
			ig::SameLine();
			ig::Text("%s", a_label);
			ig::PopID();
			return changed;
		}
	}

	void SettingsMenu::Changed(bool a_changed)
	{
		if (a_changed) {
			g_dirty = true;
			Settings::Touch();
		}
		// Sliders report a change on every frame of a drag; the INI is written once the
		// control is let go.
		if (g_dirty && !ig::IsAnyItemActive()) {
			g_dirty = false;
			QueueCommit(false);
		}
	}

	void __stdcall SettingsMenu::RenderControls()
	{
		auto& v = Settings::_v;
		bool  changed = g_captured.exchange(false);

		ig::SeparatorText(T("$STB_PC_M_Keys", "Keys"));
		changed |= KeyRow(T("$STB_PC_M_Trigger", "Trigger"), &v.triggerKeyboard, kTriggerKeyboard, false);
		Help(T("$STB_PC_M_TriggerHelp", "Hold it as the arrow or spell leaves your hand to ride it."));
		changed |= KeyRow(T("$STB_PC_M_TriggerPad", "Trigger (gamepad)"), &v.triggerGamepad, kTriggerGamepad, true);
		changed |= KeyRow(T("$STB_PC_M_LetGo", "Let go"), &v.cancelKeyboard, kCancelKeyboard, false);
		Help(T("$STB_PC_M_LetGoHelp",
			"Ends the ride at once; the projectile flies on by itself.\nMay be the same key as the trigger."));
		changed |= KeyRow(T("$STB_PC_M_LetGoPad", "Let go (gamepad)"), &v.cancelGamepad, kCancelGamepad, true);
		changed |= KeyRow(T("$STB_PC_M_MouseSteer", "Steer with the mouse"), &v.mouseSteerKey, kMouseSteer, false, true);
		Help(T("$STB_PC_M_MouseSteerHelp",
			"Hold it and the mouse flies the projectile instead of turning the camera.\n"
			"Left mouse button by default: attacking is off for the length of the ride."));
		changed |= KeyRow(T("$STB_PC_M_Slower", "Slower time"), &v.slowerKey, kSlower, false);
		Help(T("$STB_PC_M_TimeKeysHelp",
			"One step of slow motion per press while you fly. The new value is kept,\n"
			"the same as setting it on the Impact & time page. Num - and Num + work as well."));
		changed |= KeyRow(T("$STB_PC_M_Faster", "Faster time"), &v.fasterKey, kFaster, false);
		changed |= ig::Checkbox(L("$STB_PC_M_InvertY", "Invert up / down"), &v.invertSteerY);
		Help(T("$STB_PC_M_InvertYHelp",
			"Off: W lifts the nose and S lowers it -- forward reads as up the screen.\n"
			"On: W dives and S climbs, the way an aircraft stick works."));

		ig::SeparatorText(T("$STB_PC_M_Hints", "Button hints"));
		changed |= ig::Checkbox(L("$STB_PC_M_ShowHint", "Show the hint row while flying"), &v.showHint);
		ig::BeginDisabled(!v.showHint);
		changed |= ig::Checkbox(L("$STB_PC_M_HintSteer", "Include the steering keys"), &v.hintShowSteer);
		changed |= Number(T("$STB_PC_M_KeycapSize", "Keycap size"), &v.hintScale, 25.f, 250.f, "%.0f%%");
		changed |= Number(T("$STB_PC_M_LabelSize", "Label size"), &v.hintFontSize, 10.f, 40.f, "%.0f");
		changed |= Number(T("$STB_PC_M_HintBottom", "Distance from bottom"), &v.hintBottom, 0.f, 400.f, "%.0f");
		ig::EndDisabled();

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderProjectiles()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		if (g_reloadBuffers.exchange(false)) {
			Fill(g_alwaysBuf, v.alwaysRide);
			Fill(g_arrowPerkBuf, v.arrowPerk);
			Fill(g_spellPerkBuf, v.spellPerk);
		}

		ig::SeparatorText(T("$STB_PC_M_TriggerTakes", "What the trigger can take"));
		changed |= ig::Checkbox(L("$STB_PC_M_Arrows", "Arrows"), &v.arrows);
		changed |= ig::Checkbox(L("$STB_PC_M_Bolts", "Crossbow bolts"), &v.bolts);
		changed |= ig::Checkbox(L("$STB_PC_M_Spells", "Spells"), &v.spells);
		Help(T("$STB_PC_M_SpellsHelp",
			"Fire-and-forget spells that fly, from hands, staves and scrolls.\n"
			"Concentration spells and cone shouts have nothing to steer."));

		ig::SeparatorText(T("$STB_PC_M_Always", "Always ride, no key needed"));
		Help(T("$STB_PC_M_AlwaysHelp",
			"Listed shots start a ride by themselves, without the trigger key --\n"
			"even if their kind is switched off above. Perks and costs still apply."));
		{
			auto entries = SplitEntries(g_alwaysBuf.data());
			bool edited = false;

			// Add: pick a kind, then a form of that kind. Each pick is appended.
			static int              kind = 0;
			static constexpr Catalog kKindCatalog[] = { kSpells, kStaves, kAmmo, kWeapons };
			const char*              kinds[] = {
                T("$STB_PC_M_KindSpells", "Spells and scrolls"),
                T("$STB_PC_M_KindStaves", "Staves"),
                T("$STB_PC_M_KindAmmo", "Ammo"),
                T("$STB_PC_M_KindWeapons", "Bows and crossbows"),
			};
			ig::SetNextItemWidth(170.f);
			ig::Combo("##kind", &kind, kinds, 4);
			ig::SameLine();
			std::string chosen;
			if (Picker("##add", T("$STB_PC_M_AddFromList", "Add from the list..."), g_catalogs[kKindCatalog[kind]],
					entries, chosen) &&
				std::find(entries.begin(), entries.end(), chosen) == entries.end()) {
				entries.push_back(chosen);
				edited = true;
			}

			// The list itself, one row per entry, each removable.
			if (entries.empty()) {
				ig::TextDisabled("%s", T("$STB_PC_M_AlwaysEmpty", "Empty -- only the trigger key starts a ride."));
			}
			for (std::size_t i = 0; i < entries.size(); ++i) {
				ig::PushID(static_cast<int>(i));
				if (ig::SmallButton(L("$STB_PC_M_Remove", "Remove"))) {
					entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
					edited = true;
					ig::PopID();
					break;
				}
				ig::SameLine();
				if (const auto it = g_nameByIni.find(entries[i]); it != g_nameByIni.end()) {
					ig::Text("%s", it->second.c_str());
					ig::SameLine();
					ig::TextDisabled("[%s]", entries[i].c_str());
				} else {
					ig::Text("%s", entries[i].c_str());
				}
				ig::PopID();
			}

			// By hand: forms without a name, projectiles, or a plugin not loaded yet.
			static std::array<char, 256> typed{};
			ig::SetNextItemWidth(320.f);
			const bool enter = ig::InputTextWithHint("##typed",
				T("$STB_PC_M_TypeHint", "or type Plugin.esp|0xFormID / an editor id"), typed.data(), typed.size(),
				ig::ImGuiInputTextFlags_EnterReturnsTrue);
			ig::SameLine();
			if ((ig::Button(L("$STB_PC_M_Add", "Add")) || enter) && typed[0] != '\0') {
				// Several at once, comma separated, work too.
				for (auto& text : SplitEntries(typed.data())) {
					if (std::find(entries.begin(), entries.end(), text) == entries.end()) {
						entries.push_back(std::move(text));
						edited = true;
					}
				}
				typed[0] = '\0';
			}

			if (edited) {
				const std::string joined = JoinEntries(entries);
				Fill(g_alwaysBuf, joined);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([joined]() {
						Settings::_v.alwaysRide = joined;
						Settings::Apply(true);
						Settings::Save();
					});
				}
			}
		}

		ig::SeparatorText(T("$STB_PC_M_Perks", "Required perks"));
		const auto perkField = [](const char* a_label, std::span<char> a_buf, RE::BGSPerk* a_resolved,
								   std::string Settings::Values::* a_field) {
			// The finished text goes to the main thread, which owns the string and the
			// resolved perk.
			const auto commit = [a_field](std::string a_text) {
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([text = std::move(a_text), a_field]() {
						Settings::_v.*a_field = text;
						Settings::Apply(true);
						Settings::Save();
					});
				}
			};

			ig::PushID(a_label);
			ig::Text("%s", a_label);
			ig::SameLine(160.f);
			if (a_buf[0] == '\0') {
				ig::TextDisabled("%s", T("$STB_PC_M_NoPerk", "no perk needed"));
			} else if (a_resolved) {
				ig::TextColored(ig::ImVec4{ 0.55f, 0.85f, 0.55f, 1.f }, "%s",
					a_resolved->GetName() && *a_resolved->GetName() ? a_resolved->GetName() :
																	  T("$STB_PC_M_Found", "found"));
			} else {
				ig::TextColored(ig::ImVec4{ 1.f, 0.45f, 0.4f, 1.f }, "%s", T("$STB_PC_M_NotFound", "not found"));
			}

			std::string chosen;
			if (Picker("##pick", T("$STB_PC_M_PickPerk", "Pick a perk from the list..."), g_catalogs[kPerks],
					{ std::string(a_buf.data()) }, chosen)) {
				Fill(a_buf, chosen);
				commit(chosen);
			}
			ig::SameLine();
			if (ig::Button(L("$STB_PC_M_Clear", "Clear"))) {
				a_buf[0] = '\0';
				commit({});
			}

			// Typed by hand: perks without a name are not in the list, and a perk from a
			// plugin loaded later can still be named ahead of time.
			ig::InputTextWithHint("##text", T("$STB_PC_M_TypeHint", "or type Plugin.esp|0xFormID / an editor id"),
				a_buf.data(), a_buf.size());
			if (ig::IsItemDeactivatedAfterEdit()) {
				commit(a_buf.data());
			}
			ig::PopID();
			ig::Spacing();
		};
		perkField(T("$STB_PC_M_ArrowsAndBolts", "Arrows and bolts"), g_arrowPerkBuf, Settings::ArrowPerk(),
			&Settings::Values::arrowPerk);
		perkField(T("$STB_PC_M_Spells", "Spells"), g_spellPerkBuf, Settings::SpellPerk(), &Settings::Values::spellPerk);

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderCamera()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		ig::SeparatorText(T("$STB_PC_M_Position", "Position"));
		changed |= Number(T("$STB_PC_M_Distance", "Distance behind"), &v.distance, -100.f, 400.f, "%.0f");
		Help(T("$STB_PC_M_DistanceHelp", "Below 0 the camera moves ahead of the projectile."));
		changed |= Number(T("$STB_PC_M_Height", "Height above"), &v.height, -50.f, 150.f, "%.0f");

		ig::SeparatorText(T("$STB_PC_M_Follow", "Follow"));
		changed |= Number(T("$STB_PC_M_FollowLag", "Follow lag"), &v.followLag, 0.f, 0.6f, Seconds("%.2f"));
		Help(T("$STB_PC_M_FollowLagHelp",
			"How long the view trails the projectile through a turn.\n0 locks it rigidly behind."));
		changed |= Number(T("$STB_PC_M_Bank", "Bank into turns"), &v.bankAngle, -45.f, 45.f, "%.0f°");
		Help(T("$STB_PC_M_BankHelp", "Negative leans the other way. 0 keeps the horizon level."));
		changed |= Number(T("$STB_PC_M_BankLag", "Bank lag"), &v.bankLag, 0.f, 0.6f, Seconds("%.2f"));

		ig::SeparatorText(T("$STB_PC_M_Mouse", "Mouse"));
		changed |= ig::Checkbox(L("$STB_PC_M_MouseLook", "Look around (mouse / right stick)"), &v.mouseLook);
		ig::BeginDisabled(!v.mouseLook);
		changed |= Number(T("$STB_PC_M_Sensitivity", "Sensitivity"), &v.mouseSensitivity, 0.05f, 2.f, "%.2f");
		changed |= ig::Checkbox(L("$STB_PC_M_InvertMouse", "Invert mouse Y"), &v.invertMouseY);
		changed |= Number(T("$STB_PC_M_TiltLimit", "Tilt limit"), &v.mousePitchLimit, 0.f, 89.f, "%.0f°");
		changed |= Number(T("$STB_PC_M_DriftBack", "Drift back"), &v.recenterSpeed, 0.f, 180.f, DegPerSec("%.0f"));
		Help(T("$STB_PC_M_DriftBackHelp",
			"How fast the view drifts back behind the projectile. 0 = stays where you put it."));
		ig::BeginDisabled(v.recenterSpeed <= 0.f);
		changed |= Number(T("$STB_PC_M_DriftDelay", "Drift back after"), &v.recenterDelay, 0.f, 5.f, Seconds("%.1f"));
		ig::EndDisabled();
		ig::EndDisabled();

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderSteering()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		changed |= Number(T("$STB_PC_M_TurnRate", "Turn rate"), &v.turnRate, 10.f, 360.f, DegPerSec("%.0f"));
		Help(T("$STB_PC_M_TurnRateHelp", "In real seconds, so it does not change with the slow motion."));
		changed |= Number(T("$STB_PC_M_TurnResponse", "Turn response"), &v.turnResponse, 0.f, 20.f, "%.1f");
		Help(T("$STB_PC_M_TurnResponseHelp",
			"How quickly a turn builds up and dies away. Around 6 flies smooth arcs;\n"
			"0 switches the full rate on and off with the key."));
		changed |= ig::Checkbox(L("$STB_PC_M_Loops", "Allow loops"), &v.allowLoops);
		Help(T("$STB_PC_M_LoopsHelp",
			"Keep holding W and the projectile goes over the top into a full loop,\n"
			"the camera turning upside down with it. Off: the nose stops at the pitch limit."));
		ig::BeginDisabled(v.allowLoops);
		changed |= Number(T("$STB_PC_M_PitchLimit", "Pitch limit"), &v.pitchLimit, 0.f, 89.f, "%.0f°");
		ig::EndDisabled();
		changed |= Number(T("$STB_PC_M_MouseSteerSens", "Mouse steering sensitivity"), &v.mouseSteerSensitivity, 0.01f,
			1.f, "%.2f");
		Help(T("$STB_PC_M_MouseSteerSensHelp",
			"Degrees of turn per unit of mouse movement while the mouse steers.\n"
			"Invert mouse Y on the Camera page applies here too."));
		changed |= Number(T("$STB_PC_M_Gravity", "Gravity"), &v.gravityScale, 0.f, 2.f, "%.2fx");
		Help(T("$STB_PC_M_GravityHelp", "0 = the projectile holds the line you put it on, 1 = it drops as usual."));
		changed |= Number(T("$STB_PC_M_Speed", "Speed"), &v.speedScale, 0.25f, 3.f, "%.2fx");
		Help(T("$STB_PC_M_SpeedHelp", "Multiplies the projectile's speed when the ride starts."));

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderImpactAndTime()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		ig::SeparatorText(T("$STB_PC_M_SlowMo", "Slow motion"));
		changed |= Number(T("$STB_PC_M_WorldTime", "World time"), &v.worldScale, 0.01f, 1.f, "%.2fx");
		changed |= Number(T("$STB_PC_M_PlayerTime", "Player time"), &v.playerScale, 0.01f, 1.f, "%.2fx");
		Help(T("$STB_PC_M_VanillaHelp",
			"Uses the game's own magic slowdown, the one Slow Time uses.\n"
			"A Slow Time already running is never made weaker, and is handed back after the ride."));

		ig::SeparatorText(T("$STB_PC_M_AfterHit", "After the hit"));
		changed |= Number(T("$STB_PC_M_Linger", "Stay on the hit"), &v.impactLinger, 0.f, 5.f, Seconds("%.1f"));
		Help(T("$STB_PC_M_LingerHelp", "Real seconds the camera stays after the projectile lands. 0 cuts back at once."));
		changed |= ig::Checkbox(L("$STB_PC_M_EaseBack", "Ease time back to normal meanwhile"), &v.lingerEaseTime);

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderCost()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		const auto costBlock = [&changed](const char* a_title, Settings::Cost& a_cost) {
			ig::SeparatorText(a_title);
			ig::PushID(a_title);
			changed |= PoolCombo(L("$STB_PC_M_PaidFrom", "Paid from"), &a_cost.pool);
			ig::BeginDisabled(a_cost.pool == Settings::CostPool::kNone);
			changed |= Number(T("$STB_PC_M_AtStart", "At the start"), &a_cost.start, 0.f, 200.f, "%.0f");
			changed |= Number(T("$STB_PC_M_PerSecond", "Per second"), &a_cost.perSecond, 0.f, 100.f, "%.1f");
			Help(T("$STB_PC_M_PerSecondHelp", "Real seconds while steering, not during the pause after the hit."));
			changed |= ig::Checkbox(L("$STB_PC_M_RefuseShort", "Refuse or end the ride when short"),
				&a_cost.cancelOnShortage);
			Help(T("$STB_PC_M_RefuseShortHelp",
				"Off: the ride always happens and just drains the pool to zero.\n"
				"Health never goes below 1 either way."));
			ig::EndDisabled();
			ig::PopID();
		};
		costBlock(T("$STB_PC_M_ArrowsAndBolts", "Arrows and bolts"), v.arrowCost);
		costBlock(T("$STB_PC_M_Spells", "Spells"), v.spellCost);

		Changed(changed);
	}

	void __stdcall SettingsMenu::RenderGeneral()
	{
		auto& v = Settings::_v;
		bool  changed = false;

		ig::SeparatorText(T("$STB_PC_M_Range", "Range"));
		changed |= Number(T("$STB_PC_M_MaxDistance", "Range cap"), &v.maxDistance, 0.f, 60000.f, "%.0f");
		Help(T("$STB_PC_M_MaxDistanceHelp",
			"Game units from where the shot was fired; the camera hands back past it. 0 = no cap.\n"
			"A shot leaving the loaded part of the world ends the ride anyway."));
		changed |= Number(T("$STB_PC_M_Life", "Projectile life"), &v.lifeScale, 0.f, 10.f, "%.2fx");
		Help(T("$STB_PC_M_LifeHelp",
			"Multiplies how far the projectile may fly before it runs out, while you steer it.\n"
			"1 = as the game has it, 0 = it never runs out. Let go and it is back to normal."));

		ig::SeparatorText(T("$STB_PC_M_Safety", "Safety"));
		changed |= Number(T("$STB_PC_M_Timeout", "Safety timeout"), &v.maxDuration, 1.f, 120.f, Seconds("%.0f"));
		Help(T("$STB_PC_M_TimeoutHelp", "A ride is torn down after this long no matter what. Not a gameplay timer."));
		changed |= ig::Checkbox(L("$STB_PC_M_DebugLog", "Debug log"), &v.debugLog);
		Help(T("$STB_PC_M_DebugLogHelp", "Writes every ride to STB_ProjectileControl.log."));

		ig::Spacing();
		ig::Separator();
		if (ig::Button(L("$STB_PC_M_Reset", "Reset everything to defaults"))) {
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() {
					Settings::_v = Settings::Values{};
					Settings::Apply(true);
					Settings::Save();
					g_reloadBuffers.store(true);
				});
			}
		}

		Changed(changed);
	}

	void SettingsMenu::Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed, INI-only mode");
			return;
		}
		// The header takes the framework's module handle when this DLL loads. SKSE loads
		// plugins alphabetically and "SKSEMenuFramework" sorts before "STB_ProjectileControl", so it
		// is there -- but if that ever changes every call below silently does nothing.
		if (!menuFramework) {
			logger::warn("SKSEMenuFramework.dll is installed but was not loaded before this plugin; no menu page");
			return;
		}

		auto& v = Settings::_v;
		g_targets[kTriggerKeyboard] = &v.triggerKeyboard;
		g_targets[kTriggerGamepad] = &v.triggerGamepad;
		g_targets[kCancelKeyboard] = &v.cancelKeyboard;
		g_targets[kCancelGamepad] = &v.cancelGamepad;
		g_targets[kMouseSteer] = &v.mouseSteerKey;
		g_targets[kSlower] = &v.slowerKey;
		g_targets[kFaster] = &v.fasterKey;

		Fill(g_alwaysBuf, v.alwaysRide);
		Fill(g_arrowPerkBuf, v.arrowPerk);
		Fill(g_spellPerkBuf, v.spellPerk);

		// kDataLoaded, main thread: every plugin's perks are in the data handler by now.
		BuildCatalogs();

		SKSEMenuFramework::SetSection("STB Projectile Control");
		// Page names are translated too; Localization::Load runs before this at kDataLoaded.
		// A '/' would split the framework's path, so it is dropped if a translation has one.
		const auto page = [](const char* a_token, const char* a_english) {
			std::string name = T(a_token, a_english);
			std::erase(name, '/');
			return name;
		};
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageControls", "Controls"), RenderControls);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageProjectiles", "Projectiles"), RenderProjectiles);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageCamera", "Camera"), RenderCamera);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageSteering", "Steering"), RenderSteering);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageImpact", "Impact & time"), RenderImpactAndTime);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageCost", "Cost"), RenderCost);
		SKSEMenuFramework::AddSectionItem(page("$STB_PC_M_PageGeneral", "General"), RenderGeneral);

		// Kept for the whole session, like the pages.
		SKSEMenuFramework::AddInputEvent(OnInput);

		logger::info("SKSE Menu Framework pages registered");
	}
}
