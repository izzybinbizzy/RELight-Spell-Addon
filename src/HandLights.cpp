// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The light on the caster's hands - no plugin, no script.
//
// Casting art hangs off the actor, so no RE::Light mesh config reaches it. Instead each magic effect whose casting
// art is a mesh this mod lights is pointed, in memory, at its casting light: a light record made here in the colour,
// strength and reach the installed layers give that mesh. RE::Light switches off casting lights it has no config for,
// and an in-memory record can have none, so main.cpp's hook makes these lights itself and never hands them on.
// Nothing is saved; take the mod out and every effect keeps its own light.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct Target
		{
			RE::EffectSetting*  effect;
			RE::TESObjectLIGH*  own;  // the light its plugin gave it
			std::string         key;
		};

		constexpr std::uint32_t kLighInverseSquare = 1u << 14;  // Community Shaders' inverse square flag on a LIGH record

		// a hand light already lit: a menu change reaches it at once instead of on the next cast (his report, 2026-09-23:
		// "i have to dispel the spell then re-cast")
		struct Live
		{
			RE::NiPointer<RE::NiLight> light;
			std::string                key;
			bool                       heldOut{ false };  // put out because nothing lights its key any more
		};

		std::mutex                                                   gLock;
		std::vector<Target>                                          gTargets;
		std::vector<Live>                                            gLive;
		std::unordered_map<std::string, RE::TESObjectLIGH*>          gCopies;
		std::unordered_map<const RE::TESObjectLIGH*, const Hand*>    gInUse;
		std::size_t                                                  gLit = 0;
		bool                                                         gIsl = false;

		RE::TESObjectLIGH* NewLight()
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectLIGH>();
			return factory ? factory->Create() : nullptr;
		}

		// the layer that lights this hand now: the highest order whose option is not switched off
		const Hand* Winner(const std::string& a_key)
		{
			const auto& hands = Hands();
			auto        it = hands.find(a_key);
			if (it == hands.end()) {
				return nullptr;
			}
			for (const auto& h : it->second) {
				if (OptionLit(h.option)) {
					return &h;
				}
			}
			return nullptr;
		}

		// the ward colour pick (Wards.cpp): White gives the ward's hand light white, in the layer's own strength and reach
		const Hand* Picked(const Hand* a_hand)
		{
			if (!a_hand || WardColour() != 1 || MeshKey(a_hand->key) != "wardinhandfx") {
				return a_hand;
			}
			static std::unordered_map<const Hand*, Hand> white;  // under gLock, like every caller
			auto [it, fresh] = white.try_emplace(a_hand, *a_hand);
			if (fresh) {
				it->second.color = { 1.0f, 1.0f, 1.0f };
				it->second.rgb[0] = it->second.rgb[1] = it->second.rgb[2] = 255;
			}
			return &it->second;
		}

		// vanilla MagicLightFrostHand01's shape (falloff 1, field of view 90, near clip 1) with the layer's colour, reach and strength
		void Fill(RE::TESObjectLIGH* a_light, const Hand& a_hand)
		{
			auto& d = a_light->data;
			d.time = -1;
			d.radius = static_cast<std::uint32_t>(std::lround((std::max)(a_hand.radius, 0.0f)));
			d.color.red = a_hand.rgb[0];
			d.color.green = a_hand.rgb[1];
			d.color.blue = a_hand.rgb[2];
			d.color.alpha = 0;
			std::uint32_t flags = a_hand.portalStrict ? static_cast<std::uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict) : 0u;
			if (a_hand.inverseSquare) {
				flags |= kLighInverseSquare;
			}
			d.flags = static_cast<RE::TES_LIGHT_FLAGS>(flags);
			d.fallofExponent = 1.0f;
			d.fov = 90.0f;
			d.nearDistance = 1.0f;
			d.flickerPeriodRecip = 1.0f;
			d.flickerIntensityAmplitude = 0.0f;
			d.flickerMovementAmplitude = 0.0f;
			a_light->fade = a_hand.fade;
		}
	}

	namespace
	{
		// Which magic effects wear a lit mesh. Found at data load and again when a save loads, because another plugin
		// (Dynamic Wards) can change casting art in memory too. An effect's own light is kept across a re-find.
		// -> how many matched by full path
		std::size_t FindTargets()
		{
			std::unordered_map<RE::EffectSetting*, RE::TESObjectLIGH*> own;
			for (auto& t : gTargets) {
				own.emplace(t.effect, t.own);
			}
			std::unordered_set<const RE::TESObjectLIGH*> ours;
			for (auto& [_k, c] : gCopies) {
				ours.insert(c);
			}
			gTargets.clear();
			std::size_t byPath = 0;
			for (auto* effect : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EffectSetting>()) {
				if (!effect || !effect->data.castingArt) {
					continue;
				}
				const char* model = effect->data.castingArt->GetModel();
				if (!model || !*model) {
					continue;
				}
				// the full path first (a mod's own art under a shared file name), then the bare name
				auto key = PathKey(model);
				if (gCopies.contains(key)) {
					++byPath;
				} else {
					key = MeshKey(model);
				}
				if (!gCopies.contains(key)) {
					continue;
				}
				RE::TESObjectLIGH* mine = effect->data.light;
				if (auto it = own.find(effect); it != own.end()) {
					mine = it->second;
				} else if (ours.contains(mine)) {
					mine = nullptr;
				}
				gTargets.push_back({ effect, mine, std::move(key) });
			}
			// an effect whose casting art moved to a mesh no layer lights is no target now: it gets its own light back,
			// or it would keep our light for the art it no longer wears
			for (const auto& [effect, light] : own) {
				if (ours.contains(effect->data.light) &&
					std::ranges::none_of(gTargets, [effect](const Target& a_t) { return a_t.effect == effect; })) {
					effect->data.light = light;
				}
			}
			return byPath;
		}
	}

	void RefindHandLights()
	{
		std::size_t n = 0, byPath = 0;
		{
			std::lock_guard l{ gLock };
			if (gCopies.empty()) {
				return;
			}
			byPath = FindTargets();
			n = gTargets.size();
		}
		SKSE::log::info("hand lights: re-found - {} magic effect(s) wear one of their meshes ({} by full path)", n, byPath);
		ApplyHandLights(true);
	}

	void MakeHandLights()
	{
		// RE::Light's own test for inverse square lighting; without it the two words DressHandLight writes are ambient colour
		gIsl = std::filesystem::exists("Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli");
		std::size_t made = 0, failed = 0, byPath = 0;
		{
			std::lock_guard l{ gLock };
			gCopies.clear();
			gTargets.clear();
			for (const auto& [key, list] : Hands()) {
				if (list.empty()) {
					continue;
				}
				auto* copy = NewLight();
				if (!copy) {
					++failed;
					continue;
				}
				Fill(copy, list.front());
				gCopies.emplace(key, copy);
				++made;
			}
			byPath = FindTargets();
		}
		SKSE::log::info("hand lights: {} light(s) made in memory, {} failed; {} magic effect(s) wear one of their meshes ({} by "
						"their full art path); inverse square lighting {}",
			made, failed, gTargets.size(), byPath, gIsl ? "found" : "not found");
		ApplyHandLights(true);
	}

	void ApplyHandLights(bool a_log)
	{
		std::size_t lit = 0, given = 0;
		{
			std::lock_guard l{ gLock };
			gInUse.clear();
			const bool on = HandLightsOn();
			for (auto& [key, copy] : gCopies) {
				if (const Hand* h = on ? Picked(Winner(key)) : nullptr) {
					Fill(copy, *h);
					gInUse[copy] = h;
				}
			}
			for (auto& t : gTargets) {
				auto* copy = gCopies[t.key];
				auto* want = gInUse.contains(copy) ? copy : t.own;
				if (t.effect->data.light != want) {
					t.effect->data.light = want;
				}
				if (want == copy) {
					++lit;
				} else {
					++given;
				}
			}
			gLit = lit;
			// the lights already in a caster's hands take the change now; one whose key nothing lights any more goes out
			// until the next cast gives the effect its own light back
			std::erase_if(gLive, [](const Live& a_l) { return !a_l.light || a_l.light->GetRefCount() <= 1; });
			for (auto& live : gLive) {
				if (const Hand* h = on ? Picked(Winner(live.key)) : nullptr) {
					if (live.heldOut) {
						live.light->SetAppCulled(false);
						live.heldOut = false;
					}
					DressHandLight(live.light.get(), *h);
					RememberLight(live.light.get(), HandFxOf(h->key));
				} else if (!live.heldOut) {
					live.light->SetAppCulled(true);
					live.heldOut = true;
				}
			}
		}
		if (a_log) {
			SKSE::log::info("hand lights: {} magic effect(s) lit by this mod, {} left with their own light", lit, given);
		}
	}

	const Hand* HandOfLight(const RE::TESObjectLIGH* a_light)
	{
		if (!a_light) {
			return nullptr;
		}
		std::lock_guard l{ gLock };
		auto            it = gInUse.find(a_light);
		return it == gInUse.end() ? nullptr : it->second;
	}

	// dressed as RE::Light dresses a plugin light: fade, reach with the size in z, colour, and under Community Shaders
	// the inverse square flag and cutoff
	void DressHandLight(RE::NiLight* a_light, const Hand& a_hand)
	{
		if (!a_light) {
			return;
		}
		auto& data = a_light->GetLightRuntimeData();
		data.fade = a_hand.fade;
		data.radius = { a_hand.radius, a_hand.radius, a_hand.size };
		data.diffuse = a_hand.color;
		if (gIsl) {
			if (a_hand.inverseSquare) {
				Isl::SetOn(a_light);
			}
			Isl::SetCutoff(a_light, a_hand.cutoff);
		}
	}

	void NoteHandLight(RE::NiLight* a_light, const Hand& a_hand)
	{
		if (!a_light) {
			return;
		}
		std::lock_guard l{ gLock };
		if (gLive.size() >= 64) {
			std::erase_if(gLive, [](const Live& a_l) { return !a_l.light || a_l.light->GetRefCount() <= 1; });
		}
		gLive.push_back({ RE::NiPointer<RE::NiLight>(a_light), a_hand.key });
	}

	bool HandLightHeldOut(const RE::NiLight* a_light)
	{
		std::lock_guard l{ gLock };
		return std::ranges::any_of(gLive, [a_light](const Live& a_l) { return a_l.heldOut && a_l.light.get() == a_light; });
	}

	std::size_t HandLightsMade()
	{
		std::lock_guard l{ gLock };
		return gCopies.size();
	}

	std::size_t HandEffects()
	{
		std::lock_guard l{ gLock };
		return gLit;
	}
}
