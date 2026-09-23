// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Our own Brightness and Reach sliders. RE::Light's multiplier scales every RE::Light light in the game; these
// touch only this mod's lights (RE::Light's lights on objects our data names, our hand lights and spray lights;
// Streams.cpp scales its own). Under inverse square lighting reach = sqrt(K * fade / cutoff - size²), so:
//     Brightness  scales fade and cutoff by one ratio  -> the peak moves, the reach is held
//     Reach       scales the radius, cutoff re-derived -> the reach moves, the peak is held
// RE::Light rewrites fade every frame on a light that flickers or pulses, so each light remembers the fade we last
// wrote; anything else it carries is a new base. A hand light with Dynamic Lighting runs RE::Light's own oscillator
// here. Runs last in the player update, after RE::Light's.

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
			float                      baseCutoff{ -1.0f }; // the cutoff RE::Light (or its editor) last gave it
			float                      wroteCutoff{ -2.0f };
			const HandFx*              fx{ nullptr };
			float                      phase[3]{};
		};

		// RE::Light's oscillators (everyFrame.h): Pulse advances one phase by perSecond * dt * 5; Flicker walks three
		// phases by a random 1.1-13.1 * perSecond * dt. The fade is (osc * intensity + 1 - intensity) of its base.
		float Oscillate(Seen& a_s, float a_dt)
		{
			static std::minstd_rand rng{ 20260923 };
			static std::uniform_real_distribution<float> step{ 1.1f, 13.1f };
			constexpr float tau = 6.2831853f;
			float           osc = 0.0f;
			if (a_s.fx->flicker) {
				for (auto& ph : a_s.phase) {
					ph = std::fmod(ph + step(rng) * a_s.fx->perSecond * a_dt, tau);
					osc += (std::sin(ph) + 1.0f) / 6.0f;
				}
			} else {
				a_s.phase[0] = std::fmod(a_s.phase[0] + a_s.fx->perSecond * a_dt * 5.0f, tau);
				osc = (std::sin(a_s.phase[0]) + 1.0f) / 2.0f;
			}
			return osc * a_s.fx->intensity + 1.0f - a_s.fx->intensity;
		}

		float ReadCutoff(RE::NiLight* a_light)
		{
			const auto* words = reinterpret_cast<const std::uint32_t*>(&a_light->GetLightRuntimeData());
			return *reinterpret_cast<const float*>(&words[1]);
		}

		// Community Shaders' inverse square flag and cutoff live in the two words before the colour; a light
		// without the flag is not rendered that way, so its cutoff is left alone
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
		std::vector<std::pair<RE::NiPointer<RE::NiLight>, const HandFx*>> gHand;  // hand lights made since the last frame
		std::uint32_t                          gFrame = 0;

		// a spray's light, however RE::Light made it: it hangs on a flame or cone projectile whose light is a spray record
		bool IsSprayProjectileLight(RE::NiLight* a_light)
		{
			auto* ref = ReferenceOf(a_light);
			auto* base = ref ? ref->GetBaseObject() : nullptr;
			auto* proj = base ? base->As<RE::BGSProjectile>() : nullptr;
			return proj && (proj->IsFlamethrower() || proj->IsCone()) && IsSprayLight(proj->data.light);
		}

		bool OursByObject(RE::NiLight* a_light)
		{
			if (IsSprayProjectileLight(a_light)) {
				return true;
			}
			const char* n = a_light->name.c_str();
			if (!n || n[0] != 'R' || n[1] != 'L') {
				return false;
			}
			auto* ref = ReferenceOf(a_light);
			return ref && OptionOf(ref->GetBaseObject()) != kNone;
		}
	}

	void RememberHandLight(RE::NiLight* a_light, const HandFx* a_fx)
	{
		if (a_light) {
			std::lock_guard l{ gHandLock };
			gHand.emplace_back(RE::NiPointer<RE::NiLight>(a_light), a_fx);
		}
	}

	void UpdateBrightness()
	{
		{
			std::lock_guard l{ gHandLock };
			for (auto& [h, fx] : gHand) {
				auto& s = gSeen[h.get()];
				s.light = h;
				s.ours = true;
				s.fx = fx;
			}
			gHand.clear();
		}
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		static auto last = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		const float dt = (std::min)(std::chrono::duration<float>(now - last).count(), 0.25f);
		last = now;
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
			const float want = s.base * scale * (s.fx ? Oscillate(s, dt) : 1.0f);
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
			// the cutoff follows the same rule, so an edit in RE::Light's editor sticks: a cutoff we did not write is the
			// new base, both sliders at 100% leave it alone, otherwise the reach it implies is scaled by Reach only
			if (HasOverlay(niLight)) {
				const float now = ReadCutoff(niLight);
				if (now != s.wroteCutoff) {
					s.baseCutoff = now;
				}
				float cutoff = s.baseCutoff;
				if ((scale != 1.0f || reach != 1.0f) && s.baseCutoff > 0.0f && s.base > 0.0f) {
					const float size = data.radius.z;
					const float r0sq = (std::max)(kK * s.base / s.baseCutoff - size * size, 1.0f);
					const float r = std::sqrt(r0sq) * reach;
					cutoff = std::clamp(kK * want / (r * r + size * size), 0.01f, 0.99f);
				}
				if (cutoff != now) {
					WriteCutoff(niLight, cutoff);
				}
				s.wroteCutoff = ReadCutoff(niLight);
			}
		}
		// a light only this list still holds has left the game
		if ((++gFrame & 15) == 0) {
			std::erase_if(gSeen, [](const auto& kv) { return !kv.second.light || kv.second.light->GetRefCount() <= 1; });
		}
	}
}
