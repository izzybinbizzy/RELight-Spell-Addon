// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// An enchanted weapon's light follows its charge, a spell hand's light and glow follow the caster's magicka: full when
// charged, dimmer as it is used, a sputter near empty, a pulse when a hit spends charge, a flare when a soul gem refills
// it, a cooler colour near empty. A bound weapon follows the time its spell has left. (Illuminated's own light on a weapon
// no mod lights is NOT here: enchantment coverage is RE::Light's - FadeOwnLight.cpp is a stub that makes none.) Its settings: the menu's "Fading" pages and Data\SKSE\Plugins\RelightSpellAddon\Fading.ini; its
// rule files: Data\SKSE\Plugins\RelightSpellAddon\Fading\*.json. The Spell Addon's main.cpp calls the four entry points below.
//
// THE FILES: FadeGlow.h (the behaviour as plain numbers), FadeFormText.h / FadeSettingsText.h / FadeRulesText.h (text),
// FadeCharge.cpp (what a hand holds), FadeSpells.cpp (spells in hand), FadeLights.cpp (the lights found and scaled every
// frame), FadeOwnLight.cpp (our own light where none is), FadeRules.cpp, FadeSettings.cpp, FadeEditorIDs.cpp,
// FadeMenu.cpp, FadeDevBench.cpp, Fade.h (what they share), FadeMain.cpp (this file: its own hooks and the order).

#include "Fade.h"

namespace
{
	// The player's update, each reference effect's update (0x3B) and the cell's animation call are wrapped ONCE, in
	// the Spell Addon's main.cpp, which calls UpdateHands / AfterReferenceEffect / ReapplyAll after the Brightness slider's work
	// (two wrappers on one slot would leave their order to who installed last).

	// ------------------------------------------------------------------ after the game animates an effect shader
	// ShaderReferenceEffect::Update (0x28) writes this frame's fill and rim alpha; the experimental shader dimming runs
	// after it.
	struct ShaderUpdate
	{
		static bool thunk(RE::ShaderReferenceEffect* a_this, float a_delta)
		{
			const bool result = func(a_this, a_delta);
			Fade::AfterShaderEffect(a_this);
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::ShaderReferenceEffect::VTABLE[0] };
			func = vtbl.write_vfunc(0x28, thunk);
		}
	};

	// ------------------------------------------------------------------ the HUD's enchantment charge bar
	// HUDChargeMeter::Update hands the HUD movie its numbers in two calls to SetChargeMeterPercent(percent, force,
	// leftHand, show) - the two calls TrueHUD also reads for its own bar (ersh1/TrueHUD, Hooks.h). With HideChargeBar on,
	// both go out with show = false, which the vanilla HUD, SkyHUD and TrueHUD all take as "hide the bar". Installed at
	// data load, after TrueHUD hooked the same calls at plugin load, so TrueHUD is handed what we pass on.
	// SE/AE only: the call sites are known for those two.
	template <int N>
	struct ChargeBar
	{
		static bool thunk(RE::GFxValue::ObjectInterface* a_this, void* a_data, RE::GFxValue* a_result, const char* a_name,
			const RE::GFxValue* a_args, RE::UPInt a_count, bool a_isDisplayObject)
		{
			const auto settings = Fade::Config();
			if (settings.debugLog && a_args && a_count == 4 && a_args[0].IsNumber() && a_args[2].IsBool() && a_args[3].IsBool()) {
				// the debug log: each change of what the game hands the HUD (it calls this every frame)
				static std::tuple<int, bool, bool> last[2]{ { -1, false, false }, { -1, false, false } };
				const std::tuple<int, bool, bool>  now{ static_cast<int>(a_args[0].GetNumber()), a_args[2].GetBool(), a_args[3].GetBool() };
				if (std::exchange(last[N], now) != now) {
					SKSE::log::info("charge bar ({}): {}% {} hand, show {}{}", N, std::get<0>(now), std::get<1>(now) ? "left" : "right",
						std::get<2>(now), settings.hideChargeBar ? " - hidden by RELight - Spell Addon" : "");
				}
			}
			if (a_args && a_count == 4 && settings.hideChargeBar) {
				RE::GFxValue args[4]{ a_args[0], a_args[1], a_args[2], a_args[3] };
				args[3].SetBoolean(false);
				return func(a_this, a_data, a_result, a_name, args, a_count, a_isDisplayObject);
			}
			return func(a_this, a_data, a_result, a_name, a_args, a_count, a_isDisplayObject);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static bool                                    Install(std::ptrdiff_t a_offset)
		{
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(50771, 51666), a_offset };  // HUDChargeMeter::Update
			if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
				return false;
			}
			func = SKSE::GetTrampoline().write_call<5>(target.address(), thunk);
			return true;
		}
	};

	void InstallChargeBar()
	{
		if (REL::Module::IsVR()) {
			SKSE::log::info("VR: the charge bar cannot be hidden");
			return;
		}
		const bool ok = ChargeBar<0>::Install(0x168) && ChargeBar<1>::Install(0x2B3);
		SKSE::log::info("{}", ok ? "the charge bar's two updates hooked" :
								   "the charge bar's updates are not where expected (another plugin rewrote them?); it cannot be hidden");
	}
}

namespace Fade
{
	void OnPluginLoad()
	{
		// the trampoline for the two charge-bar calls is main.cpp's (SKSE::Init, made once)
		InstallEditorIDHooks();  // before the game reads its plugins: the rule files may name forms by editor ID
	}

	void OnPostLoad() { OfferToDevBench(); }

	void OnDataLoaded()
	{
		LoadSettings();
		LoadRules();
		ShaderUpdate::Install();
		InstallChargeBar();
		SKSE::log::info("fading: hooked effect shaders; the player update, effect updates and cell animations are main.cpp's wrappers");
		RegisterMenu();
	}

	void OnGameLoading()
	{
		// the weapons about to load (or a new game's) are new copies: forget the old ones, putting their lights back first,
		// and the rule matches of enchantments made in play
		ReleaseAll();
		ForgetRuleMatches();
	}
}
