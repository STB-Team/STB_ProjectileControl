#include "Hint.h"

#include "Input.h"
#include "Localization.h"
#include "Session.h"
#include "Settings.h"
#include "swfhelper/ImportData.h"

namespace ProjectileControl
{
	namespace
	{
		// Where the keycap art can come from, best match first. Every SkyUI-derived UI
		// ships one of these as a standalone file exporting the clip as "ButtonArt" --
		// character 153 in all of them, so the frame layout is identical. Untarnished UI's
		// own favoritesmenu.swf also holds the clip, but with no export name, and an import
		// can only ask by name -- hence the standalone files. SkyUI is a requirement, so the
		// last entry always answers; nothing ships with this mod.
		struct Source
		{
			const char* path;
			const char* symbol;
		};
		constexpr std::array kSources{
			Source{ "Interface/skyui/buttonArtDD.swf", "ButtonArt" },   // Untarnished UI, Dear Diary
			Source{ "Interface/skyui/buttonArtDDW.swf", "ButtonArt" },  // DearDiaryHUD
			Source{ "Interface/skyui/buttonart.swf", "ButtonArt" },     // SkyUI and reskins of it
		};

		std::string g_swf;
		std::string g_symbol;

		// Our clip on the HUD's _root. The depth is high and odd enough not to land on
		// anything the HUD or a HUD mod places there.
		constexpr const char*  kHolderName = "STBProjectileControlHint";
		constexpr std::int32_t kHolderDepth = 0x6FFF7A00;

		constexpr double kKeyGap = 3.0;     // between keycaps of one entry
		constexpr double kLabelGap = 8.0;   // keycaps -> label
		constexpr double kEntryGap = 28.0;  // label -> next entry

		// SkyUI ButtonArt frame layout (see STB Hotkey System's flash/README.md).
		constexpr std::uint32_t kGamepadBase = 266;
		constexpr std::array<std::uint32_t, 16> kGamepadIds{
			0x0001, 0x0002, 0x0004, 0x0008,  // D-pad up, down, left, right
			0x0010, 0x0020,                  // Start, Back
			0x0040, 0x0080,                  // left, right stick click
			0x0100, 0x0200,                  // LB, RB
			0x1000, 0x2000, 0x4000, 0x8000,  // A, B, X, Y
			0x0009, 0x000A,                  // LT, RT
		};
		// Stands in for "the left stick" in the steer entry: the art has no stick-move glyph.
		constexpr std::uint32_t kLeftStickId = 0x0040;

		std::vector<ImportData::loadReq> g_loadReq;
		bool                             g_resolved = false;
		const void*                      g_importedDef = nullptr;

		// What is on screen now, so the row is only rebuilt when something changes.
		bool          g_shown = false;
		std::uint64_t g_layout = 0;

		REL::Relocation<void (*)(RE::IMenu*, float, std::uint32_t)> _advanceHud;

		std::uint32_t GamepadFrame(std::uint32_t a_id)
		{
			for (std::size_t i = 0; i < kGamepadIds.size(); ++i) {
				if (kGamepadIds[i] == a_id) {
					return kGamepadBase + static_cast<std::uint32_t>(i);
				}
			}
			return 0;
		}

		struct Entry
		{
			std::array<std::uint32_t, 4> frames{};  // 0 = unused slot
			std::string                  label;
		};

