// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// VAER Reborn on Thaumaturgy's own effects. Thaumaturgy points the Fear, Paralyze, Turn Undead, Banish, Silent Moons and
// second Absorb enchantments at new magic effects of its own, and VAER Reborn only dresses the vanilla ones, so those
// weapons had no swirl. Each copy is given the enchant art and the enchant shader VAER made for the vanilla effect. Once, at
// data loaded, and only when VAEReborn.esp and Thaumaturgy.esp are both loaded and the VAER Reborn option of the Weapons
// download is installed: its enchantment lights are keyed by VAER's shaders, so without it the swap would leave the weapon
// unlit.
//
// The table is the one list Let There Be Glow's ESL and Illuminated share (gen.VAER_SWIRL_FORMS); relightgen's selftest
// refuses a table here that is not that list, row for row.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct SwirlCopy
		{
			std::string_view effectPlugin;
			RE::FormID       effect;
			std::string_view artPlugin;
			RE::FormID       art;
			std::string_view shaderPlugin;
			RE::FormID       shader;
			std::string_view name;
		};

		// VAER_SWIRL_FORMS begin
		constexpr SwirlCopy kSwirlCopies[] = {
			{ "Thaumaturgy.esp", 0x1904A2, "VAEReborn.esp", 0x000807, "VAEReborn.esp", 0x00082C, "MAG_EnchFearFFContact" },
			{ "Thaumaturgy.esp", 0x1904A0, "VAEReborn.esp", 0x00080B, "VAEReborn.esp", 0x000823, "MAG_EnchParalyzeFFContact" },
			{ "Thaumaturgy.esp", 0x1904A3, "VAEReborn.esp", 0x00080E, "VAEReborn.esp", 0x00082F, "MAG_EnchTurnUndeadFFContact01" },
			{ "Thaumaturgy.esp", 0x30C4C4, "VAEReborn.esp", 0x00080E, "VAEReborn.esp", 0x00082F, "MAG_EnchTurnUndeadFFContact02" },
			{ "Thaumaturgy.esp", 0x30C4C5, "VAEReborn.esp", 0x00080E, "VAEReborn.esp", 0x00082F, "MAG_EnchTurnUndeadFFContact03" },
			{ "Thaumaturgy.esp", 0x1904A4, "VAEReborn.esp", 0x000803, "VAEReborn.esp", 0x000827, "MAG_EnchBanishFFContact01" },
			{ "Thaumaturgy.esp", 0x3115CA, "VAEReborn.esp", 0x00080A, "VAEReborn.esp", 0x000826, "MAG_EnchSilentMoonsEnchFFContact02" },
			{ "Thaumaturgy.esp", 0x427D9E, "VAEReborn.esp", 0x000801, "VAEReborn.esp", 0x00082A, "MAG_EnchAbsorbMagickaFFContact02" },
			{ "Thaumaturgy.esp", 0x42CEA1, "VAEReborn.esp", 0x000802, "VAEReborn.esp", 0x000826, "MAG_EnchAbsorbStaminaFFContact02" },
		};
		// VAER_SWIRL_FORMS end

		// loaded, not merely present: LookupModByName also finds a plugin that is installed but not enabled
		[[nodiscard]] bool Loaded(std::string_view a_name)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && (dh->LookupLoadedModByName(a_name) || dh->LookupLoadedLightModByName(a_name));
		}

		// the VAER Reborn option's data file is there only when the player ticked the option in the installer
		[[nodiscard]] bool VaerOptionInstalled()
		{
			for (const auto& option : Options()) {
				if (option.id == "Weapons - VAER Reborn") {
					return true;
				}
			}
			return false;
		}
	}

	void VaerSwirls()
	{
		if (!Loaded("VAEReborn.esp") || !Loaded("Thaumaturgy.esp")) {
			SKSE::log::info("VAER on Thaumaturgy: VAEReborn.esp and Thaumaturgy.esp are not both loaded; nothing to do");
			return;
		}
		if (!VaerOptionInstalled()) {
			SKSE::log::info("VAER on Thaumaturgy: the VAER Reborn option is not installed; Thaumaturgy's effects keep their own art");
			return;
		}
		auto*       dh = RE::TESDataHandler::GetSingleton();
		std::size_t set = 0, missing = 0;
		for (const auto& c : kSwirlCopies) {
			auto* effect = dh->LookupForm<RE::EffectSetting>(c.effect, c.effectPlugin);
			auto* art = dh->LookupForm<RE::BGSArtObject>(c.art, c.artPlugin);
			auto* shader = dh->LookupForm<RE::TESEffectShader>(c.shader, c.shaderPlugin);
			if (!effect || !art || !shader) {
				++missing;
				SKSE::log::warn("[VAER] {}: a form is not in this load order (effect {}, art {}, shader {})", c.name,
					effect != nullptr, art != nullptr, shader != nullptr);
				continue;
			}
			effect->data.enchantEffectArt = art;
			effect->data.enchantShader = shader;
			if (effect->data.enchantEffectArt == art && effect->data.enchantShader == shader) {
				++set;
				SKSE::log::info("[VAER] {} now wears VAER's swirl {:08X} and shader {:08X}", c.name, art->GetFormID(),
					shader->GetFormID());
			}
		}
		SKSE::log::info("VAER on Thaumaturgy: {} of {} effect(s) given VAER's swirl, {} not found", set, std::size(kSwirlCopies),
			missing);
	}
}
