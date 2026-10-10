// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings, and the settings file's text both ways, with nothing of the game in it (tests/test_fade.cpp reads and
// writes it off the game). Settings.cpp only opens the file and keeps the one shared copy.
//
//   [Settings]
//   Enabled=1
//   EmptyBrightness=10        percent, 0 to 50 - what is left of the light at 0% charge
//   Curve=1                   0 linear, 1 gentle (stays bright longer); the old 2 (steep) reads as gentle
//   ReachFollows=50           percent, 0 to 100 - how much of the fade the reach follows too
//   Sputter=1
//   SputterBelow=15           percent of charge, 1 to 50
//   SputterStrength=60        percent - the deepest dip, at empty
//   EmptySteady=1             at exactly 0% the light holds still
//   HitPulse=1
//   HitPulseStrength=60       percent, 0 to 200
//   RechargeFlare=1
//   RechargeFlareStrength=80  percent, 0 to 200
//   ColorCooling=1
//   ColorCoolingAmount=50     percent - toward grey; a fire spell or enchantment with an orange light cools to ember
//   Weapons=1                 enchanted weapons' lights and glow follow their charge
//   Staves=1
//   BoundWeapons=1
//   Spells=1                  a spell in hand: its hand's lights and glow follow the caster's magicka
//   BoundFadeSeconds=10       1 to 60: the last seconds of a bound weapon's spell, over which its light fades
//   Who=0                     0 the player, 1 the player and followers, 2 everyone nearby (every loaded actor)
//   DimShader=1               the enchantment's glow (its shader and its art's swirls) follows the charge too
//   HideChargeBar=0           the HUD's enchantment charge bar is hidden (vanilla HUD, SkyHUD, TrueHUD)
//   HudGems=0                 a soul gem per weapon hand beside the health bar, shown while its charge is down (needs SKSE Menu Framework)
//   Reticle=0                 the spell reticle beside the crosshair, draining with magicka (needs SKSE Menu Framework)
//   ReticleStyle=0            0 a bar per hand beside the crosshair, 1 Crossfire's bar under it
//   ReticleSize=100           percent, 50 to 200
//   ReticleOpacity=85         percent, 10 to 100
//   OwnLight=1                a weapon no other mod lights gets a simple light in its enchantment's colour - only where
//                             FadeConfig.h offers it (Mod::kOwnLight): elsewhere it is neither read nor written
//   DebugLog=0
//   Fire=1 Frost=1 Shock=1 Absorb=1 SoulTrap=1 Paralyze=1 FearTurnBanish=1 OtherEffects=1
//                             by effect: an enchantment's or a spell's strongest effect, for weapons and spells alike
//   Destruction=1 Restoration=1 Conjuration=1 Alteration=1 Illusion=1
//                             by school: the same effect's school (an effect of no school is never held back by these)
//
// Every value is a whole number. The file is read the way people edit it: a byte order mark (Notepad's), any case in
// section and key names, spaces, Windows line ends, and ; or # comments, on their own line or after a value. A line
// that is not a whole number, or names no setting, keeps the default and is reported; a retired key (kRetired) is
// skipped quietly.

#pragma once

