// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Sprays, breath shouts and beams. RE::Light lights them - the projectile's own light record, and a spray's or (since 2.2) a
// beam's record through our `isPluginLight` config; this mod only takes that light into its menu (Brightness, Reach, the switch).
// ⚠ Truman's call after the Lightning Bolt freeze: this file used to hang travelling lights of its own
// on the projectile's 3D (from the load hook, which also runs on the game's loader threads, under a lock). All of that
// is gone; a `stream` line in the data now only says "this projectile is ours for the menu".
//
// CREDIT: how a light is made and registered here (for Held.cpp) follows ReLight by Truman (github.com/TrumanGIT/ReLight),
// GPL-3.0-or-later, with his permission and kept under the same license. From ReLight: one master NiPointLight made once
// and cloned for every use (a freshly made light attached straight away crashes), the light's size carried in the
// radius' z, the create parameters a non-shadow light needs (field of view 90, portal-strict, never fades), and handing
// the light to the shadow scene node, which is what renders it.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kLightName = "RSAStream";

		std::unordered_set<const RE::TESObjectLIGH*> gSpraySet;

		// how many of each kind of user a claim changed
		struct LightUsers
		{
			std::size_t projectiles{ 0 }, muzzles{ 0 }, effects{ 0 }, explosions{ 0 }, hazards{ 0 };
		};

		// every record field that names a light record - a projectile's own light and its muzzle flash, a magic effect's, an
		// explosion's, a hazard's - handed to a_change(the projectile whose own light it is, or nullptr; the field), which returns
		// true when it changed that field. One walk for both claims, so a user kind is never in one and missing from the other
		// (2.2's code review)
		template <class F>
		LightUsers ForEachLightField(RE::TESDataHandler& a_dh, F&& a_change)
		{
			LightUsers n;
			for (auto* p : a_dh.GetFormArray<RE::BGSProjectile>()) {
				if (p) {
					n.projectiles += a_change(p, p->data.light) ? 1 : 0;
					n.muzzles += a_change(nullptr, p->data.muzzleFlashLight) ? 1 : 0;
				}
			}
			for (auto* e : a_dh.GetFormArray<RE::EffectSetting>()) {
				n.effects += e && a_change(nullptr, e->data.light) ? 1 : 0;
			}
			for (auto* x : a_dh.GetFormArray<RE::BGSExplosion>()) {
				n.explosions += x && a_change(nullptr, x->data.light) ? 1 : 0;
			}
			for (auto* h : a_dh.GetFormArray<RE::BGSHazard>()) {
				n.hazards += h && a_change(nullptr, h->data.light) ? 1 : 0;
			}
			return n;
		}
	}

	// A spray is lit by its own light record, which RE::Light tunes through our `isPluginLight` config. Many of those
	// records are shared (fireballs, casting lights, explosions, hazards, muzzle flashes), so the record is taken off
	// every user that is not a spray, in memory - RE::Light would have switched those unconfigured lights off anyway. Since 2.2
	// a beam our data lights keeps it too (the bolts).
	void ClaimSprayLights()
	{
		gSpraySet.clear();
		for (const auto id : SprayLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id)) {
				gSpraySet.insert(l);
			}
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (gSpraySet.empty() || !dh) {
			return;
		}
		const auto n = ForEachLightField(*dh, [](const RE::BGSProjectile* a_proj, RE::TESObjectLIGH*& a_light) {
			if (!a_light || !gSpraySet.contains(a_light)) {
				return false;
			}
			// a spray (flamethrower or cone) keeps its own record - and since 2.2 (the bolts) a BEAM our data lights keeps it too: a
			// record of both is lit by the spray's config, never taken off the bolt (his rule: the projectile's own light, made
			// bigger and brighter)
			if (a_proj && (a_proj->IsFlamethrower() || a_proj->IsCone() || (a_proj->IsBeam() && StreamOf(a_proj)))) {
				return false;
			}
			a_light = nullptr;
			return true;
		});
		SKSE::log::info(
			"spray lights: {} record(s) are the sprays' alone now - taken off {} projectile(s), {} muzzle flash(es), "
			"{} magic effect(s), {} explosion(s), {} hazard(s)",
			gSpraySet.size(), n.projectiles, n.muzzles, n.effects, n.explosions, n.hazards);
	}

	// A beam is lit by its own light record too, made bigger and brighter by our `isPluginLight` config (his rule 2026-10-13,
	// RE::Light's way - one light per projectile). Beam records are shared far more widely than the sprays' -
	// MagicLightShockProjectile01 is worn by ~30 beams and by missiles, cones, explosions, hazards, arrows and muzzle flashes - so
	// the record stays on OUR beams only (a beam projectile whose model one of our `stream` lines names) and every other user is
	// pointed at an in-memory copy of it: the same light it had before, which RE::Light has no config for. A record that is a
	// spray record too stays the spray's (ClaimSprayLights keeps it on our beams, lit by the spray's config).
	void ClaimBeamLights()
	{
		std::unordered_set<const RE::TESObjectLIGH*> beams;
		for (const auto id : BeamLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id); l && !gSpraySet.contains(l)) {
				beams.insert(l);
			}
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (beams.empty() || !dh) {
			return;
		}
		std::unordered_map<const RE::TESObjectLIGH*, RE::TESObjectLIGH*> copies;
		std::size_t                                                      failed = 0, ours = 0;
		const auto                                                       copyOf = [&](RE::TESObjectLIGH* a_light) {
			auto [it, fresh] = copies.try_emplace(a_light, nullptr);
			if (fresh) {
				if (auto* copy = LightKit::NewForm<RE::TESObjectLIGH>()) {
					// the whole light as the user had it: its look, sound, emittance, lens flare and model (2.2's code review;
					// Illuminated's FormCopies.cpp CopyLight copies the same)
					copy->data = a_light->data;
					copy->fade = a_light->fade;
					copy->sound = a_light->sound;
					copy->emittanceColor = a_light->emittanceColor;
					copy->lensFlare = a_light->lensFlare;
					copy->SetModel(a_light->GetModel());
					it->second = copy;
				} else {
					++failed;  // no copy: this user keeps the record (and our config's light) - said in the log
				}
			}
			return it->second ? it->second : a_light;
		};
		const auto n = ForEachLightField(*dh, [&](const RE::BGSProjectile* a_proj, RE::TESObjectLIGH*& a_light) {
			if (!a_light || !beams.contains(a_light)) {
				return false;
			}
			if (a_proj && a_proj->IsBeam() && StreamOf(a_proj)) {
				++ours;  // one of our beams: it keeps the record our config raises
				return false;
			}
			a_light = copyOf(a_light);
			return true;
		});
		SKSE::log::info(
			"beam lights: {} record(s) stay on our {} beam(s); {} copy(ies) made for the other users - {} projectile(s), {} muzzle "
			"flash(es), {} magic effect(s), {} explosion(s), {} hazard(s){}",
			beams.size(), ours, copies.size() - failed, n.projectiles, n.muzzles, n.effects, n.explosions, n.hazards,
			failed ? std::format("; {} copy(ies) could not be made - those users keep the record", failed) : std::string{});
	}

	bool IsSprayLight(const RE::TESObjectLIGH* a_light)
	{
		return a_light && gSpraySet.contains(a_light);
	}

	bool IsStreamObject(const RE::TESObjectREFR* a_ref)
	{
		return a_ref && StreamOf(a_ref->GetBaseObject()) != nullptr;
	}

	RE::BSLight* MakeOurLight(const Stream& a_s, const RE::NiColor& a_colour, const RE::NiPoint3& a_at, float a_fade, float a_reach,
		RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene, RE::NiPointLight*& a_made)
	{
		auto* light = LightKit::CloneLight();
		if (!light) {
			return nullptr;
		}
		light->name = kLightName;
		light->fadeAmount = kMovingLightMark;
		auto& data = light->GetLightRuntimeData();
		data.diffuse = a_colour;
		data.fade = a_fade;
		data.radius = { a_reach, a_reach, a_s.size };  // x and y are the reach; z carries the light's size
		light->SetLightAttenuation(a_reach);           // without it the light has no attenuation and lights nothing
		if (!IslShader()) {
			// off Community Shaders: an ambient of a tenth of its color, RE::Light's rule (Truman); the caller
			// hands a plain fade and reach (Held.cpp: fade x 1.14, reach x 178 / 133)
			data.ambient = { a_colour.red * 0.1f, a_colour.green * 0.1f, a_colour.blue * 0.1f };
		}
		// after SetLightAttenuation, which writes the same two words; the cutoff is re-derived so Brightness moves
		// the peak with the reach held (at 100%/100% it equals the file's). Only when RE::Light's own test finds inverse
		// square lighting (IslShader), as the hand lights - no lighting pick of our own (his "relight is relight is relight")
		if (IslShader()) {
			Isl::SetOn(light);
			Isl::SetCutoff(light, CutoffFor(a_fade, a_reach, a_s.size));
		}
		light->local.translate = a_at;
		light->local.scale = 1.0f;
		const RE::NiPointer<RE::NiPointLight> hold(light);  // freed here if it is never registered
		a_parent->AttachChild(light, true);
		RE::NiUpdateData update{};
		light->Update(update);
		auto* bs = LightKit::AddToScene(a_scene, light);
		if (!bs) {
			a_parent->DetachChild(light);
			return nullptr;
		}
		a_made = light;
		return bs;
	}
}
