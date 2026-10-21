// RELight - Spell Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// A row of small lights along each bolt, flame spray and cone our data lights - Illuminated's row (its StreamLights.cpp), as
// HIS ANSWERS 2026-10-20 asked for both mods: checklist 140, "if you plain out cannot make part 1 of 140 to happen then revert
// to this as a backup plan" (one stretched light cannot be drawn inside our mods), then "Looks good. Do the flame sprays and
// cones in Illuminated first, then RELight", and 146 / 147 closed: "for now its the 12 or however many point lights you ended
// up using". The projectile wears its own light at its own size (Streams.cpp); the row lights its length - lights no farther
// apart than 80 units, laid again every frame to where its art ends, where it hit something, or its range (BeamRow.h, the
// same file as Illuminated's). Off on Performance (his Quality / Performance order of 2026-10-20), with All lights off, with
// its option switched off, and while the player sneaks with "Lights off while sneaking" on.
// Truman's call after the Lightning Bolt freeze still stands for the loader threads: the Load3D hook only QUEUES the
// projectile (QueueRow, any thread); every light is made, moved and taken off here on the main thread (UpdateRows, in the
// player update).
//
// CREDIT: a light cloned from one master light, marked as moving and handed to the shadow scene node - ReLight by Truman
// (github.com/TrumanGIT/ReLight, GPL-3.0-or-later, used with his permission), as Held.cpp MakeOwnLight.

