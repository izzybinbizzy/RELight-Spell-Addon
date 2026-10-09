// RELight - Spell Addon - the fading module's own names
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The fading module is the same code in Illuminated and in RELight - Spell Addon (the two never run together, so each
// carries its own copy). This header is the ONE fading file that differs between them: the mod's name, its folder and
// what it offers. Every other Fade* file must be byte-identical in both (`PC Runner\fade copies check.py`, in preflight).
// Game-free: the fading tests include it.

#pragma once

namespace Fade::Mod
{
	inline constexpr const char* kName = "RELight - Spell Addon";                                   // the menu section and the log lines
	inline constexpr const char* kSettingsPath = "Data/SKSE/Plugins/RelightSpellAddon/Fading.ini";  // the settings file
	inline constexpr const char* kRulesDir = "Data/SKSE/Plugins/RelightSpellAddon/Fading";          // the rule files (*.json)
	inline constexpr const char* kRulesDirText = "Data\\SKSE\\Plugins\\RelightSpellAddon\\Fading";  // the same, as a player reads it
	inline constexpr const char* kLogName = "RelightSpellAddon.log";
	// no light of our own on a weapon: an enchanted weapon's light is RE::Light's (RELight's rule: "ENCHANTMENTS ARE TRUMAN'S"),
	// so FadeOwnLight.cpp makes none, the setting is not offered and nothing searches for one
	inline constexpr bool        kOwnLight = false;
	inline constexpr const char* kOwnLightName = "RelightFadeLight";
}
