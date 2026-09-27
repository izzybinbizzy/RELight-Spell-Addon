// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The wards: one dome per ward, and the colour pick. Everything here steps down when DynamicWards.dll is loaded, which
// dresses every ward itself (his call, 2026-09-24: "have the whole ward system step down if dynamic wards is present").
//
// ONE DOME. A ward spell's rank is the effect that accumulates Ward Power; every OTHER effect on the same spell that wears
// the vanilla ward art draws a second dome and a second hand light - vanilla's ShieldConcSelf, Mysticism's Dummy rows,
// Odin's Ward Regeneration (his capture 2026-09-24: an orange 360 dome with a second dome inside, measured in his game:
// Lesser Ward fires Ward - Lesser and Odin's Ward Regeneration, both on WardBodyFX). Those lose their art and their light.
// An effect on a mod's own ward art (Better Vampires' blood ward) is never touched.
//
// THE COLOUR (his call, 2026-09-24, "relight through skse menu"): Vanilla blue or White. The vanilla ward art forms are
// pointed at our meshes (`meshes\magic\Glow Wards\`, Dynamic Wards' own looks under our names): the dome (with 360 Ward
// the 360 sphere, in place of its orange), the hand art and 360 Ward's hit flash. Vanilla keeps the vanilla dome and hand,
// and gives the 360 sphere and flash the vanilla blue. The hand light follows in HandLights.cpp. Nothing is saved.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::uint32_t kShieldConcSelf = 0x0FCC62;  // Skyrim.esm
		constexpr std::uint32_t kDomeArt = 0x018124;         // Skyrim.esm WardHitEffect: magic\wardbodyfx.nif
		constexpr std::uint32_t kHandArt = 0x0253F1;         // Skyrim.esm: magic\wardinhandfx.nif
		constexpr std::uint32_t kFlashArt = 0x000802;        // 360 Ward.esp: magic\wardShieldHitFX.nif
		constexpr std::uint32_t kWardLight = 0x02F3EF;       // Skyrim.esm MagicLightWardHand01: every ward's own hand light
		constexpr const char*   k360Plugin = "360 Ward.esp";
		constexpr const char*   kEmptyModel = "Effects\\FXEmptyObject.nif";

		RE::BGSArtObject*                                    gEmpty = nullptr;
		std::vector<RE::EffectSetting*>                      gSilenced;
		std::unordered_map<RE::BGSArtObject*, std::string>   gOriginal;  // an art form's model before we pointed it at ours
		bool                                                 gSteppedDownLogged = false;
		std::optional<RE::Color>                             gWardLightColour;  // MagicLightWardHand01's own, before White

		bool DynamicWardsLoaded() { return REX::W32::GetModuleHandleA("DynamicWards.dll") != nullptr; }

		// the vanilla ward art, by the bare mesh name - also once we have pointed it at our own copies, which keep the names
		bool IsVanillaWardArt(const RE::BGSArtObject* a_art)
		{
			const char* model = a_art ? a_art->GetModel() : nullptr;
			if (!model || !*model) {
				return false;
			}
			const auto key = MeshKey(model);
			return key == "wardinhandfx" || key == "wardbodyfx" || key == "wardbodyfx360";
		}

		bool WearsWardArt(const RE::EffectSetting* a_e)
		{
			return a_e && (IsVanillaWardArt(a_e->data.castingArt) || IsVanillaWardArt(a_e->data.hitEffectArt));
		}

		bool IsRank(const RE::EffectSetting* a_e)
		{
			return a_e->data.archetype == RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude &&
			       a_e->data.primaryAV == RE::ActorValue::kWardPower;
		}

		RE::BGSArtObject* Empty()
		{
			if (!gEmpty) {
				if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSArtObject>(); factory) {
					gEmpty = factory->Create();
					if (gEmpty) {
						gEmpty->SetModel(kEmptyModel);
					}
				}
			}
			return gEmpty;
		}

		// every effect that shares a spell with a ward rank and wears the vanilla ward art without being a rank itself
		void FindExtras()
		{
			std::unordered_set<RE::EffectSetting*> extras(gSilenced.begin(), gSilenced.end());
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (auto* shield = dh->LookupForm<RE::EffectSetting>(kShieldConcSelf, "Skyrim.esm"); shield && WearsWardArt(shield)) {
				extras.insert(shield);
			}
			for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
				if (!spell) {
					continue;
				}
				bool                            rank = false;
				std::vector<RE::EffectSetting*> others;
				for (auto* effect : spell->effects) {
					auto* base = effect ? effect->baseEffect : nullptr;
					if (!WearsWardArt(base)) {
						continue;
					}
					if (IsRank(base)) {
						rank = true;
					} else {
						others.push_back(base);
					}
				}
				if (rank) {
					extras.insert(others.begin(), others.end());
				}
			}
			gSilenced.assign(extras.begin(), extras.end());
		}

		std::size_t Silence()
		{
			auto*       empty = Empty();
			std::size_t changed = 0;
			if (!empty) {
				return changed;
			}
			for (auto* e : gSilenced) {
				auto& d = e->data;
				if (d.castingArt != empty || d.hitEffectArt != empty || d.enchantEffectArt || d.light) {
					++changed;
				}
				d.castingArt = empty;
				d.hitEffectArt = empty;
				d.enchantEffectArt = nullptr;
				d.light = nullptr;  // a second hand light (vanilla's white MagicLightWardHand01) goes with the second dome
			}
			return changed;
		}

		// point an art form at our copy, or back at the model it had
		void Point(RE::BGSArtObject* a_art, const char* a_ours)
		{
			if (!a_art) {
				return;
			}
			auto it = gOriginal.find(a_art);
			if (it == gOriginal.end()) {
				const char* now = a_art->GetModel();
				it = gOriginal.emplace(a_art, now ? now : "").first;
			}
			const std::string want = a_ours ? std::string(a_ours) : it->second;
			const char*        now = a_art->GetModel();
			if (!now || want != now) {
				a_art->SetModel(want.c_str());
			}
		}

		void Paint()
		{
			auto*      dh = RE::TESDataHandler::GetSingleton();
			// loaded, not merely present: LookupModByName also finds a plugin that is installed but not enabled
			const bool has360 = dh->LookupLoadedModByName(k360Plugin) || dh->LookupLoadedLightModByName(k360Plugin);
			const bool white = WardColour() == 1;
			auto*      dome = dh->LookupForm<RE::BGSArtObject>(kDomeArt, "Skyrim.esm");
			auto*      hand = dh->LookupForm<RE::BGSArtObject>(kHandArt, "Skyrim.esm");
			auto*      flash = has360 ? dh->LookupForm<RE::BGSArtObject>(kFlashArt, k360Plugin) : nullptr;
			if (has360) {
				Point(dome, white ? "Magic\\Glow Wards\\White\\wardbodyfx360.nif" : "Magic\\Glow Wards\\Blue\\wardbodyfx360.nif");
				Point(flash, white ? "Magic\\Glow Wards\\White\\wardshieldhitfx.nif" : "Magic\\Glow Wards\\Blue\\wardshieldhitfx.nif");
			} else {
				// his call 2026-09-27: Vanilla blue wears OUR dome too - the vanilla look, reading our own copies of its
				// textures, so no other mod's loose textures reach it
				Point(dome, white ? "Magic\\Glow Wards\\White\\wardbodyfx.nif" : "Magic\\Glow Wards\\Blue\\wardbodyfx.nif");
			}
			Point(hand, white ? "Magic\\Glow Wards\\White\\wardinhandfx.nif" : "Magic\\Glow Wards\\Blue\\wardinhandfx.nif");
			// the game's own ward hand light, where no installed layer lights the ward hand (HandLights.cpp does where one does)
			if (auto* light = dh->LookupForm<RE::TESObjectLIGH>(kWardLight, "Skyrim.esm")) {
				if (!gWardLightColour) {
					gWardLightColour = light->data.color;
				}
				light->data.color = white ? RE::Color{ 255, 255, 255, 0 } : *gWardLightColour;
			}
			SKSE::log::info("wards: {} {} ({}), hand {}", has360 ? "360 sphere" : "dome", white ? "white" : "vanilla blue",
				dome && dome->GetModel() ? dome->GetModel() : "no art form", hand && hand->GetModel() ? hand->GetModel() : "no art form");
		}
	}

	bool WardsSteppedDown() { return DynamicWardsLoaded(); }

	void ApplyWards(const char* a_why)
	{
		if (DynamicWardsLoaded()) {
			if (!gSteppedDownLogged) {
				gSteppedDownLogged = true;
				SKSE::log::info("wards: Dynamic Wards is loaded - it dresses the wards, so this plugin leaves them alone");
			}
			return;
		}
		FindExtras();
		const auto changed = Silence();
		SKSE::log::info("wards: {} effect(s) wear the ward art beside a ward's rank and show no second dome ({} changed; {})",
			gSilenced.size(), changed, a_why);
		Paint();
	}
}
