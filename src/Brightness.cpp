// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// OUR OWN BRIGHTNESS SLIDER - it scales this mod's lights and nothing else.
//
// ⚫ WHY IT EXISTS (§0.13 of the ReLight handoff): RE::Light's own slider, lightBrightnessMultiplier, scales
// EVERY RE::Light light in the game, so on a load order with another RE::Light addon it dims their lights
// too. This one touches only the lights this mod makes: RE::Light's lights on objects our data files name,
// the hand lights (records in our own light plugin) and the travelling lights (Streams.cpp does its own).
// It is also what let the Reduced tier go: Reduced was the same color and reach at 0.2875 of the peak, and
// the slider at about 30% is that.
//
// ⛔⛔ AND HERE IS THE THING THIS FILE HAD WRONG UNTIL 2026-09-22, IN HIS WORDS: *"the brightness slider
// says reach doesn't change but when you make it brighter it goes further away...thats called reach."*
// He is right. Scaling FADE alone keeps the reach only under the game's own attenuation. He runs
// COMMUNITY SHADERS with inverse square lighting, and there the reach is
//     reach = sqrt(K * fade / cutoff - size²)
// so fade drags the reach with it unless the cutoff moves by the same ratio. It is the same lesson the
// rune lift learned on the Light Placer side the night before, in a second place.
// ✅ SO THERE ARE TWO SLIDERS NOW AND EACH ONE HOLDS THE OTHER STEADY:
//     Brightness  scales fade, and scales cutoff by the SAME ratio  -> the peak moves, the reach does not
//     Reach       scales the radius, and re-derives cutoff from it  -> the reach moves, the peak does not
// Both come out of one formula, the house `cutoff = K * fade / (reach² + size²)` every config was
// written from, so a light this touches lands exactly where a config with those numbers would have.
//
// ⚫ HOW, and why this way: like RE::Light's slider it scales FADE, so a light keeps its colour and shape.
// RE::Light writes a light's fade once when it makes it - and again EVERY frame for a light that flickers,
// pulses or has a fade curve (everyFrame.h, updateLights). So each light remembers the fade it was last
// given here: if what it carries now is not that, RE::Light (or the game) has written it since, and that
// new value is the base to scale. This runs last in the player update, after RE::Light's own.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct Seen
		{
			RE::NiPointer<RE::NiLight> light;
			bool                       ours{ false };
			float                      base{ 0.0f };       // the fade RE::Light last gave it
			float                      written{ -1.0f };   // the fade we last wrote
			float                      baseRadius{ 0.0f }; // the reach RE::Light last gave it
			float                      wroteRadius{ -1.0f };
		};

		// ⚫ Community Shaders reads its inverse square flag and its cutoff out of the two words before the
		// colour - RE::Light's own `Overlay` writes exactly those two. A light without the flag is not being
		// rendered that way, so its cutoff is left alone.
		constexpr std::uint32_t kInverseSquare = 1u << 10;

		bool HasOverlay(RE::NiLight* a_light)
		{
			const auto* words = reinterpret_cast<const std::uint32_t*>(&a_light->GetLightRuntimeData());
			return (words[0] & kInverseSquare) != 0;
		}

		void WriteCutoff(RE::NiLight* a_light, float a_cutoff)
		{
			auto* words = reinterpret_cast<std::uint32_t*>(&a_light->GetLightRuntimeData());
			*reinterpret_cast<float*>(&words[1]) = std::clamp(a_cutoff, 0.01f, 0.99f);
		}

		std::unordered_map<RE::NiLight*, Seen> gSeen;
		std::mutex                             gHandLock;
		std::vector<RE::NiPointer<RE::NiLight>> gHand;  // hand lights made since the last frame
		std::uint32_t                          gFrame = 0;

		RE::TESObjectREFR* ReferenceOf(RE::NiAVObject* a_obj)
		{
			for (auto* o = a_obj; o; o = o->parent) {
				if (auto* ref = o->GetUserData()) {
					return ref;
				}
			}
			return nullptr;
		}

		bool OursByObject(RE::NiLight* a_light)
		{
			const char* n = a_light->name.c_str();
			if (!n || n[0] != 'R' || n[1] != 'L') {
				return false;
			}
			auto* ref = ReferenceOf(a_light);
			return ref && OptionOf(ref->GetBaseObject()) != kNone;
		}
	}

	void RememberHandLight(RE::NiLight* a_light)
	{
		if (a_light) {
			std::lock_guard l{ gHandLock };
			gHand.emplace_back(a_light);
		}
	}

	void UpdateBrightness()
	{
		{
			std::lock_guard l{ gHandLock };
			for (auto& h : gHand) {
				auto& s = gSeen[h.get()];
				s.light = h;
				s.ours = true;
			}
			gHand.clear();
		}
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		const float scale = Brightness();
		const float reach = Reach();
		for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			auto  it = gSeen.find(niLight);
			if (it == gSeen.end()) {
				// only OUR lights are remembered: holding a reference to every light in the game would keep
				// lights alive that the game has let go. Deciding "ours" is cheap - a name, then a cached form.
				if (!OursByObject(niLight)) {
					continue;
				}
				it = gSeen.emplace(niLight, Seen{ RE::NiPointer<RE::NiLight>(niLight), true }).first;
			}
			auto& s = it->second;
			auto& data = niLight->GetLightRuntimeData();
			auto& fade = data.fade;
			if (fade != s.written) {
				s.base = fade;  // written since we last touched it: that is the new base
			}
			// the same remember-what-we-wrote rule for the reach: x and y are the reach, z is the SIZE
			if (data.radius.x != s.wroteRadius) {
				s.baseRadius = data.radius.x;
			}
			const float want = s.base * scale;
			if (fade != want) {
				fade = want;
			}
			s.written = want;
			const float wantRadius = s.baseRadius * reach;
			if (data.radius.x != wantRadius) {
				data.radius.x = wantRadius;
				data.radius.y = wantRadius;
			}
			s.wroteRadius = wantRadius;
			// ⚫ AND THE CUTOFF, WHICH IS WHAT ACTUALLY DECIDES THE REACH UNDER INVERSE SQUARE. Re-derived
			// from the house formula with the fade and reach this light now carries, so Brightness moves the
			// peak without the reach following it and Reach moves the reach without the peak following it.
			if (HasOverlay(niLight)) {
				const float size = data.radius.z;
				WriteCutoff(niLight, kK * want / (wantRadius * wantRadius + size * size));
			}
		}
		// a light only this list still holds has left the game
		if ((++gFrame & 15) == 0) {
			std::erase_if(gSeen, [](const auto& kv) { return !kv.second.light || kv.second.light->GetRefCount() <= 1; });
		}
	}
}
