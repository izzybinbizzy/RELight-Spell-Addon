// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// One ward, one dome. Every vanilla ward spell casts two effects that wear ward art: its WardConcSelf rank and
// ShieldConcSelf, the silent second half. With 360 Ward the rank wears the 360 sphere and ShieldConcSelf keeps the vanilla
// dome, so both draw (his report 2026-09-24: "on the relight 360 ward it's showing both 360 and vanilla ward"). Dynamic
// Wards already silences it, so this steps down whenever DynamicWards.dll is loaded (his "have the whole ward system step
// down if dynamic wards is present").

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::uint32_t kShieldConcSelf = 0x0FCC62;  // Skyrim.esm
		constexpr const char*   k360Plugin = "360 Ward.esp";
		constexpr const char*   kEmptyModel = "Effects\\FXEmptyObject.nif";

		RE::BGSArtObject* gEmpty = nullptr;
	}

	void SilenceSecondDome(const char* a_why)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(k360Plugin) || REX::W32::GetModuleHandleA("DynamicWards.dll")) {
			return;
		}
		auto* effect = dh->LookupForm<RE::EffectSetting>(kShieldConcSelf, "Skyrim.esm");
		if (!effect) {
			return;
		}
		if (!gEmpty) {
			if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSArtObject>(); factory) {
				gEmpty = factory->Create();
				if (gEmpty) {
					gEmpty->SetModel(kEmptyModel);
				}
			}
		}
		if (!gEmpty) {
			return;
		}
		auto& d = effect->data;
		const bool changed = d.hitEffectArt != gEmpty || d.castingArt != gEmpty || d.enchantEffectArt;
		d.hitEffectArt = gEmpty;
		d.castingArt = gEmpty;
		d.enchantEffectArt = nullptr;
		if (changed) {
			SKSE::log::info("wards: 360 Ward without Dynamic Wards - ShieldConcSelf wears no dome, so a ward shows one ({})", a_why);
		}
	}
}