		std::vector<Entry> BuildEntries()
		{
			std::vector<Entry> entries;
			const bool         pad = Input::UsingGamepad();

			if (Settings::HintShowSteer()) {
				Entry steer;
				if (pad) {
					steer.frames[0] = GamepadFrame(kLeftStickId);
				} else {
					// Forward, Strafe Left, Back, Strafe Right -- read as "W A S D".
					steer.frames = { Input::MoveKey(0), Input::MoveKey(2), Input::MoveKey(1), Input::MoveKey(3) };
				}
				steer.label = Localization::Get("$STB_PC_Hint_Steer");
				entries.push_back(std::move(steer));

				// Keyboard and mouse only: a gamepad has neither a mouse nor these keys.
				// Stored codes are keycap frames already (scancode, 256 + mouse button).
				if (!pad) {
					if (const auto mouse = Settings::MouseSteerKey(); mouse != 0) {
						Entry aim;
						aim.frames[0] = mouse;
						aim.label = Localization::Get("$STB_PC_Hint_Mouse");
						entries.push_back(std::move(aim));
					}
					const auto slower = Settings::SlowerKey();
					const auto faster = Settings::FasterKey();
					if (slower != 0 || faster != 0) {
						// The current value is part of the label; the keys bump the settings
						// generation, which re-lays the row out.
						Entry time;
						time.frames = { slower, faster, 0, 0 };
						time.label = fmt::format("{} {}%", Localization::Get("$STB_PC_Hint_Time"),
							std::lround(Settings::WorldScale() * 100.f));
						entries.push_back(std::move(time));
					}
				}
			}

			const std::uint32_t cancel = pad ? GamepadFrame(Settings::CancelGamepad()) : Settings::CancelKeyboard();
			if (cancel != 0) {
				Entry leave;
				leave.frames[0] = cancel;
				leave.label = Localization::Get("$STB_PC_Hint_Cancel");
				entries.push_back(std::move(leave));
			}
			return entries;
		}

		// Cheap fingerprint of the row: which keys, which device, and the settings generation
		// -- so a size, font or position changed in the menu re-lays the row out at once.
		// The labels only change with the language, which needs a restart anyway.
		std::uint64_t LayoutKey(const std::vector<Entry>& a_entries)
		{
			std::uint64_t key = (1469598103934665603ull ^ Settings::Generation()) * 1099511628211ull;
			for (const auto& e : a_entries) {
				for (const auto f : e.frames) {
					key = (key ^ f) * 1099511628211ull;
				}
				key = (key ^ 0xFFu) * 1099511628211ull;
			}
			return key;
		}

		double Number(RE::GFxValue& a_obj, const char* a_member)
		{
			RE::GFxValue v;
			return (a_obj.GetMember(a_member, &v) && v.IsNumber()) ? v.GetNumber() : 0.0;
		}

		// Attach (or reuse) a keycap under a_parent at a_x; returns its width, 0 when the
		// symbol is not available or a_frame is 0 (which also hides a reused clip).
		double Keycap(RE::GFxValue& a_parent, const std::string& a_name, std::uint32_t a_frame, std::int32_t a_depth,
			double a_x, double& a_height)
		{
			RE::GFxValue icon;
			a_parent.GetMember(a_name.c_str(), &icon);
			if (a_frame == 0) {
				if (icon.IsObject()) {
					icon.SetMember("_visible", RE::GFxValue{ false });
				}
				return 0.0;
			}
			if (!icon.IsObject()) {
				a_parent.AttachMovie(&icon, g_symbol.c_str(), a_name.c_str(), a_depth, nullptr);
				if (!icon.IsObject()) {
					return 0.0;
				}
			}
			const double scale = Settings::HintScale();
			icon.GotoAndStop(std::to_string(a_frame).c_str());
			icon.SetMember("_xscale", RE::GFxValue{ scale });
			icon.SetMember("_yscale", RE::GFxValue{ scale });
			icon.SetMember("_x", RE::GFxValue{ a_x });
			icon.SetMember("_y", RE::GFxValue{ 0.0 });
			icon.SetMember("_visible", RE::GFxValue{ true });
			a_height = (std::max)(a_height, Number(icon, "_height"));
			return Number(icon, "_width");
		}

