// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Our own Brightness and Reach sliders. RE::Light's multiplier scales every RE::Light light in the game; these touch
// only this mod's lights (RE::Light's lights on objects our data names, our hand lights and spray lights; Streams.cpp
// scales its own). Under inverse square lighting reach = sqrt(K * fade / cutoff - size²), so:
//     Brightness  scales fade and cutoff by one ratio  -> the peak moves, the reach is held
//     Reach       scales the radius, cutoff re-derived -> the reach moves, the peak is held
// RE::Light rewrites fade every frame on a light that flickers or pulses, so each light remembers the fade we last
// wrote; anything else it carries is a new base. Each option's own slider multiplies in the same way, from that base, so
// no slider ever scales a value we wrote. A hand light with Dynamic Lighting breathes or crackles here (Oscillate),
// and a light on an object an art pick recolours (`tint`) takes that colour. Runs last in the player update.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct Seen
		{
			RE::NiPointer<RE::NiLight> light;
			const HandFx*              fx{ nullptr };
			const RE::NiColor*         tint{ nullptr };
			std::size_t                option{ kNone };     // whose own slider it follows
			float                      base{ 0.0f };        // the fade RE::Light last gave it
			float                      written{ -1.0f };    // the fade we last wrote
			float                      baseRadius{ 0.0f };  // the reach RE::Light last gave it
			float                      wroteRadius{ -1.0f };
			float                      baseCutoff{ -1.0f };  // the cutoff RE::Light (or its editor) last gave it
			float                      wroteCutoff{ -2.0f };
			std::array<float, 3>       phase{};
		};

		// Dynamic Lighting on a hand light, as Let There Be Glow's Light Placer curves look:
		//   Pulse (a breathe): RE::Light's pulse - one phase advancing perSecond * dt * 5, a smooth swing of `intensity`.
		//   Flicker (a crackle): Light Placer's stepped fade - the light JUMPS between a high and a low level, twice a cycle,
		//   `intensity` deep. A random walk of smooth sines (RE::Light's flicker, the first version here) averaged out to about
		//   8% and read as static (his report, 2026-09-23, measured).
		[[nodiscard]] float Oscillate(Seen& a_s, float a_dt)
		{
			static std::minstd_rand                      rng{ 20260923 };
			static std::uniform_real_distribution<float> unit{ 0.0f, 1.0f };
			constexpr float                              tau = std::numbers::pi_v<float> * 2.0f;
			constexpr float                              kRatePerHz = 1.2566f;  // gen.RELIGHT_RATE_PER_HZ: perSecond back to cycles a second
			const auto&                                  fx = *a_s.fx;
			if (!fx.flicker) {
				a_s.phase[0] = std::fmod(a_s.phase[0] + fx.perSecond * a_dt * 5.0f, tau);
				return (std::sin(a_s.phase[0]) + 1.0f) / 2.0f * fx.intensity + 1.0f - fx.intensity;
			}
			// phase[0]: time into this step; phase[1]: the level held; phase[2]: 1 while on a high step, 0 on a low one
			const float hold = 0.5f / (std::max)(fx.perSecond / kRatePerHz, 0.1f);
			a_s.phase[0] += a_dt;
			if (a_s.phase[1] <= 0.0f || a_s.phase[0] >= hold) {
				a_s.phase[0] = std::fmod(a_s.phase[0], hold);
				a_s.phase[2] = a_s.phase[2] > 0.5f ? 0.0f : 1.0f;
				const float dip = a_s.phase[2] > 0.5f ? 0.3f * unit(rng) : 0.6f + 0.4f * unit(rng);  // high steps near full, low near the bottom
				a_s.phase[1] = 1.0f - fx.intensity * dip;
			}
			return a_s.phase[1];
		}

		std::unordered_map<RE::NiLight*, Seen>                            gSeen;
		std::mutex                                                        gNewLock;
		struct Made
		{
			RE::NiPointer<RE::NiLight> light;
			const HandFx*              fx;
			std::size_t                option;
		};
		std::vector<Made>                                                 gNew;  // lights made since the last frame
		std::uint32_t                                                     gFrame = 0;

		[[nodiscard]] const RE::TESBoundObject* BaseOf(RE::NiLight* a_light)
		{
			const auto* ref = ReferenceOf(a_light);
			return ref ? ref->GetBaseObject() : nullptr;
		}

		// a spray's light, however RE::Light made it: it hangs on a flame or cone projectile whose light is a spray record
		[[nodiscard]] bool IsSprayProjectileLight(const RE::TESBoundObject* a_base)
		{
			const auto* proj = a_base ? a_base->As<RE::BGSProjectile>() : nullptr;
			return proj && (proj->IsFlamethrower() || proj->IsCone()) && IsSprayLight(proj->data.light);
		}

		// RE::Light names every light it makes from a config "RL" + the node it hung it on. An enchantment light hangs on
		// the actor holding the weapon, so it is found by its shader instead (Options.cpp) - his report, 2026-09-26: "the
		// sliders don't work for enchantments"
		// the option a light of ours belongs to: its object's, else its enchantment's, else its spray record's
		[[nodiscard]] std::size_t OptionOfSeen(RE::NiLight* a_light, const RE::TESBoundObject* a_base)
		{
			if (const auto o = OptionOf(a_base); o != kNone) {
				return o;
			}
			if (const auto o = EnchantOptionOf(a_light); o != kNone) {
				return o;
			}
			const auto* proj = a_base ? a_base->As<RE::BGSProjectile>() : nullptr;
			return proj ? OptionOfSprayLight(proj->data.light) : kNone;
		}

		[[nodiscard]] bool OursByObject(RE::NiLight* a_light, const RE::TESBoundObject* a_base)
		{
			if (IsSprayProjectileLight(a_base)) {
				return true;
			}
			const char* n = a_light->name.c_str();
			return n && n[0] == 'R' && n[1] == 'L' && (OptionOf(a_base) != kNone || EnchantOptionOf(a_light) != kNone);
		}

		void Apply(RE::NiLight* a_light, Seen& a_s, float a_scale, float a_reach, float a_dt)
		{
			auto& data = a_light->GetLightRuntimeData();
			if (data.fade != a_s.written) {
				a_s.base = data.fade;  // written since we last touched it: that is the new base
			}
			if (data.radius.x != a_s.wroteRadius) {
				a_s.baseRadius = data.radius.x;  // x and y are the reach, z is the size
			}
			const float fade = a_s.base * a_scale * (a_s.fx ? Oscillate(a_s, a_dt) : 1.0f);
			data.fade = fade;
			a_s.written = fade;
			const float radius = a_s.baseRadius * a_reach;
			data.radius.x = radius;
			data.radius.y = radius;
			a_s.wroteRadius = radius;
			if (a_s.tint) {
				data.diffuse = *a_s.tint;
			}
			// the cutoff follows the same rule, so an edit in RE::Light's editor sticks: a cutoff we did not write is the
			// new base, both sliders at 100% leave it alone, otherwise the reach it implies is scaled by Reach only
			if (!Isl::On(a_light)) {
				return;
			}
			const float current = Isl::Cutoff(a_light);
			if (current != a_s.wroteCutoff) {
				a_s.baseCutoff = current;
			}
			float cutoff = a_s.baseCutoff;
			if ((a_scale != 1.0f || a_reach != 1.0f) && a_s.baseCutoff > 0.0f && a_s.base > 0.0f) {
				const float size = data.radius.z;
				const float reach = std::sqrt((std::max)(kK * a_s.base / a_s.baseCutoff - size * size, 1.0f)) * a_reach;
				cutoff = CutoffFor(fade, reach, size);
			}
			if (cutoff != current) {
				Isl::SetCutoff(a_light, cutoff);
			}
			a_s.wroteCutoff = Isl::Cutoff(a_light);
		}
	}

	void RememberLight(RE::NiLight* a_light, const HandFx* a_fx, std::size_t a_option)
	{
		if (a_light) {
			std::lock_guard l{ gNewLock };
			gNew.push_back({ RE::NiPointer<RE::NiLight>(a_light), a_fx, a_option });
		}
	}

	void UpdateBrightness(float a_delta)
	{
		{
			std::lock_guard l{ gNewLock };
			for (auto& m : gNew) {
				auto& s = gSeen[m.light.get()];
				s.light = m.light;
				s.fx = m.fx;
				s.option = m.option;
			}
			gNew.clear();
		}
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		const float dt = std::clamp(a_delta, 0.0f, 0.25f);
		const float scale = Brightness();
		const float reach = Reach();
		for (const auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			auto  it = gSeen.find(niLight);
			if (it == gSeen.end()) {
				// only OUR lights are remembered: holding every light in the game would keep alive lights it let go
				const auto* base = BaseOf(niLight);
				if (!OursByObject(niLight, base)) {
					continue;
				}
				it = gSeen.emplace(niLight, Seen{ .light = RE::NiPointer<RE::NiLight>(niLight), .tint = TintOf(base),
												.option = OptionOfSeen(niLight, base) })
						 .first;
			}
			Apply(niLight, it->second, scale * OptionBrightness(it->second.option), reach, dt);
		}
		// a light only this table still holds has left the game
		if ((++gFrame & 15) == 0) {
			std::erase_if(gSeen, [](const auto& a_kv) { return !a_kv.second.light || a_kv.second.light->GetRefCount() <= 1; });
		}
	}
}
