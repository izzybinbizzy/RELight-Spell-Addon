// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// 🖐 THE LIGHT ON THE CASTER'S HANDS - no plugin, no script. HIS CALL, 2026-09-22: *"i never wanted it in a pex
// script in the first place, i already don't want the esp"*.
//
// ⚫ WHY THIS IS NEEDED AT ALL: casting art hangs off the ACTOR, so no RE::Light config on a mesh can reach it.
// What the game does give a spell is its CASTING LIGHT - a light record on the magic effect, which the game makes
// at the hand while the spell is readied and takes away when it ends. So each magic effect whose casting art is a
// mesh this mod lights is pointed, in memory, at a light record made here, in the colour, strength and reach that
// the installed layers give that mesh.
//
// ⚫ WHY RE::Light IS STEPPED AROUND FOR THESE LIGHTS (read off its source, commit 622bbbb, `magicLightThunk`):
// with `disableGameLights=true` it switches off every casting light it has no config for, and a config can only
// name a record from a real plugin - a light made in memory has none. So main.cpp's hook, which runs BEFORE
// RE::Light's (it is installed after every plugin has loaded), makes our lights with the game's own function and
// never hands them to RE::Light. Every other casting light still goes through RE::Light exactly as before.
//
// Nothing is saved: the effects are pointed at our lights when the game loads its data and again whenever a
// switch changes who lights a hand. Take the mod out and every effect keeps the light its own plugin gave it.

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

		// ⚫ the TES flag Community Shaders reads for inverse square lighting (RE::Light's TES_LIGHT_FLAGS_EXT), and the
		// bit it keeps in the light's own words once the light is made - the same one Brightness.cpp reads
		constexpr std::uint32_t kLighInverseSquare = 1u << 14;
		constexpr std::uint32_t kInverseSquare = 1u << 10;

		std::mutex                                                   gLock;
		std::vector<Target>                                          gTargets;
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
			const auto& opts = Options();
			for (const auto& h : it->second) {
				if (h.option >= opts.size() || !opts[h.option].switchable || opts[h.option].on) {
					return &h;
				}
			}
			return nullptr;
		}

		// ⚫ the record the game builds the light from. The numbers the ESP's records carried: a byte copy of vanilla
		// MagicLightFrostHand01's shape (falloff 1, field of view 90, near clip 1, flicker period 1, no amplitudes),
		// with the layer's colour, reach and strength
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

	void MakeHandLights()
	{
		// ⚫ how RE::Light itself decides Community Shaders' inverse square lighting is there (Utility.h). Without it the
		// two words written below are the light's AMBIENT colour, so they are left alone.
		gIsl = std::filesystem::exists("Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli");
		std::size_t made = 0, failed = 0;
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
			for (auto* effect : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EffectSetting>()) {
				if (!effect || !effect->data.castingArt) {
					continue;
				}
				const char* model = effect->data.castingArt->GetModel();
				if (!model || !*model) {
					continue;
				}
				auto key = MeshKey(model);
				if (gCopies.contains(key)) {
					gTargets.push_back({ effect, effect->data.light, std::move(key) });
				}
			}
		}
		SKSE::log::info("hand lights: {} light(s) made in memory, {} failed; {} magic effect(s) wear one of their meshes; "
						"inverse square lighting {}",
			made, failed, gTargets.size(), gIsl ? "found" : "not found");
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
				if (const Hand* h = on ? Winner(key) : nullptr) {
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
		}
		if (a_log) {
			SKSE::log::info("hand lights: {} magic effect(s) lit by this mod, {} left with their own light", lit, given);
		}
	}

	const Hand* HandOfLight(RE::TESObjectLIGH* a_light)
	{
		if (!a_light) {
			return nullptr;
		}
		std::lock_guard l{ gLock };
		auto            it = gInUse.find(a_light);
		return it == gInUse.end() ? nullptr : it->second;
	}

	// what RE::Light did to the light it made from a plugin-light config (LightData.cpp setNiPointLightDataFromCfg and
	// setOverlayData): the strength, the reach with the SIZE in its z, the colour, and - under Community Shaders - the
	// inverse square flag and the cutoff in the two words before the colour
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
			auto* words = reinterpret_cast<std::uint32_t*>(&data);
			if (a_hand.inverseSquare) {
				words[0] |= kInverseSquare;
			}
			*reinterpret_cast<float*>(&words[1]) = std::clamp(a_hand.cutoff, 0.01f, 0.99f);
		}
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