		// A plain label in the game's own UI font. $EverywhereMediumFont is a font-library
		// name every movie resolves, and embedFonts has to be on for it to render at all.
		// The format is applied after the text, or the field falls back to the device font.
		RE::GFxValue Label(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const std::string& a_name,
			std::int32_t a_depth, const std::string& a_text)
		{
			RE::GFxValue field;
			a_parent.GetMember(a_name.c_str(), &field);
			if (!field.IsObject()) {
				const RE::GFxValue args[6]{ RE::GFxValue{ a_name.c_str() }, RE::GFxValue{ static_cast<double>(a_depth) },
					RE::GFxValue{ 0.0 }, RE::GFxValue{ 0.0 }, RE::GFxValue{ 10.0 }, RE::GFxValue{ 10.0 } };
				a_parent.Invoke("createTextField", nullptr, args, std::size(args));
				if (!a_parent.GetMember(a_name.c_str(), &field) || !field.IsObject()) {
					return field;
				}
				field.SetMember("selectable", RE::GFxValue{ false });
				field.SetMember("border", RE::GFxValue{ false });
				field.SetMember("background", RE::GFxValue{ false });
				field.SetMember("embedFonts", RE::GFxValue{ true });
				field.SetMember("autoSize", RE::GFxValue{ "left" });
			}

			field.SetMember("text", RE::GFxValue{ a_text.c_str() });

			RE::GFxValue format;
			a_movie->CreateObject(&format, "TextFormat");
			if (format.IsObject()) {
				format.SetMember("font", RE::GFxValue{ "$EverywhereMediumFont" });
				format.SetMember("size", RE::GFxValue{ static_cast<double>(Settings::HintFontSize()) });
				format.SetMember("color", RE::GFxValue{ static_cast<double>(0xFFFFFF) });
				field.Invoke("setTextFormat", nullptr, &format, 1);
				field.Invoke("setNewTextFormat", nullptr, &format, 1);
			}
			field.SetMember("_visible", RE::GFxValue{ true });
			return field;
		}

		void Layout(RE::GFxMovieView* a_movie, RE::GFxValue& a_holder, const std::vector<Entry>& a_entries)
		{
			// First pass: keycaps and labels at y = 0, left to right.
			struct Placed
			{
				RE::GFxValue label;
				double       x = 0.0;
			};
			std::vector<Placed> placed;
			double              x = 0.0;
			double              keyHeight = 0.0;
			std::int32_t        depth = 1;
			bool                anyKeycap = false;

			for (std::size_t i = 0; i < a_entries.size(); ++i) {
				const auto& entry = a_entries[i];
				bool        first = true;
				for (std::size_t j = 0; j < entry.frames.size(); ++j) {
					const std::string name = "k" + std::to_string(i) + "_" + std::to_string(j);
					const double      w = Keycap(a_holder, name, entry.frames[j], depth++, x, keyHeight);
					if (w > 0.0) {
						x += w + kKeyGap;
						first = false;
						anyKeycap = true;
					}
				}
				if (!first) {
					x += kLabelGap - kKeyGap;
				}
				Placed p;
				p.label = Label(a_movie, a_holder, "t" + std::to_string(i), depth++, entry.label);
				p.x = x;
				x += Number(p.label, "_width") + kEntryGap;
				placed.push_back(std::move(p));
			}
			const double width = (std::max)(x - kEntryGap, 0.0);

			static bool warned = false;
			if (!anyKeycap && !warned) {
				warned = true;
				logger::warn("hint: no keycaps drawn, showing labels only");
			}

			// Second pass: labels centred on the keycap row.
			for (auto& p : placed) {
				if (!p.label.IsObject()) {
					continue;
				}
				const double h = Number(p.label, "_height");
				p.label.SetMember("_x", RE::GFxValue{ p.x });
				p.label.SetMember("_y", RE::GFxValue{ keyHeight > 0.0 ? (keyHeight - h) / 2.0 : 0.0 });
			}

			// Centred at the bottom of what is actually visible: with a wide screen the HUD
			// shows more than its stage, and the stage centre is not the screen centre.
			const auto   rect = a_movie->GetVisibleFrameRect();
			const double rowHeight = (std::max)(keyHeight, 20.0);
			a_holder.SetMember("_x", RE::GFxValue{ (rect.left + rect.right) / 2.0 - width / 2.0 });
			a_holder.SetMember("_y", RE::GFxValue{ rect.bottom - Settings::HintBottom() - rowHeight });
		}

