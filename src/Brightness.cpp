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
// ⚫ HOW, and why this way: like RE::Light's slider it scales FADE only, so a dimmer light keeps its reach.
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
			float                      base{ 0.0f };
			float                      written{ -1.0f };
		};

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
			auto& fade = niLight->GetLightRuntimeData().fade;
			if (fade != s.written) {
				s.base = fade;  // written since we last touched it: that is the new base
			}
			const float want = s.base * scale;
			if (fade != want) {
				fade = want;
			}
			s.written = want;
		}
		// a light only this list still holds has left the game
		if ((++gFrame & 15) == 0) {
			std::erase_if(gSeen, [](const auto& kv) { return !kv.second.light || kv.second.light->GetRefCount() <= 1; });
		}
	}
}
