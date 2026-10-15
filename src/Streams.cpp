// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Sprays, breath shouts and beams. 2.3 - HIS ANSWER 2026-10-14 ~23:55, asked whether a spray or a bolt may carry a second light:
// "do it like illuminated, i checked there and it looks good". So each spray, flamethrower and beam projectile our data lights
// wears an in-memory COPY of its own light record, raised the way Illuminated's merged Streams raise it (its StreamLights.cpp):
// brightness 1.72 at size 2.5, reach the projectile's whole length plus 15 %, bounded by what one light carries at a cutoff of
// 0.01, never under the hand reach, never dimmer than its own original - ONE light per projectile, never lights hung along it.
// (RE::Light's spray config of 2.1 / 2.2 reached only part of a spray - his finding 2026-10-14 - and RE::Light does not apply a
// config to a beam's light at all, measured 2.2.) The copy is made by the game's own light function, never by RE::Light's hook,
// and dressed here (Hooks.cpp MakeMagicLight, as the hand lights are): the moving-light mark, under Community Shaders the
// inverse square words, off it the same reach drawn plain.
// The records' OTHER users: a spray record is taken off them, as in 2.1 / 2.2 (RE::Light's own configs light those objects - a
// second light would double them); a beam record's other users (a lightning arrow, a shock explosion ...) wear a plain copy - the
// light they had - made by the game's own function too, so they keep it even where RE::Light is set to switch off game lights it
// has no config for (his call 2026-10-14 ~11:40: "fix it for later update").
// ⚠ Truman's call after the Lightning Bolt freeze still stands: nothing here hangs a light on a projectile's 3D.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// Illuminated's numbers (StreamLights.cpp): the house hand light's peak at the spray size, the end plus 15 %
		constexpr float kOwnSize = 2.5f;
		constexpr float kOwnFade = 1077.69f * kOwnSize * kOwnSize / LightKit::kK;
		constexpr float kOwnEndReach = 1.15f;
		constexpr float kOwnShortestReach = 133.0f;
		constexpr float kOwnLowestCutoff = 0.01f;

		// how a raised copy's light is drawn - worked out once, read from any thread once gReady is set
		struct Dress
		{
			float fade{ 0.0f }, reach{ 0.0f }, cutoff{ 0.0f };
			bool  isl{ false };
		};

		std::unordered_set<const RE::TESObjectLIGH*>        gSpraySet;  // the data's spray records
		std::unordered_map<const RE::TESObjectLIGH*, Dress> gRaised;    // our raised copies, one per spray / bolt projectile
		std::unordered_set<const RE::TESObjectLIGH*>        gPlain;     // the beam records' other users' plain copies
		std::atomic<bool>                                   gReady{ false };

		// how many of each kind of user a claim changed
		struct LightUsers
		{
			std::size_t projectiles{ 0 }, muzzles{ 0 }, effects{ 0 }, explosions{ 0 }, hazards{ 0 };
		};

		// every record field that names a light record - a projectile's own light and its muzzle flash, a magic effect's, an
		// explosion's, a hazard's - handed to a_change(the projectile whose own light it is, or nullptr; the field), which returns
		// true when it changed that field. One walk, so a user kind is never in one claim and missing from another (2.2's review)
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

		// the whole light as its user had it: look, sound, emittance, lens flare and model (Illuminated's FormCopies.cpp CopyLight)
		RE::TESObjectLIGH* CopyOf(const RE::TESObjectLIGH* a_light)
		{
			auto* copy = a_light ? LightKit::NewForm<RE::TESObjectLIGH>() : nullptr;
			if (!copy) {
				return nullptr;
			}
			copy->data = a_light->data;
			copy->fade = a_light->fade;
			copy->sound = a_light->sound;
			copy->emittanceColor = a_light->emittanceColor;
			copy->lensFlare = a_light->lensFlare;
			copy->SetModel(a_light->GetModel());
			return copy;
		}

		// a_copy raised from a_base as Illuminated raises a stream's own light, its color his where the data names one
		Dress Raise(RE::TESObjectLIGH* a_copy, const RE::TESObjectLIGH* a_base, float a_range)
		{
			const bool ownIsl = (a_base->data.flags.underlying() & LightKit::kRecordInverseSquare) != 0;
			Dress      d;
			d.fade = LightKit::NeverDimmer(kOwnFade, kOwnSize, a_base->fade, ownIsl ? LightKit::LightPlacerSize(a_base->data.fov) : 0.0f);
			const float limit = std::sqrt((std::max)(LightKit::kK * d.fade / kOwnLowestCutoff - kOwnSize * kOwnSize, 1.0f));
			d.reach = std::clamp(std::isfinite(a_range) ? a_range * kOwnEndReach : kOwnShortestReach, kOwnShortestReach, limit);
			d.isl = InverseSquare();
			// the record itself carries the raised light too, as Illuminated's Raise writes it: a light the game makes from it
			// anywhere our hook does not see (a beam's - RE::Light's hook on the same call sites never configured one, measured
			// 2.2) is drawn the same as one DressStreamLight dresses
			if (d.isl) {
				// Community Shaders draws a record carrying the inverse-square flag by its fade and cutoff - the falloff field IS
				// the cutoff then, the field of view its size
				d.cutoff = LightKit::kK * d.fade / (d.reach * d.reach + kOwnSize * kOwnSize);
				a_copy->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(a_copy->data.flags.underlying() | LightKit::kRecordInverseSquare);
				a_copy->data.fallofExponent = d.cutoff;
				a_copy->data.fov = kOwnSize;
			} else {
				// ENB and Vanilla: the same reach drawn plain by the game's own lighting (Illuminated's LightKit::PlainAt), the
				// falloff left as the record has it
				const auto plain = LightKit::PlainAt(d.fade, 0.0f, d.reach);
				d.fade = plain.fade;
				d.reach = plain.radius;
				a_copy->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(a_copy->data.flags.underlying() & ~LightKit::kRecordInverseSquare);
			}
			if (const auto his = RecordColor(a_base->GetFormID())) {
				a_copy->data.color.red = his->red;
				a_copy->data.color.green = his->green;
				a_copy->data.color.blue = his->blue;
			}
			a_copy->fade = d.fade;
			a_copy->data.radius = static_cast<std::uint32_t>(std::lround(d.reach));
			return d;
		}
	}

	// once a session: the hooks read these sets from any thread once gReady is set, and a second walk would find every field
	// already wearing its copy - so it is never redone
	void RaiseStreamLights()
	{
		if (gReady.load(std::memory_order_acquire)) {
			SKSE::log::warn("stream lights: already raised this session - left as they are");
			return;
		}
		std::unordered_set<const RE::TESObjectLIGH*> beams;
		for (const auto id : SprayLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id)) {
				gSpraySet.insert(l);
			}
		}
		for (const auto id : BeamLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id); l && !gSpraySet.contains(l)) {
				beams.insert(l);
			}
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if ((gSpraySet.empty() && beams.empty()) || !dh) {
			gReady.store(true, std::memory_order_release);
			return;
		}
		std::unordered_map<const RE::TESObjectLIGH*, RE::TESObjectLIGH*> plainOf;
		std::size_t                                                      sprays = 0, bolts = 0, failed = 0, dark = 0;
		const auto                                                       n = ForEachLightField(*dh, [&](const RE::BGSProjectile* a_proj, RE::TESObjectLIGH*& a_light) {
			const bool sprayRecord = a_light && gSpraySet.contains(a_light);
			if (!a_light || (!sprayRecord && !beams.contains(a_light))) {
				return false;
			}
			// a spray (flamethrower or cone) on a spray record, or a beam our data lights: its own raised copy
			const bool spray = a_proj && sprayRecord && (a_proj->IsFlamethrower() || a_proj->IsCone());
			const bool bolt = a_proj && a_proj->IsBeam() && StreamOf(a_proj);
			if (spray || bolt) {
				auto* copy = CopyOf(a_light);
				if (!copy) {
					++failed;  // no copy: it keeps its record (and RE::Light's config) as in 2.2 - said in the log
					return false;
				}
				gRaised.emplace(copy, Raise(copy, a_light, a_proj->data.range));
				a_light = copy;
				(spray ? sprays : bolts) += 1;
				return true;
			}
			// anything else on a spray record goes dark, as in 2.1 / 2.2 (RE::Light's own configs light those objects)
			if (sprayRecord) {
				a_light = nullptr;
				++dark;
				return true;
			}
			// anything else on a beam record keeps the light it had, made by the game's own function (Hooks.cpp)
			auto [it, fresh] = plainOf.try_emplace(a_light, nullptr);
			if (fresh) {
				it->second = CopyOf(a_light);
				if (it->second) {
					gPlain.insert(it->second);
				} else {
					++failed;
				}
			}
			if (!it->second) {
				return false;
			}
			a_light = it->second;
			return true;
		});
		gReady.store(true, std::memory_order_release);
		SKSE::log::info(
			"stream lights: {} spray(s) and {} bolt(s) wear a raised copy of their own light ({} drawn {}); the records' other users - "
			"{} projectile(s), {} muzzle flash(es), {} magic effect(s), {} explosion(s), {} hazard(s): {} on a spray record left dark, "
			"the rest on {} plain copy(ies) of a beam record{}",
			sprays, bolts, gRaised.size(), InverseSquare() ? "inverse square" : "plain", n.projectiles - sprays - bolts, n.muzzles,
			n.effects, n.explosions, n.hazards, dark, gPlain.size(),
			failed ? std::format("; {} copy(ies) could not be made - those users keep the record", failed) : std::string{});
	}

	bool IsOurLight(const RE::TESObjectLIGH* a_light)
	{
		return a_light && gReady.load(std::memory_order_acquire) && (gRaised.contains(a_light) || gPlain.contains(a_light));
	}

	void DressStreamLight(RE::NiLight* a_light, const RE::TESObjectLIGH* a_record)
	{
		if (!a_light || !a_record || !gReady.load(std::memory_order_acquire)) {
			return;
		}
		// the moving-light mark on every copy, plain ones included - as RE::Light's own hook marks the magic lights it passes, so
		// its flicker prevention lets them reach every surface
		a_light->fadeAmount = kMovingLightMark;
		const auto it = gRaised.find(a_record);
		if (it == gRaised.end()) {
			return;  // a plain copy: otherwise the light the game made, as it made it
		}
		const auto& d = it->second;
		auto&       data = a_light->GetLightRuntimeData();
		data.fade = d.fade;
		if (d.isl) {
			data.radius = { d.reach, d.reach, kOwnSize };
			Isl::SetOn(a_light);
			Isl::SetCutoff(a_light, d.cutoff);
			return;
		}
		data.radius = { d.reach, d.reach, LightKit::kLightSize };
		if (auto* point = netimmerse_cast<RE::NiPointLight*>(a_light)) {
			point->SetLightAttenuation(d.reach);
		}
		const RE::NiColor c{ a_record->data.color.red / 255.0f, a_record->data.color.green / 255.0f, a_record->data.color.blue / 255.0f };
		data.ambient = { c.red * 0.1f, c.green * 0.1f, c.blue * 0.1f };  // after the attenuation's words (RE::Light's rule, Truman)
	}

	std::size_t RaisedLights() { return gRaised.size(); }
	std::size_t PlainLights() { return gPlain.size(); }

	bool IsSprayLight(const RE::TESObjectLIGH* a_light)
	{
		return a_light && (gSpraySet.contains(a_light) || (gReady.load(std::memory_order_acquire) && gRaised.contains(a_light)));
	}

	bool IsStreamObject(const RE::TESObjectREFR* a_ref)
	{
		return a_ref && StreamOf(a_ref->GetBaseObject()) != nullptr;
	}
}