		// Can this file be loaded and does it export that symbol? Loading goes through the
		// game's own loader, so a file inside a BSA counts just as much as a loose one --
		// which a filesystem check would miss (SkyUI keeps its buttonart.swf in SkyUI_SE.bsa).
		bool Usable(const char* a_path, const char* a_symbol)
		{
			auto* def = ImportData::LoadMovie(a_path);
			return def && def->GetResource(a_symbol) != nullptr;
		}

		void Use(const char* a_path, const char* a_symbol)
		{
			g_swf = a_path;
			g_symbol = a_symbol;
			g_loadReq.clear();
			g_loadReq.push_back(ImportData::loadReq{ .sourcePath = g_swf, .exports = { g_symbol }, .resources = {} });
			logger::info("hint: keycaps from {} ({})", g_swf, g_symbol);
		}

		// Done at the first import rather than at kDataLoaded: this asks Scaleform to load
		// movies, and by now the interface is up.
		void Resolve()
		{
			if (g_resolved) {
				return;
			}
			g_resolved = true;

			for (const auto& source : kSources) {
				if (Usable(source.path, source.symbol)) {
					Use(source.path, source.symbol);
					return;
				}
			}
			logger::warn("hint: no keycap art found (is SkyUI installed?), showing labels only");
		}

		void Update(RE::IMenu* a_hud)
		{
			auto* movie = a_hud ? a_hud->uiMovie.get() : nullptr;
			if (!movie) {
				return;
			}

			RE::GFxValue root;
			if (!movie->GetVariable(&root, "_root") || !root.IsObject()) {
				return;
			}
			RE::GFxValue holder;
			root.GetMember(kHolderName, &holder);

			const bool want = Settings::ShowHint() && Session::HintVisible();
			if (!want) {
				if (g_shown && holder.IsObject()) {
					holder.SetMember("_visible", RE::GFxValue{ false });
				}
				g_shown = false;
				return;
			}

			// The symbol goes into the movie's definition, which outlives the menu; import
			// once per definition.
			if (const void* def = movie->GetMovieDef(); def != g_importedDef) {
				Resolve();
				if (!g_loadReq.empty()) {
					ImportData::ImportResources(ImportData::GetMovieDefImpl(movie->GetMovieDef()), g_loadReq);
					for (auto& req : g_loadReq) {
						req.resources.clear();
					}
					logger::info("hint: keycaps imported into the HUD");
				}
				g_importedDef = def;
			}

			bool fresh = false;
			if (!holder.IsObject()) {
				root.CreateEmptyMovieClip(&holder, kHolderName, kHolderDepth);
				if (!holder.IsObject()) {
					return;
				}
				fresh = true;
			}

			const auto          entries = BuildEntries();
			const std::uint64_t layout = LayoutKey(entries);
			if (fresh || !g_shown || layout != g_layout) {
				Layout(movie, holder, entries);
				g_layout = layout;
			}
			holder.SetMember("_visible", RE::GFxValue{ true });
			g_shown = true;
		}

		// HUDMenu::AdvanceMovie runs every frame while the game is up -- the place STB
		// Hotkey System draws its HUD keycaps from as well. Chained, both work.
		void AdvanceHud(RE::IMenu* a_this, float a_interval, std::uint32_t a_time)
		{
			_advanceHud(a_this, a_interval, a_time);
			Update(a_this);
		}
	}

	void Hint::Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_HUDMenu[0] };
		_advanceHud = vtbl.write_vfunc(0x5, &AdvanceHud);
		logger::info("hint installed (HUDMenu::AdvanceMovie)");
	}
}
