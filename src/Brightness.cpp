// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Our own Brightness and Reach sliders. RE::Light's multiplier scales every RE::Light light in the game; these touch
// only this mod's lights (RE::Light's lights on objects our data names, our hand lights, spray lights and the lights of
// the beam and breath projectiles our `stream` lines name). Under inverse square lighting reach = sqrt(K * fade / cutoff - size²), so:
//     Brightness  scales fade and cutoff by one ratio  -> the peak moves, the reach is held
//     Reach       scales the radius, cutoff re-derived -> the reach moves, the peak is held
// RE::Light rewrites fade every frame on a light that flickers or pulses, so each light remembers the fade we last
// wrote; anything else it carries is a new base, so no slider ever scales a value we wrote. A hand light with Dynamic
// Lighting breathes or crackles here (Oscillate), and a light on an object an art pick recolors (`tint`) takes that
// color. Runs last in the player update.
//
// Illuminated's light settings, ported 2026-10-08 (his "add everything from illuminated into relight ... sister mods" -
// RE::Light lights the objects; what it has no setting for is done here, on the lights this pass already looks after):
//   Dim in daylight   Brightness x a daylight factor: outdoors by the hour (full by day, none at night, a ramp at dawn and
//                     dusk), indoors by how bright the room's own light is - looked at every two seconds
//   (Light colors and the Fire / Frost / Shock color picks were here until his order of 2026-10-09 took the color
//   variations out of the menu - ColorFor keeps each light's own color; an art pick's tint still recolors.)
// The color is remembered like the fade: what RE::Light writes is the new base, what we wrote is never taken for one.
// The fading module (Fade*.cpp, Illuminated's) scales some of the same lights by charge and magicka right after this pass
// and after each enchantment effect's update; it reports each fade it writes (NoteFadeWrite), so its write is never taken
// for a new base here and the two never compound. Lock order: the fading module's lock, then gSeenLock.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// keyed by the light, which Keep.cpp holds; ForgetSliderLights drops the row before the light is freed
		struct Seen
		{
			const HandFx*        fx{ nullptr };
			const RE::NiColor*   tint{ nullptr };
			float                base{ 0.0f };        // the fade RE::Light last gave it
			float                written{ -1.0f };    // the fade we last wrote
			float                baseRadius{ 0.0f };  // the reach RE::Light last gave it
			float                wroteRadius{ -1.0f };
			float                baseCutoff{ -1.0f };  // the cutoff RE::Light (or its editor) last gave it
			float                wroteCutoff{ -2.0f };
			std::array<float, 3> phase{};
			int                  element{ 0 };   // fire, frost or shock: its color setting
			RE::NiColor          baseDiffuse{};  // the color RE::Light (or the hand light) last gave it
			RE::NiColor          wroteDiffuse{ -1.0f, -1.0f, -1.0f };
		};

		[[nodiscard]] bool SameColor(const RE::NiColor& a, const RE::NiColor& b) noexcept
		{
			return a.red == b.red && a.green == b.green && a.blue == b.blue;
		}

		// ✂ HIS ORDER 2026-10-09: *"remove the color variations from illuminated and relight in the lights menu"* - Light colors
		// (Paler / Deeper) and the Fire / Frost / Shock color picks are gone: every light keeps the color RE::Light (or an art
		// pick's tint) gives it. One place, so the lights that are scaled elsewhere (Held.cpp) draw the same.
		[[nodiscard]] RE::NiColor ColorFor(const Seen&, const RE::NiColor& a_base) { return a_base; }

		float gDaylight = 1.0f, gDaylightClock = 2.0f;  // main thread only (UpdateBrightness)

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

		std::unordered_map<const RE::NiLight*, Seen> gSeen;
		std::mutex                                   gSeenLock;  // the fading module reports from the effect updates too
		std::mutex                                   gNewLock;
		struct Made
		{
			RE::NiLight*  light;
			const HandFx* fx;
			int           element;
		};
		std::vector<Made> gNew;  // lights made since the last frame

		// The lights that are NOT ours, so the walk up to the reference and the data lookup are not repeated for each of them
		// every frame (main thread). A freed light's address can come back as another light, so each is remembered with its
		// name text and its parent, and asked again when either differs; RE::Light's enchantment lights are never cached here
		// (one becomes ours when its effect starts - Options.cpp finds those each frame).
		struct NotOurs
		{
			const void*   name{ nullptr };
			const void*   parent{ nullptr };
			std::uint32_t frame{ 0 };
		};
		std::unordered_map<const RE::NiLight*, NotOurs> gNotOurs;
		std::uint32_t                                   gFrame = 0;

		[[nodiscard]] bool KnownNotOurs(const RE::NiLight* a_light)
		{
			const auto it = gNotOurs.find(a_light);
			if (it == gNotOurs.end() || it->second.name != a_light->name.c_str() || it->second.parent != a_light->parent) {
				return false;
			}
			it->second.frame = gFrame;
			return true;
		}

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
		[[nodiscard]] bool OursByObject(RE::NiLight* a_light, const RE::TESBoundObject* a_base)
		{
			if (IsSprayProjectileLight(a_base)) {
				return true;
			}
			return IsReLightLight(a_light) && (OptionOf(a_base) != kNone || EnchantOptionOf(a_light) != kNone);
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
			if (!SameColor(data.diffuse, a_s.wroteDiffuse)) {
				a_s.baseDiffuse = data.diffuse;  // RE::Light (or the hand light) gave it this color: the new base
			}
			const RE::NiColor color = ColorFor(a_s, a_s.tint ? *a_s.tint : a_s.baseDiffuse);
			data.diffuse = color;
			a_s.wroteDiffuse = color;
			// the cutoff follows the same rule, so an edit in RE::Light's editor sticks: a cutoff we did not write is the
			// new base, both sliders at 100% leave it alone, otherwise the reach it implies is scaled by Reach only.
			// Without Community Shaders' inverse square lighting these words are the ambient color (Plugin.h), so they are
			// neither read nor written - as Held.cpp and Streams.cpp
			if (!IslShader() || !Isl::On(a_light)) {
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

	void RememberLight(RE::NiLight* a_light, const HandFx* a_fx, int a_element)
	{
		if (a_light) {
			KeepLight(a_light);
			std::lock_guard l{ gNewLock };
			gNew.push_back({ a_light, a_fx, a_element });
		}
	}

	float DaylightFactor() { return gDaylight; }

	RE::NiColor DrawnColor(int, const RE::NiColor& a_base) { return ColorFor(Seen{}, a_base); }

	// ------------------------------------------------------------------ elements and their colors
	int ElementOf(const RE::EffectSetting* a_effect)
	{
		switch (a_effect ? a_effect->data.resistVariable : RE::ActorValue::kNone) {
		case RE::ActorValue::kResistFire:
			return 1;
		case RE::ActorValue::kResistFrost:
			return 2;
		case RE::ActorValue::kResistShock:
			return 3;
		default:
			return 0;
		}
	}

	int ElementOfForm(const RE::TESForm* a_form)
	{
		static const auto map = [] {
			std::unordered_map<const RE::TESForm*, int> m;
			for (const auto* effect : LightKit::FormsOf<RE::EffectSetting>()) {
				const int e = ElementOf(effect);
				if (!e) {
					continue;
				}
				for (const RE::TESForm* f : { static_cast<const RE::TESForm*>(effect->data.projectileBase),
						 static_cast<const RE::TESForm*>(effect->data.explosion) }) {
					if (f) {
						auto [it, added] = m.try_emplace(f, e);
						if (!added && it->second != e) {
							it->second = 0;  // fired by effects of two elements: none
						}
					}
				}
			}
			return m;
		}();
		const auto it = a_form ? map.find(a_form) : map.end();
		return it == map.end() ? 0 : it->second;
	}

	void ForgetSliderLights(const GoneLights& a_gone)
	{
		{
			std::lock_guard l{ gNewLock };
			std::erase_if(gNew, [&](const Made& a_m) { return a_gone.contains(a_m.light); });
		}
		std::lock_guard l{ gSeenLock };
		std::erase_if(gSeen, [&](const auto& a_kv) { return a_gone.contains(a_kv.first); });
		std::erase_if(gNotOurs, [&](const auto& a_kv) { return a_gone.contains(a_kv.first); });
	}

	void NoteFadeWrite(const RE::NiPointLight* a_light, float a_before, float a_after)
	{
		std::lock_guard l{ gSeenLock };
		// only when the fading module started from the fade this pass wrote (Illuminated measured why, 2026-10-08: noting
		// every write froze steady lights this pass had never scaled at full strength)
		if (const auto it = gSeen.find(a_light); it != gSeen.end() && it->second.written == a_before) {
			it->second.written = a_after;
		}
	}

	void UpdateBrightness(float a_delta)
	{
		static std::vector<Made> made;  // main thread; its storage and gNew's are swapped back and forth, never reallocated
		made.clear();
		{
			std::lock_guard l{ gNewLock };
			made.swap(gNew);
		}
		++gFrame;
		{
			std::lock_guard l{ gSeenLock };
			for (const auto& m : made) {
				auto& s = gSeen[m.light];
				s.fx = m.fx;
				s.element = m.element;
			}
		}
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		const float dt = std::clamp(a_delta, 0.0f, 0.25f);
		gDaylightClock += dt;
		if (gDaylightClock >= 2.0f) {
			gDaylightClock = 0.0f;
			gDaylight = LightKit::Daylight(DimInDaylight());
		}
		const float scale = Brightness() * gDaylight;
		const float reach = Reach();
		for (const auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			// gNotOurs is the main thread's alone (this pass and the Keep sweep), so it is read with no lock
			if (KnownNotOurs(niLight)) {
				continue;
			}
			// gSeenLock is taken per light, not across the walk: the fading module's NoteFadeWrite (the effect updates)
			// never waits for the whole pass (the re-score's RELight issue 2)
			std::lock_guard l{ gSeenLock };
			auto            it = gSeen.find(niLight);
			if (it == gSeen.end()) {
				// only OUR lights are remembered; the others are remembered as not ours
				const auto* base = BaseOf(niLight);
				if (!OursByObject(niLight, base)) {
					// a light whose reference has no base yet (its 3D still loading) is not cached: it may turn out ours
					if (base && !IsEnchantLight(niLight)) {
						gNotOurs.insert_or_assign(niLight, NotOurs{ niLight->name.c_str(), niLight->parent, gFrame });
					}
					continue;
				}
				gNotOurs.erase(niLight);
				KeepLight(niLight);
				it = gSeen.emplace(niLight, Seen{ .tint = TintOf(base), .element = ElementOfForm(base) }).first;
			}
			Apply(niLight, it->second, scale, reach, dt);
		}
		// a light not seen for a few seconds has left the scene: let it go from the cache
		constexpr std::uint32_t kForget = 300;
		if (gFrame % kForget == 0) {
			std::erase_if(gNotOurs, [](const auto& a_kv) { return gFrame - a_kv.second.frame > kForget; });
		}
	}
}
