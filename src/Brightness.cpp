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
// wrote; anything else it carries is a new base, so no slider ever scales a value we wrote. A hand light with Dynamic Lighting breathes or crackles here (Oscillate),
// and a light on an object an art pick recolours (`tint`) takes that colour. Runs last in the player update.
//
// Illuminated's light settings, ported 2026-10-08 (his "add everything from illuminated into relight ... sister mods" -
// RE::Light lights the objects; what it has no setting for is done here, on the lights this pass already looks after):
//   Dim in daylight   Brightness x a daylight factor: outdoors by the hour (full by day, none at night, a ramp at dawn and
//                     dusk), indoors by how bright the room's own light is - looked at every two seconds
//   Light colors      Automatic draws each color as RE::Light gives it; Paler turns it from linear light to the screen's
//                     (the Community Shaders look), Deeper the other way
//   Fire / Frost / Shock colors   a named color on every light of that element (a hand light by its effect, an object's
//                     light by the effect that fires it); the spell's own otherwise
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

		// linear light <-> the screen's color, per channel (Dynamic Wards' wardgen.srgb, as Illuminated's Paler colors)
		[[nodiscard]] float ToScreen(float x) noexcept
		{
			x = std::clamp(x, 0.0f, 1.0f);
			return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
		}
		[[nodiscard]] float ToLinear(float x) noexcept
		{
			x = std::clamp(x, 0.0f, 1.0f);
			return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
		}

		[[nodiscard]] RE::NiColor ColorFor(const Seen& a_s, const RE::NiColor& a_base)
		{
			if (const int pick = ElementColor(a_s.element); a_s.element > 0 && pick > 0) {
				return NamedColor(pick);
			}
			switch (LightColors()) {
			case 1:
				return { ToScreen(a_base.red), ToScreen(a_base.green), ToScreen(a_base.blue) };
			case 2:
				return { ToLinear(a_base.red), ToLinear(a_base.green), ToLinear(a_base.blue) };
			default:
				return a_base;
			}
		}

		// how much dimmer the lights are now (Dim in daylight): 1 at night and in the dark
		[[nodiscard]] float DaylightFactor()
		{
			const int pick = DimInDaylight();
			auto*     player = RE::PlayerCharacter::GetSingleton();
			auto*     cell = player ? player->GetParentCell() : nullptr;
			if (pick <= 0 || !cell) {
				return 1.0f;
			}
			const float most = pick == 1 ? 0.25f : 0.5f;
			float       bright = 0.0f;
			if (cell->IsInteriorCell()) {
				if (const auto* l = cell->GetLighting()) {
					const auto lum = [](const RE::Color& c) { return (0.2126f * c.red + 0.7152f * c.green + 0.0722f * c.blue) / 255.0f; };
					bright = std::clamp(((std::max)(lum(l->ambient), lum(l->directional)) - 0.15f) / 0.3f, 0.0f, 1.0f);
				}
			} else if (const auto* calendar = RE::Calendar::GetSingleton()) {
				const float h = calendar->GetHour();
				bright = h < 5.0f || h >= 20.0f ? 0.0f : h < 8.0f ? (h - 5.0f) / 3.0f :
				                                     h < 17.0f    ? 1.0f :
				                                                    (20.0f - h) / 3.0f;
			}
			return 1.0f - most * bright;
		}
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
			if (!SameColor(data.diffuse, a_s.wroteDiffuse)) {
				a_s.baseDiffuse = data.diffuse;  // RE::Light (or the hand light) gave it this color: the new base
			}
			const RE::NiColor color = ColorFor(a_s, a_s.tint ? *a_s.tint : a_s.baseDiffuse);
			data.diffuse = color;
			a_s.wroteDiffuse = color;
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

	void RememberLight(RE::NiLight* a_light, const HandFx* a_fx, int a_element)
	{
		if (a_light) {
			KeepLight(a_light);
			std::lock_guard l{ gNewLock };
			gNew.push_back({ a_light, a_fx, a_element });
		}
	}

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
			for (const auto* effect : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EffectSetting>()) {
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

	RE::NiColor NamedColor(int a_pick)
	{
		// Dynamic Wards' preset hues (Illuminated's kNamedColors, the same order)
		constexpr std::uint32_t kColors[kNamedColorCount] = { 0xD0102E, 0xFF6A10, 0xFFC420, 0x2ED452, 0x00D2B0, 0x40DCFF, 0x2468FF, 0x7A3CFF,
			0xFF2EC4, 0xFFFFFF };
		const auto              c = kColors[std::clamp(a_pick, 1, kNamedColorCount) - 1];
		return { ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f };
	}

	bool TouchedByENBLight(const RE::TESForm* a_form)
	{
		const auto* files = a_form ? a_form->sourceFiles.array : nullptr;
		return files && std::ranges::any_of(*files, [](const RE::TESFile* f) { return f && f->GetFilename() == "ENB Light.esp"; });
	}

	void ForgetSliderLights(const GoneLights& a_gone)
	{
		{
			std::lock_guard l{ gNewLock };
			std::erase_if(gNew, [&](const Made& a_m) { return a_gone.contains(a_m.light); });
		}
		std::lock_guard l{ gSeenLock };
		std::erase_if(gSeen, [&](const auto& a_kv) { return a_gone.contains(a_kv.first); });
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
		std::vector<Made> made;
		{
			std::lock_guard l{ gNewLock };
			made.swap(gNew);
		}
		std::lock_guard l{ gSeenLock };
		for (const auto& m : made) {
			auto& s = gSeen[m.light];
			s.fx = m.fx;
			s.element = m.element;
		}
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		const float dt = std::clamp(a_delta, 0.0f, 0.25f);
		if ((gDaylightClock += dt) >= 2.0f) {
			gDaylightClock = 0.0f;
			gDaylight = DaylightFactor();
		}
		const float scale = Brightness() * gDaylight;
		const float reach = Reach();
		for (const auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			auto  it = gSeen.find(niLight);
			if (it == gSeen.end()) {
				// only OUR lights are remembered
				const auto* base = BaseOf(niLight);
				if (!OursByObject(niLight, base)) {
					continue;
				}
				KeepLight(niLight);
				it = gSeen.emplace(niLight, Seen{ .tint = TintOf(base), .element = ElementOfForm(base) }).first;
			}
			Apply(niLight, it->second, scale, reach, dt);
		}
	}
}