#include "FadeConfig.h"
#include "FadeGlow.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Fade
{
	enum class Who : int
	{
		kPlayer = 0,
		kPlayerAndFollowers = 1,
		kEveryone = 2  // his ask 2026-10-09: "can fading not work for npcs as well?"
	};

	struct Settings
	{
		bool         enabled{ true };
		Glow::Tuning tuning{};
		Who          who{ Who::kPlayer };
		bool         weapons{ true };  // enchanted weapons (staves and bound weapons have their own switches too)
		bool         staves{ true };
		bool         bound{ true };
		bool         spells{ true };  // a spell in hand fades with the caster's magicka
		// what kind of magic fades, by the strongest effect (Kind), for weapons and spells alike: [element] and [school]
		std::array<bool, 8> elements{ true, true, true, true, true, true, true, true };
		std::array<bool, 5> schools{ true, true, true, true, true };
		float               boundFadeSeconds{ 10.0f };
		bool                dimShader{ true };  // the enchantment's glow shader and its art's swirls follow the charge too
		bool                hideChargeBar{ false };
		bool                hudGems{ false };  // Menu.cpp's HUD element: one charge gem per tracked hand of the player
		float               hudGemSize{ 1.0f };
		float               hudGemOpacity{ 0.9f };
		bool                hudGemPercent{ true };  // the percent under each gem
		bool                reticle{ false };       // FadeMenu.cpp's reticle: a bar per hand of the player beside the crosshair
		int                 reticleStyle{ 0 };      // 0 the bars beside the crosshair, 1 Crossfire's bar under it (his pick 2026-10-10)
		float               reticleSize{ 1.0f };
		float               reticleOpacity{ 0.85f };
		bool                ownLight{ Mod::kOwnLight };  // a weapon no other mod lights gets a light of our own (FadeOwnLight.cpp)
		bool                debugLog{ false };

		bool operator==(const Settings&) const = default;
	};

	// the kind of an enchantment's or a spell's strongest effect, as Spells.cpp reads it off the game
	enum class Element : int
	{
		kFire = 0,
		kFrost,
		kShock,
		kAbsorb,
		kSoulTrap,
		kParalyze,
		kFearTurnBanish,  // fear, turn undead, banish
		kOther
	};
	inline constexpr const char* kElementKeys[]{ "Fire", "Frost", "Shock", "Absorb", "SoulTrap", "Paralyze", "FearTurnBanish", "OtherEffects" };
	inline constexpr const char* kElementNames[]{ "fire", "frost", "shock", "absorb", "soul trap", "paralyze", "fear, turn or banish", "other" };
	inline constexpr const char* kSchoolKeys[]{ "Destruction", "Restoration", "Conjuration", "Alteration", "Illusion" };

	struct Kind
	{
		Element element{ Element::kOther };
		int     school{ -1 };  // 0 Destruction .. 4 Illusion, as kSchoolKeys; -1 no school
	};

	// "" when this kind fades, else why it does not (for the log and the Debug page)
	[[nodiscard]] inline std::string KindOff(const Settings& a_s, Kind a_k)
	{
		const auto e = static_cast<std::size_t>(a_k.element);
		if (e < a_s.elements.size() && !a_s.elements[e]) {
			return std::string(kElementKeys[e]) + " off";
		}
		if (a_k.school >= 0 && a_k.school < 5 && !a_s.schools[static_cast<std::size_t>(a_k.school)]) {
			return std::string(kSchoolKeys[a_k.school]) + " off";
		}
		return {};
	}

	// what a weapon ends up with once the rule files have had their say (Rules.cpp, RulesText.h)
	enum class Mode : int
	{
		kCharge = 0,  // the light follows the enchantment's charge
		kExempt = 1,  // the light is left alone
		kBound = 2    // the light follows the time a bound-weapon spell has left
	};

	struct Verdict
	{
		Mode         mode{ Mode::kCharge };
		Glow::Tuning tuning{};
		float        boundFadeSeconds{ 10.0f };
		std::string  why;  // the rule(s) that decided it, for the menu's debug page
	};

	namespace SettingsText
	{
		[[nodiscard]] inline float Pct(int a_v, int a_lo, int a_hi) { return static_cast<float>(std::clamp(a_v, a_lo, a_hi)) / 100.0f; }
		[[nodiscard]] inline int   ToPct(float a_v) { return static_cast<int>(std::lround(a_v * 100.0f)); }

		// every setting: its key in the file, how it reads back as a whole number, and how a whole number sets it (clamped to
		// what the menu allows). One table, so the file, the menu and DevBench can never disagree.
		struct Key
		{
			const char* name;
			int (*get)(const Settings&);
			void (*set)(Settings&, int);
		};

		inline constexpr Key kKeys[]{
			{ "Enabled", [](const Settings& s) { return s.enabled ? 1 : 0; }, [](Settings& s, int v) { s.enabled = v != 0; } },
			{ "EmptyBrightness", [](const Settings& s) { return ToPct(s.tuning.floor); }, [](Settings& s, int v) { s.tuning.floor = Pct(v, 0, 50); } },
			{ "Curve", [](const Settings& s) { return static_cast<int>(s.tuning.curve); },
				[](Settings& s, int v) { s.tuning.curve = v <= 0 ? Glow::Curve::kLinear : Glow::Curve::kGentle; } },
			{ "ReachFollows", [](const Settings& s) { return ToPct(s.tuning.reachFollows); },
				[](Settings& s, int v) { s.tuning.reachFollows = Pct(v, 0, 100); } },
			{ "Sputter", [](const Settings& s) { return s.tuning.sputter ? 1 : 0; }, [](Settings& s, int v) { s.tuning.sputter = v != 0; } },
			{ "SputterBelow", [](const Settings& s) { return ToPct(s.tuning.sputterBelow); },
				[](Settings& s, int v) { s.tuning.sputterBelow = Pct(v, 1, 50); } },
			{ "SputterStrength", [](const Settings& s) { return ToPct(s.tuning.sputterStrength); },
				[](Settings& s, int v) { s.tuning.sputterStrength = Pct(v, 0, 100); } },
			{ "EmptySteady", [](const Settings& s) { return s.tuning.emptySteady ? 1 : 0; }, [](Settings& s, int v) { s.tuning.emptySteady = v != 0; } },
			{ "HitPulse", [](const Settings& s) { return s.tuning.pulse ? 1 : 0; }, [](Settings& s, int v) { s.tuning.pulse = v != 0; } },
			{ "HitPulseStrength", [](const Settings& s) { return ToPct(s.tuning.pulseStrength); },
				[](Settings& s, int v) { s.tuning.pulseStrength = Pct(v, 0, 200); } },
			{ "RechargeFlare", [](const Settings& s) { return s.tuning.flare ? 1 : 0; }, [](Settings& s, int v) { s.tuning.flare = v != 0; } },
			{ "RechargeFlareStrength", [](const Settings& s) { return ToPct(s.tuning.flareStrength); },
				[](Settings& s, int v) { s.tuning.flareStrength = Pct(v, 0, 200); } },
			{ "ColorCooling", [](const Settings& s) { return s.tuning.cool ? 1 : 0; }, [](Settings& s, int v) { s.tuning.cool = v != 0; } },
			{ "ColorCoolingAmount", [](const Settings& s) { return ToPct(s.tuning.coolAmount); },
				[](Settings& s, int v) { s.tuning.coolAmount = Pct(v, 0, 100); } },
			{ "Weapons", [](const Settings& s) { return s.weapons ? 1 : 0; }, [](Settings& s, int v) { s.weapons = v != 0; } },
			{ "Staves", [](const Settings& s) { return s.staves ? 1 : 0; }, [](Settings& s, int v) { s.staves = v != 0; } },
			{ "BoundWeapons", [](const Settings& s) { return s.bound ? 1 : 0; }, [](Settings& s, int v) { s.bound = v != 0; } },
			{ "Spells", [](const Settings& s) { return s.spells ? 1 : 0; }, [](Settings& s, int v) { s.spells = v != 0; } },
			{ "BoundFadeSeconds", [](const Settings& s) { return static_cast<int>(std::lround(s.boundFadeSeconds)); },
				[](Settings& s, int v) { s.boundFadeSeconds = static_cast<float>(std::clamp(v, 1, 60)); } },
			{ "Who", [](const Settings& s) { return static_cast<int>(s.who); }, [](Settings& s, int v) { s.who = static_cast<Who>(std::clamp(v, 0, 2)); } },
			{ "DimShader", [](const Settings& s) { return s.dimShader ? 1 : 0; }, [](Settings& s, int v) { s.dimShader = v != 0; } },
			{ "HideChargeBar", [](const Settings& s) { return s.hideChargeBar ? 1 : 0; }, [](Settings& s, int v) { s.hideChargeBar = v != 0; } },
			{ "HudGems", [](const Settings& s) { return s.hudGems ? 1 : 0; }, [](Settings& s, int v) { s.hudGems = v != 0; } },
			{ "HudGemSize", [](const Settings& s) { return ToPct(s.hudGemSize); }, [](Settings& s, int v) { s.hudGemSize = Pct(v, 50, 200); } },
			{ "HudGemOpacity", [](const Settings& s) { return ToPct(s.hudGemOpacity); },
				[](Settings& s, int v) { s.hudGemOpacity = Pct(v, 10, 100); } },
			{ "HudGemPercent", [](const Settings& s) { return s.hudGemPercent ? 1 : 0; }, [](Settings& s, int v) { s.hudGemPercent = v != 0; } },
			{ "Reticle", [](const Settings& s) { return s.reticle ? 1 : 0; }, [](Settings& s, int v) { s.reticle = v != 0; } },
			{ "ReticleStyle", [](const Settings& s) { return s.reticleStyle; }, [](Settings& s, int v) { s.reticleStyle = std::clamp(v, 0, 1); } },
			{ "ReticleSize", [](const Settings& s) { return ToPct(s.reticleSize); }, [](Settings& s, int v) { s.reticleSize = Pct(v, 50, 200); } },
			{ "ReticleOpacity", [](const Settings& s) { return ToPct(s.reticleOpacity); },
				[](Settings& s, int v) { s.reticleOpacity = Pct(v, 10, 100); } },
			{ "OwnLight", [](const Settings& s) { return s.ownLight ? 1 : 0; }, [](Settings& s, int v) { s.ownLight = v != 0; } },
			{ "DebugLog", [](const Settings& s) { return s.debugLog ? 1 : 0; }, [](Settings& s, int v) { s.debugLog = v != 0; } },
			{ "Fire", [](const Settings& s) { return s.elements[0] ? 1 : 0; }, [](Settings& s, int v) { s.elements[0] = v != 0; } },
			{ "Frost", [](const Settings& s) { return s.elements[1] ? 1 : 0; }, [](Settings& s, int v) { s.elements[1] = v != 0; } },
			{ "Shock", [](const Settings& s) { return s.elements[2] ? 1 : 0; }, [](Settings& s, int v) { s.elements[2] = v != 0; } },
			{ "Absorb", [](const Settings& s) { return s.elements[3] ? 1 : 0; }, [](Settings& s, int v) { s.elements[3] = v != 0; } },
			{ "SoulTrap", [](const Settings& s) { return s.elements[4] ? 1 : 0; }, [](Settings& s, int v) { s.elements[4] = v != 0; } },
			{ "Paralyze", [](const Settings& s) { return s.elements[5] ? 1 : 0; }, [](Settings& s, int v) { s.elements[5] = v != 0; } },
			{ "FearTurnBanish", [](const Settings& s) { return s.elements[6] ? 1 : 0; }, [](Settings& s, int v) { s.elements[6] = v != 0; } },
			{ "OtherEffects", [](const Settings& s) { return s.elements[7] ? 1 : 0; }, [](Settings& s, int v) { s.elements[7] = v != 0; } },
			{ "Destruction", [](const Settings& s) { return s.schools[0] ? 1 : 0; }, [](Settings& s, int v) { s.schools[0] = v != 0; } },
			{ "Restoration", [](const Settings& s) { return s.schools[1] ? 1 : 0; }, [](Settings& s, int v) { s.schools[1] = v != 0; } },
			{ "Conjuration", [](const Settings& s) { return s.schools[2] ? 1 : 0; }, [](Settings& s, int v) { s.schools[2] = v != 0; } },
			{ "Alteration", [](const Settings& s) { return s.schools[3] ? 1 : 0; }, [](Settings& s, int v) { s.schools[3] = v != 0; } },
			{ "Illusion", [](const Settings& s) { return s.schools[4] ? 1 : 0; }, [](Settings& s, int v) { s.schools[4] = v != 0; } },
		};

		// keys an older version wrote: read past without a warning, never written again (ColorCoolingTint: his word 2026-10-10,
		// "the only option will be grey, ember cooling only for fire spell types that are orange" - Glow::TintFor decides)
		inline constexpr std::string_view kRetired[]{ "OwnLightReach", "ColorCoolingTint" };

		[[nodiscard]] inline bool SameText(std::string_view a, std::string_view b) noexcept
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// a key this mod offers: OwnLight only where FadeConfig.h offers our own light
		[[nodiscard]] inline bool Offered(std::string_view a_name) noexcept { return Mod::kOwnLight || !SameText(a_name, "OwnLight"); }

		[[nodiscard]] inline const Key* Find(std::string_view a_name) noexcept
		{
			for (const auto& k : kKeys) {
				if (SameText(a_name, k.name) && Offered(k.name)) {
					return &k;
				}
			}
			return nullptr;
		}

		[[nodiscard]] inline std::string_view Trim(std::string_view a_s) noexcept
		{
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.front()))) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.back()))) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		// a whole number and nothing else ("15abc" and "1.5" are not)
		[[nodiscard]] inline bool ReadInt(std::string_view a_v, int& a_out) noexcept
		{
			int v = 0;
			const auto [end, ec] = std::from_chars(a_v.data(), a_v.data() + a_v.size(), v);
			if (a_v.empty() || ec != std::errc{} || end != a_v.data() + a_v.size()) {
				return false;
			}
			a_out = v;
			return true;
		}

		// one setting by its key (any case), clamped as the menu clamps it; false for a key it does not know
		inline bool Apply(Settings& a_s, std::string_view a_key, int a_value)
		{
			if (const auto* k = Find(a_key)) {
				k->set(a_s, a_value);
				return true;
			}
			return false;
		}

		struct ReadResult
		{
			int                      taken{ 0 };
			std::vector<std::string> problems;  // "Key=value: why", for the log
		};

		// reads the [Settings] lines into a_s; a line that cannot be used keeps the default and is reported
		inline ReadResult Read(std::istream& a_in, Settings& a_s)
		{
			ReadResult  r;
			std::string line;
			bool        inSettings = false, first = true;
			while (std::getline(a_in, line)) {
				std::string_view l = line;
				if (first && l.starts_with("\xEF\xBB\xBF")) {
					l.remove_prefix(3);  // a UTF-8 byte order mark
				}
				first = false;
				if (const auto c = l.find_first_of(";#"); c != std::string_view::npos) {
					l = l.substr(0, c);
				}
				l = Trim(l);
				if (l.empty()) {
					continue;
				}
				if (l.front() == '[' && l.back() == ']') {
					inSettings = SameText(Trim(l.substr(1, l.size() - 2)), "Settings");
					continue;
				}
				const auto eq = l.find('=');
				if (!inSettings || eq == std::string_view::npos) {
					continue;
				}
				const auto key = Trim(l.substr(0, eq));
				const auto val = Trim(l.substr(eq + 1));
				int        v = 0;
				if (!Offered(key) || std::ranges::any_of(kRetired, [key](std::string_view k) { return SameText(key, k); })) {
					continue;
				}
				if (!Find(key)) {
					r.problems.push_back(std::string(key) + ": not a setting");
				} else if (!ReadInt(val, v)) {
					r.problems.push_back(std::string(key) + "=" + std::string(val) + ": not a whole number; the default stays");
				} else {
					Apply(a_s, key, v);
					++r.taken;
				}
			}
			return r;
		}

		inline void Write(std::ostream& a_out, const Settings& a_s)
		{
			a_out << "; " << Mod::kName << " - fading - written by its menu (SKSE Menu Framework). Percent values are whole numbers.\n[Settings]\n";
			for (const auto& k : kKeys) {
				if (Offered(k.name)) {
					a_out << k.name << '=' << k.get(a_s) << '\n';
				}
			}
		}
	}
}