#include "BeamRow.h"
#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::size_t kMostRowLights = 64;  // every row light in the scene together
		constexpr std::size_t kMostPerRow = 20;
		constexpr float       kRowHeight = 100.0f;  // a beam's usual height over the ground it lights
		constexpr const char* kRowName = "RSARow";

		std::unordered_map<RE::FormID, RowRecipe> gRecipes;  // projectile base -> its row; written once at data load, read-only after
		std::atomic<bool>                         gReady{ false };

		struct Live
		{
			RE::NiPointer<RE::NiAVObject>           root;
			RE::NiPointer<RE::TESObjectREFR>        ref;
			const RowRecipe*                        recipe{ nullptr };
			std::vector<RE::NiPointer<RE::BSLight>> lights;
		};
		std::unordered_map<const RE::NiAVObject*, Live> gLive;  // main thread
		std::size_t                                     gLights = 0;
		std::size_t                                     gMade = 0;
		std::mutex                                      gQueueLock;
		std::vector<RE::NiPointer<RE::TESObjectREFR>>   gQueue;  // what a loader thread loaded, for the next frame (under gQueueLock)

		// his look 2026-10-20 ~19:00 (Illuminated's row, the same numbers): "make vanilla brighter enb softer as suggested to
		// align with cs"
		[[nodiscard]] float RowStrengthFor(Lighting a_lighting)
		{
			switch (a_lighting) {
			case Lighting::kVanilla:
				return 1.8f;
			case Lighting::kEnb:
				return 0.3f;
			default:
				return 1.0f;
			}
		}

		[[nodiscard]] bool RowsOn(const RE::PlayerCharacter* a_player)
		{
			return !PerformanceMode() && !AllLightsOff() && !(SneakOn() && a_player && a_player->IsSneaking());
		}

		// a light's strength, reach and color written whole. Community Shaders draws an inverse-square point light at four
		// times its fade and writes back the reach that gives (Illuminated's run cs 2026-10-20 18:15: every row light read
		// twice the reach laid) - so the cutoff is set for that, and the fade lowered where the cutoff would pass its top
		void Dress(RE::NiPointLight* a_light, const RE::NiColor& a_color, float a_fade, float a_radius, bool a_isl)
		{
			auto& data = a_light->GetLightRuntimeData();
			data.diffuse = a_color;
			data.fade = a_fade;
			data.radius = { a_radius, a_radius, LightKit::kLightSize };
			a_light->SetLightAttenuation(a_radius);
			if (a_isl) {
				constexpr float kDrawnTimes = 4.0f;
				const float     room = a_radius * a_radius + LightKit::kK * LightKit::kLightSize * LightKit::kLightSize * 0.5f;
				data.fade = (std::min)(a_fade, Isl::kHighestCutoff * room / (kDrawnTimes * LightKit::kK));
				Isl::SetOwn(a_light);
				Isl::SetCutoff(a_light, kDrawnTimes * LightKit::kK * data.fade / room);
			} else {
				data.ambient = { a_color.red * 0.1f, a_color.green * 0.1f, a_color.blue * 0.1f };  // RE::Light's rule (Truman)
			}
		}

		// where a row starts and which way it runs: a bolt along its model's +Y from its root; a flame spray or cone along the
		// projectile's own aim from where it stands (a spray's model points another way - measured 2026-10-11: its forward is -X)
		struct Axis
		{
			RE::NiPoint3 start;
			RE::NiPoint3 dir{ 0.0f, 1.0f, 0.0f };
		};

		[[nodiscard]] Axis AxisOf(const RE::TESObjectREFR* a_ref, const RE::NiAVObject* a_root, bool a_beam)
		{
			if (a_beam) {
				return { a_root->world.translate, a_root->world.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f } };
			}
			const float pitch = a_ref->GetAngleX(), yaw = a_ref->GetAngleZ();
			return { a_ref->GetPosition(), { std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) } };
		}

		// how far along the axis the drawn art reaches (its geometry's bounds); 0 when nothing is drawn
		[[nodiscard]] float DrawnLength(RE::NiAVObject* a_root, const Axis& a_axis)
		{
			float reach = 0.0f;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto& bound = a_geometry->worldBound;
				if (bound.radius > 0.0f) {
					reach = (std::max)(reach, (bound.center - a_axis.start).Dot(a_axis.dir) + bound.radius);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return std::isfinite(reach) ? reach : 0.0f;
		}

		// how long a row is now: where the drawn art ends, never past the newest impact, never past the range
		[[nodiscard]] float RowLength(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root, const Axis& a_axis)
		{
			auto* proj = a_ref ? a_ref->As<RE::Projectile>() : nullptr;
			if (!proj || !a_root) {
				return 0.0f;
			}
			auto& rt = proj->GetProjectileRuntimeData();
			float length = rt.range;
			if (const float drawn = DrawnLength(a_root, a_axis); drawn > 1.0f && drawn < length) {
				length = drawn;
			}
			for (const auto* impact : rt.impacts) {  // the newest impact comes first
				if (impact) {
					const float hit = a_axis.start.GetDistance(impact->desiredTargetLoc);
					if (hit > 0.0f && hit < length) {
						length = hit;
					}
					break;
				}
			}
			return std::isfinite(length) ? length : 0.0f;
		}

		void Forget(const RE::NiAVObject* a_root, RE::ShadowSceneNode* a_scene)
		{
			const auto it = gLive.find(a_root);
			if (it == gLive.end()) {
				return;
			}
			for (auto& bs : it->second.lights) {
				if (!bs) {
					continue;
				}
				if (a_scene) {
					a_scene->RemoveLight(bs);
				}
				if (auto* light = bs->light.get(); light && light->parent) {
					light->parent->DetachChild(light);  // no light node left on a projectile that lives on
				}
			}
			gLights -= (std::min)(gLights, it->second.lights.size());
			gLive.erase(it);
		}

		void Hang(RE::TESObjectREFR* a_ref, RE::ShadowSceneNode* a_scene)
		{
			auto*      root = a_ref ? a_ref->Get3D() : nullptr;
			auto*      node = root ? root->AsNode() : nullptr;
			auto*      base = a_ref ? a_ref->GetBaseObject() : nullptr;
			auto*      proj = a_ref ? a_ref->As<RE::Projectile>() : nullptr;
			const auto recipe = base ? gRecipes.find(base->GetFormID()) : gRecipes.end();
			if (!node || !node->parent || !proj || recipe == gRecipes.end() || gLive.contains(node) || gLights >= kMostRowLights) {
				return;
			}
			if (recipe->second.option != kNone && !OptionLit(recipe->second.option)) {
				return;
			}
			const auto want = BeamRow::Lay(proj->GetProjectileRuntimeData().range, static_cast<int>(kMostPerRow), recipe->second.reach, kRowHeight);
			const auto n = (std::min)(want.size(), kMostRowLights - gLights);
			const bool isl = InverseSquare();
			Live       live{ RE::NiPointer<RE::NiAVObject>(node), RE::NiPointer<RE::TESObjectREFR>(a_ref), &recipe->second, {} };
			for (std::size_t i = 0; i < n; ++i) {
				auto* light = LightKit::CloneLight();
				if (!light) {
					break;
				}
				light->name = kRowName;
				light->fadeAmount = kMovingLightMark;                 // so RE::Light's flicker prevention lets a moving light reach every surface
				Dress(light, recipe->second.color, 0.0f, 1.0f, isl);  // dark until UpdateRows lays it out
				light->local.translate = {};
				light->local.scale = 1.0f;
				const RE::NiPointer<RE::NiPointLight> hold(light);  // freed here if it is never registered
				node->AttachChild(light, true);
				RE::NiUpdateData update{};
				light->Update(update);
				if (auto* bs = LightKit::AddToScene(a_scene, light, 2.0f)) {
					live.lights.emplace_back(bs);
				} else {
					node->DetachChild(light);
				}
			}
			if (live.lights.empty()) {
				return;
			}
			if (gMade < 6) {
				SKSE::log::info("[ROW] {:08X} | {} light(s) along {:.0f} | reach target {:.0f} | fade {:.2f}", base->GetFormID(), live.lights.size(),
					proj->GetProjectileRuntimeData().range, recipe->second.reach, recipe->second.fade);
			}
			++gMade;
			gLights += live.lights.size();
			gLive.emplace(node, std::move(live));
		}

		void Move()
		{
			const bool isl = InverseSquare();
			// the two sliders and Dim in daylight, then the per-lighting strength the rows were tuned with (no plain-drawing step:
			// RowStrengthFor already holds the Vanilla / ENB difference)
			const float      bright = Brightness() * DaylightFactor() * RowStrengthFor(LightingFound()), reachPct = Reach();
			RE::NiUpdateData update{};
			for (auto& [root, live] : gLive) {
				const auto& rec = *live.recipe;
				const Axis  axis = AxisOf(live.ref.get(), live.root.get(), rec.beam);
				const float length = RowLength(live.ref.get(), live.root.get(), axis);
				if (length <= 1.0f) {
					continue;
				}
				const auto  toLocal = live.root->world.Invert();
				const auto  row = BeamRow::Lay(length, static_cast<int>(live.lights.size()), rec.reach, kRowHeight);
				const float rootScale = live.root->world.scale > 0.0f ? live.root->world.scale : 1.0f;
				for (std::size_t i = 0; i < live.lights.size(); ++i) {
					auto* light = live.lights[i] ? netimmerse_cast<RE::NiPointLight*>(live.lights[i]->light.get()) : nullptr;
					if (!light) {
						continue;
					}
					if (i >= row.size()) {  // a row cut so short it needs fewer: the spare lights rest dark at the hand
						Dress(light, rec.color, 0.0f, 1.0f, isl);
						continue;
					}
					const auto& s = row[i];
					Dress(light, rec.color, rec.fade * s.strength * bright, s.reach * reachPct, isl);
					if (rec.beam) {
						light->local.translate = { 0.0f, s.along * length / rootScale, 0.0f };  // in the root's own units
					} else {
						light->local.translate = toLocal * (axis.start + axis.dir * (s.along * length));
					}
					light->Update(update);
				}
			}
		}
	}

	void SetRowRecipes(std::unordered_map<RE::FormID, RowRecipe>&& a_recipes)
	{
		if (gReady.load(std::memory_order_acquire)) {
			return;  // once a session: a loader thread may be reading them
		}
		gRecipes = std::move(a_recipes);
		gReady.store(true, std::memory_order_release);
		const auto beams = std::ranges::count_if(gRecipes, [](const auto& a_r) { return a_r.second.beam; });
		SKSE::log::info("rows: {} bolt(s) and {} spray(s) / cone(s) wear a row of lights along their length (at most {} per row, {} in the scene)",
			beams, gRecipes.size() - static_cast<std::size_t>(beams), kMostPerRow, kMostRowLights);
	}

	void QueueRow(RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref && gReady.load(std::memory_order_acquire) ? a_ref->GetBaseObject() : nullptr;
		if (!base || !gRecipes.contains(base->GetFormID())) {
			return;
		}
		std::lock_guard l{ gQueueLock };
		gQueue.emplace_back(a_ref);  // held by its refcount; no handle made on a loader thread
	}

	void UpdateRows()
	{
		auto*                                         scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		std::vector<RE::NiPointer<RE::TESObjectREFR>> queued;
		{
			std::lock_guard l{ gQueueLock };
			queued.swap(gQueue);
		}
		const bool on = RowsOn(RE::PlayerCharacter::GetSingleton());
		// gone (its 3D out of the world or rebuilt), switched off, or every row off: taken off the scene now
		std::vector<const RE::NiAVObject*> drop;
		for (const auto& [root, live] : gLive) {
			const bool gone = !live.root->parent || !live.ref || live.ref->Get3D() != live.root.get();
			if (!on || gone || (live.recipe->option != kNone && !OptionLit(live.recipe->option))) {
				drop.push_back(root);
			}
		}
		for (const auto* root : drop) {
			Forget(root, scene);
		}
		if (on && scene) {
			for (const auto& ref : queued) {
				Hang(ref.get(), scene);
			}
		}
		if (!gLive.empty()) {
			Move();
		}
	}

	void DropRows()
	{
		auto*                              scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		std::vector<const RE::NiAVObject*> all;
		for (const auto& [root, live] : gLive) {
			all.push_back(root);
		}
		for (const auto* root : all) {
			Forget(root, scene);
		}
		std::lock_guard l{ gQueueLock };
		gQueue.clear();
	}

	bool IsRowLight(const RE::NiLight* a_light)
	{
		return a_light && std::string_view(a_light->name.c_str()) == kRowName;
	}

	std::size_t LiveRowLights() { return gLights; }
	std::size_t RowsMade() { return gMade; }
}
