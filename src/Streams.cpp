// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Sprays, breath shouts and beams. RE::Light lights them - the projectile's own light record, and a spray's record
// through our `isPluginLight` config; this mod only takes that light into its menu (Brightness, Reach, the switch).
// ⚠ 2026-09-29, Truman's call after the Lightning Bolt freeze: this file used to hang travelling lights of its own
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
		constexpr float       kFieldOfView = 90.0f;   // what a light that casts no shadow is given (from ReLight)
		constexpr const char* kLightName = "RSAStream";

		RE::NiPointer<RE::NiPointLight> gMaster;

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());  // let go once cloned
				if (!fresh) {
					return nullptr;
				}
				auto* clone = netimmerse_cast<RE::NiPointLight*>(fresh->Clone());
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		std::unordered_set<const RE::TESObjectLIGH*> gSpraySet;
	}

	// A spray is lit by its own light record, which RE::Light tunes through our `isPluginLight` config. Many of those
	// records are shared (fireballs, casting lights, explosions, hazards, muzzle flashes), so the record is taken off
	// every user that is not a spray, in memory - RE::Light would have switched those unconfigured lights off anyway.
	void ClaimSprayLights()
	{
		gSpraySet.clear();
		for (const auto id : SprayLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id)) {
				gSpraySet.insert(l);
			}
		}
		if (gSpraySet.empty()) {
			return;
		}
		auto*       dh = RE::TESDataHandler::GetSingleton();
		std::size_t proj = 0, muzzle = 0, effects = 0, expl = 0, hazards = 0;
		for (auto* p : dh->GetFormArray<RE::BGSProjectile>()) {
			if (!p) {
				continue;
			}
			if (p->data.light && gSpraySet.contains(p->data.light) && !p->IsFlamethrower() && !p->IsCone()) {
				p->data.light = nullptr;
				++proj;
			}
			if (p->data.muzzleFlashLight && gSpraySet.contains(p->data.muzzleFlashLight)) {
				p->data.muzzleFlashLight = nullptr;
				++muzzle;
			}
		}
		for (auto* e : dh->GetFormArray<RE::EffectSetting>()) {
			if (e && e->data.light && gSpraySet.contains(e->data.light)) {
				e->data.light = nullptr;
				++effects;
			}
		}
		for (auto* x : dh->GetFormArray<RE::BGSExplosion>()) {
			if (x && x->data.light && gSpraySet.contains(x->data.light)) {
				x->data.light = nullptr;
				++expl;
			}
		}
		for (auto* h : dh->GetFormArray<RE::BGSHazard>()) {
			if (h && h->data.light && gSpraySet.contains(h->data.light)) {
				h->data.light = nullptr;
				++hazards;
			}
		}
		SKSE::log::info("spray lights: {} record(s) are the sprays' alone now - taken off {} projectile(s), {} muzzle flash(es), "
						"{} magic effect(s), {} explosion(s), {} hazard(s)",
			gSpraySet.size(), proj, muzzle, effects, expl, hazards);
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
		auto* light = CloneMaster();
		if (!light) {
			return nullptr;
		}
		light->name = kLightName;
		auto& data = light->GetLightRuntimeData();
		data.diffuse = a_colour;
		data.fade = a_fade;
		data.radius = { a_reach, a_reach, a_s.size };  // x and y are the reach; z carries the light's size
		light->SetLightAttenuation(a_reach);            // without it the light has no attenuation and lights nothing
		// after SetLightAttenuation, which writes the same two words; the cutoff is re-derived so Brightness moves
		// the peak with the reach held (at 100%/100% it equals the file's)
		Isl::SetOn(light);
		Isl::SetCutoff(light, CutoffFor(a_fade, a_reach, a_s.size));
		light->local.translate = a_at;
		light->local.scale = 1.0f;
		const RE::NiPointer<RE::NiPointLight> hold(light);   // freed here if it is never registered
		a_parent->AttachChild(light, true);
		RE::NiUpdateData update{};
		light->Update(update);
		RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
		params.dynamic = true;
		params.shadowLight = false;
		params.portalStrict = true;
		params.affectLand = true;
		params.affectWater = true;
		params.neverFades = true;
		params.fov = kFieldOfView;
		params.falloff = 1.0f;
		params.nearDistance = 5.0f;
		params.depthBias = 1.0f;
		params.sceneGraphIndex = 0;
		params.restrictedNode = nullptr;
		params.lensFlareData = nullptr;
		auto* bs = a_scene->AddLight(light, params);
		if (!bs) {
			a_parent->DetachChild(light);
			return nullptr;
		}
		a_made = light;
		return bs;
	}
}
