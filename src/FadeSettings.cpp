// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings file, Data\SKSE\Plugins\RelightSpellAddon\Fading.ini (its lines: SettingsText.h). Read once at data load, written
// whenever the menu changes a setting.
//
// Threads: the menu changes the settings on the render thread and DevBench on its own, while the main thread reads them
// every frame. The one copy here is behind a lock, and everyone else works on a copy (Config / SetConfig).

#include "Fade.h"

#include <sstream>

namespace Fade
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/RelightSpellAddon/Fading.ini";

		std::mutex gLock;
		Settings   gSettings;
	}

	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	Settings Config()
	{
		std::lock_guard lock(gLock);
		return gSettings;
	}

	void SetConfig(const Settings& a_settings)
	{
		std::lock_guard lock(gLock);
		gSettings = a_settings;
	}

	bool ApplySetting(std::string_view a_key, int a_value)
	{
		std::lock_guard lock(gLock);
		return SettingsText::Apply(gSettings, a_key, a_value);
	}

	Preview& PreviewState()
	{
		static Preview preview;
		return preview;
	}

	void LoadSettings()
	{
		Settings                 s;
		SettingsText::ReadResult read;
		if (std::ifstream in(kPath); in) {
			read = SettingsText::Read(in, s);
		}
		for (const auto& p : read.problems) {
			SKSE::log::warn("settings: {}", p);
		}
		SetConfig(s);
		const auto& t = s.tuning;
		SKSE::log::info(
			"settings: {} line(s) read from {}; enabled {}, empty brightness {}%, curve {}, reach follows {}%, "
			"sputter {} below {}%, pulse {}, flare {}, cooling {}, staves {}, bound {} ({} s), who {}, dim glow {}, hide charge bar {}, own light {}",
			read.taken, kPath, s.enabled, SettingsText::ToPct(t.floor), static_cast<int>(t.curve), SettingsText::ToPct(t.reachFollows),
			t.sputter, SettingsText::ToPct(t.sputterBelow), t.pulse, t.flare, t.cool, s.staves, s.bound, s.boundFadeSeconds,
			static_cast<int>(s.who), s.dimShader, s.hideChargeBar, s.ownLight);
	}

	void SaveSettings()
	{
		std::ostringstream text;
		SettingsText::Write(text, Config());
		// written beside the file, then moved over it: a crash or a full disk mid-write leaves the old settings, never half a file
		const std::string tmp = std::string(kPath) + ".tmp";
		std::ofstream     out(tmp, std::ios::trunc);  // text mode: Windows line ends, as Notepad writes
		if (!out) {
			SKSE::log::warn("settings: {} could not be written", kPath);
			return;
		}
		out << text.str();
		out.close();
		std::error_code ec;
		if (!out) {
			SKSE::log::warn("settings: {} could not be written", kPath);
		} else if (std::filesystem::rename(tmp, kPath, ec); ec) {
			SKSE::log::warn("settings: {} could not be replaced ({})", kPath, ec.message());
		}
		std::filesystem::remove(tmp, ec);  // nothing left behind when the move failed
	}
}
