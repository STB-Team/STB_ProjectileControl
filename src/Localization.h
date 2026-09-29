#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace ProjectileControl::Localization
{
	// UI strings, loaded from Data/Interface/STB_ProjectileControl_<language>.txt. Ported from STB
	// Quick Sell (which took it from STB Hotkey System).
	//
	// Same shape as the game's own Translate_*.txt: one entry per line, `$TOKEN`, a TAB,
	// then the text. UTF-16 LE (what the CK and vanilla files use) and UTF-8 are both
	// accepted. Read by the plugin itself rather than left to the Scaleform translator: the
	// hint is built from C++ and the notifications never pass through a movie.
	//
	// Every token has a built-in English default, so a missing or partial file is fine.

	// Reads sLanguage:General and loads the matching file, falling back to english.
	void Load();

	// Translated text for a_token, or the built-in default, or a_token itself.
	[[nodiscard]] std::string Get(std::string_view a_token);

	// Translated text for a_token, or a_english. For strings whose English lives next to
	// the code that shows them (the settings menu), rather than in the defaults table.
	[[nodiscard]] std::string Get(std::string_view a_token, std::string_view a_english);

	// Get() with `{0}`, `{1}`, ... replaced by a_args in order.
	[[nodiscard]] std::string Format(std::string_view a_token, std::initializer_list<std::string_view> a_args);
}
