// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The light on the caster's hands - no plugin, no script.
//
// Casting art hangs off the actor, so no RE::Light mesh config reaches it. Instead each magic effect whose casting
// art is a mesh this mod lights is pointed, in memory, at its casting light: a light record made here in the colour,
// strength and reach the installed layers give that mesh. RE::Light switches off casting lights it has no config for,
// and an in-memory record can have none, so main.cpp's hook makes these lights itself and never hands them on.
// Nothing is saved; take the mod out and every effect keeps its own light.
//
// AUTOMATIC HAND LIGHTS (Illuminated's, ported 2026-10-08 - his "add everything from illuminated into relight"): a spell
// whose casting art no layer lights (another mod's spell this mod has no patch for) would be dark - RE::Light switches its
// casting light off, having no config for it. Behind the setting "Light spells this mod has no patch for" it gets a hand
// light of ours instead: in the color of its own casting light when it has one, else its element's (the color most of the
// tuned spells of that element wear: fire, frost, shock), at the middle strength and reach of the tuned hand lights. A spell
// with neither keeps what it has: no color is guessed. On ENB, a spell ENB Light changed is left to ENB Light (its switch).

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct Target
		{
			RE::EffectSetting* effect;
			RE::TESObjectLIGH* own;  // the light its plugin gave it
			std::string        key;
			bool               automatic{ false };  // no layer lights its art: key names an automatic hand (gAutoHands)
			bool               enbLight{ false };   // ENB Light changed the effect or its art: left to it on ENB
		};

		constexpr std::string_view          kAutoPrefix = "auto ";
		const std::vector<std::string_view> kSkipPrefixes{ "trap", "hazard", "voice", "ench", "test" };  // Illuminated's pass 1 list

		constexpr std::uint32_t kLighInverseSquare = 1u << 14;  // Community Shaders' inverse square flag on a LIGH record

		// a hand light already lit: a menu change reaches it at once instead of on the next cast (his report, 2026-09-23:
		// "i have to dispel the spell then re-cast")
		struct Live
		{
			RE::NiLight* light;  // held in Keep.cpp; ForgetHandLights drops it here before it is freed
			std::string  key;
			bool         heldOut{ false };  // put out because nothing lights its key any more
		};

		std::mutex                                                gLock;
		std::vector<Target>                                       gTargets;
		std::vector<Live>                                         gLive;
		std::unordered_map<std::string, RE::TESObjectLIGH*>       gCopies;
		std::unordered_map<const RE::TESObjectLIGH*, const Hand*> gInUse;
		std::size_t                                               gLit = 0, gAutoLit = 0;
		bool                                                      gIsl = false;
		std::map<std::string, Hand, std::less<>>                  gAutoHands;     // "auto r,g,b" -> its hand (stable: a map's nodes never move)
		std::unordered_map<std::string, RE::TESObjectLIGH*>       gAutoCopies;    // the same key -> its light record
		StringMap<int>                                            gElementOfKey;  // hand key -> element, most of its effects' (0: none, or a tie)

		[[nodiscard]] bool AutoKey(std::string_view a_key) { return a_key.starts_with(kAutoPrefix); }

		[[nodiscard]] bool SkippedPrefix(const RE::EffectSetting* a_effect)
		{
			std::string low = a_effect ? a_effect->GetFormEditorID() : "";
			std::ranges::transform(low, low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (low.size() > 4 && low.starts_with("dlc")) {
				low = low.substr(4);
			}
			return std::ranges::any_of(kSkipPrefixes, [&](std::string_view p) { return low.starts_with(p); });
		}

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
			// ENB and Vanilla (Lighting.cpp): an inverse-square hand light is drawn plain - its reach as a radius, no flag
			const bool plain = a_hand.inverseSquare && !IslLighting();
			d.radius = static_cast<std::uint32_t>(std::lround((std::max)(plain ? PlainRadius(a_hand.radius) : a_hand.radius, 0.0f)));
			d.color.red = a_hand.rgb[0];
			d.color.green = a_hand.rgb[1];
			d.color.blue = a_hand.rgb[2];
			d.color.alpha = 0;
			std::uint32_t flags = a_hand.portalStrict ? static_cast<std::uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict) : 0u;
			if (a_hand.inverseSquare && !plain) {
				flags |= kLighInverseSquare;
			}
			d.flags = static_cast<RE::TES_LIGHT_FLAGS>(flags);
			d.fallofExponent = 1.0f;
			d.fov = 90.0f;
			d.nearDistance = 1.0f;
			d.flickerPeriodRecip = 1.0f;
			d.flickerIntensityAmplitude = 0.0f;
			d.flickerMovementAmplitude = 0.0f;
			a_light->fade = plain ? PlainFade(a_hand.fade) : a_hand.fade;
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
			for (auto& [_k, c] : gAutoCopies) {
				ours.insert(c);
			}
			gTargets.clear();
			gElementOfKey.clear();
			std::size_t                     byPath = 0;
			std::vector<RE::EffectSetting*> uncovered;
			const auto                      ownOf = [&](RE::EffectSetting* a_effect) {
				RE::TESObjectLIGH* mine = a_effect->data.light;
				if (auto it = own.find(a_effect); it != own.end()) {
					mine = it->second;
				} else if (ours.contains(mine)) {
					mine = nullptr;
				}
				return mine;
			};
			// a key's element: the one most of the effects wearing it carry (fire, frost or shock); an effect of no element
			// does not count - measured 2026-10-08: Flames' hand art is shared with effects of none, and "all must agree" left
			// the Fire color off Flames' own hand. A tie gives none.
			StringMap<std::array<std::size_t, 4>> votes;
			const auto                            noteElement = [&](const std::string& a_key, int a_element) {
				if (a_element > 0 && a_element < 4) {
					++votes[a_key][static_cast<std::size_t>(a_element)];
				}
			};
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
					uncovered.push_back(effect);
					continue;
				}
				noteElement(key, ElementOf(effect));
				gTargets.push_back({ effect, ownOf(effect), std::move(key) });
			}
			// the automatic hand lights: the middle of the tuned hand lights (by fade x radius²), and each element's color
			// as most of the tuned spells of that element wear it
			std::vector<const Hand*> tuned;
			for (const auto& [key, list] : Hands()) {
				if (!list.empty()) {
					tuned.push_back(&list.front());
				}
			}
			std::ranges::sort(tuned, {}, [](const Hand* h) { return h->fade * h->radius * h->radius; });
			std::map<int, std::map<std::uint32_t, std::size_t>> byElement;
			const auto                                          pack = [](const std::uint8_t a_rgb[3]) { return (std::uint32_t{ a_rgb[0] } << 16) | (std::uint32_t{ a_rgb[1] } << 8) | a_rgb[2]; };
			for (const auto& t : gTargets) {
				if (const int e = ElementOf(t.effect); e > 0) {
					if (const Hand* h = Winner(t.key)) {
						++byElement[e][pack(h->rgb)];
					}
				}
			}
			for (auto* effect : uncovered) {
				if (tuned.empty() || effect->data.castingType == RE::MagicSystem::CastingType::kConstantEffect ||
					effect->data.archetype == RE::EffectArchetypes::ArchetypeID::kLight || SkippedPrefix(effect)) {
					continue;
				}
				std::uint32_t rgb = 0;
				bool          found = false;
				if (const auto* l = ownOf(effect)) {
					rgb = (std::uint32_t{ l->data.color.red } << 16) | (std::uint32_t{ l->data.color.green } << 8) | l->data.color.blue;
					found = true;
				} else if (const auto it = byElement.find(ElementOf(effect)); it != byElement.end() && !it->second.empty()) {
					rgb = std::ranges::max_element(it->second, {}, [](const auto& kv) { return kv.second; })->first;
					found = true;
				}
				if (!found) {
					continue;  // no color to read: it keeps what it has
				}
				auto key = std::format("{}{},{},{}", kAutoPrefix, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
				if (!gAutoHands.contains(key)) {
					Hand h = *tuned[tuned.size() / 2];
					h.key = key;
					h.rgb[0] = static_cast<std::uint8_t>(rgb >> 16);
					h.rgb[1] = static_cast<std::uint8_t>(rgb >> 8);
					h.rgb[2] = static_cast<std::uint8_t>(rgb);
					h.color = { h.rgb[0] / 255.0f, h.rgb[1] / 255.0f, h.rgb[2] / 255.0f };
					h.option = kNone;
					auto* copy = NewLight();
					if (!copy) {
						continue;
					}
					Fill(copy, h);
					gAutoCopies.emplace(key, copy);
					gAutoHands.emplace(key, std::move(h));
				}
				noteElement(key, ElementOf(effect));
				const bool enb = TouchedByENBLight(effect) || TouchedByENBLight(effect->data.castingArt);
				gTargets.push_back({ effect, ownOf(effect), std::move(key), true, enb });
			}
			for (const auto& [key, v] : votes) {
				const auto best = std::ranges::max_element(v.begin() + 1, v.end());
				const auto ties = std::ranges::count(v.begin() + 1, v.end(), *best);
				gElementOfKey.emplace(key, ties == 1 ? static_cast<int>(best - v.begin()) : 0);
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
		// inverse square lighting only on the Community Shaders pick with its shader there (Lighting.cpp); without it the two
		// words DressHandLight writes are ambient colour
		ReadLighting();
		gIsl = IslLighting();
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
		SKSE::log::info(
			"hand lights: {} light(s) made in memory, {} failed; {} magic effect(s) wear one of their meshes ({} by "
			"their full art path); inverse square lighting {}",
			made, failed, gTargets.size(), byPath, gIsl ? "found" : "not found");
		{
			std::lock_guard l{ gLock };
			const auto      autos = std::ranges::count_if(gTargets, [](const Target& t) { return t.automatic; });
			SKSE::log::info("automatic hand lights: {} spell(s) no layer lights get one, in {} color(s); setting {}", autos, gAutoCopies.size(),
				AutoLightsOn() ? "on" : "off");
			for (const auto& t : gTargets) {
				if (t.automatic) {
					SKSE::log::info("[HAND-AUTO] {:08X} {} | {}{}", t.effect->GetFormID(), t.effect->GetFullName(), t.key, t.enbLight ? " | ENB Light's" : "");
				}
			}
		}
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
			const bool autoOn = on && AutoLightsOn();
			for (auto& [key, copy] : gAutoCopies) {
				if (autoOn) {
					gInUse[copy] = &gAutoHands.find(key)->second;
				}
			}
			std::size_t autoLit = 0;
			for (auto& t : gTargets) {
				auto*      copy = t.automatic ? gAutoCopies[t.key] : gCopies[t.key];
				const bool left = t.automatic && t.enbLight && EnbLighting() && LeaveToENBLight();  // ENB Light lights it
				auto*      want = gInUse.contains(copy) && !left ? copy : t.own;
				if (t.effect->data.light != want) {
					t.effect->data.light = want;
				}
				if (want == copy) {
					++lit;
					autoLit += t.automatic ? 1 : 0;
				} else {
					++given;
				}
			}
			gLit = lit;
			gAutoLit = autoLit;
			// the lights already in a caster's hands take the change now; one whose key nothing lights any more goes out
			// until the next cast gives the effect its own light back
			for (auto& live : gLive) {
				const auto  autoIt = AutoKey(live.key) ? gAutoHands.find(live.key) : gAutoHands.end();
				const Hand* h = autoIt != gAutoHands.end() ? (autoOn ? &autoIt->second : nullptr) : on ? Picked(Winner(live.key)) :
				                                                                                         nullptr;
				if (h) {
					if (live.heldOut) {
						live.light->SetAppCulled(false);
						live.heldOut = false;
					}
					DressHandLight(live.light, *h);
					const auto e = gElementOfKey.find(live.key);  // gLock is held here: not ElementOfHandKey, which takes it
					RememberLight(live.light, HandFxOf(h->key), e == gElementOfKey.end() || e->second < 0 ? 0 : e->second);
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
		a_light->fadeAmount = kMovingLightMark;
		auto&       data = a_light->GetLightRuntimeData();
		const bool  plain = !gIsl && a_hand.inverseSquare;  // ENB and Vanilla: drawn plain (Lighting.cpp)
		const float radius = plain ? PlainRadius(a_hand.radius) : a_hand.radius;
		data.fade = plain ? PlainFade(a_hand.fade) : a_hand.fade;
		data.radius = { radius, radius, a_hand.size };
		data.diffuse = a_hand.color;
		if (!gIsl) {
			data.ambient = PlainAmbient(a_hand.color);
		}
		if (gIsl) {
			if (a_hand.inverseSquare) {
				Isl::SetOn(a_light);
			}
			Isl::SetCutoff(a_light, a_hand.cutoff);
		}
	}

	// main thread (main.cpp NoteMagicLight)
	void NoteHandLight(RE::NiLight* a_light, const std::string& a_key)
	{
		if (!a_light) {
			return;
		}
		KeepLight(a_light);
		std::lock_guard l{ gLock };
		gLive.push_back({ a_light, a_key });
	}

	void ForgetHandLights(const GoneLights& a_gone)
	{
		std::lock_guard l{ gLock };
		std::erase_if(gLive, [&](const Live& a_l) { return a_gone.contains(a_l.light); });
	}

	bool HandLightHeldOut(const RE::NiLight* a_light)
	{
		std::lock_guard l{ gLock };
		return std::ranges::any_of(gLive, [a_light](const Live& a_l) { return a_l.heldOut && a_l.light == a_light; });
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

	std::size_t AutoHandEffects()
	{
		std::lock_guard l{ gLock };
		return gAutoLit;
	}

	int ElementOfHandKey(std::string_view a_key)
	{
		std::lock_guard l{ gLock };
		const auto      it = gElementOfKey.find(a_key);
		return it == gElementOfKey.end() || it->second < 0 ? 0 : it->second;
	}
}
