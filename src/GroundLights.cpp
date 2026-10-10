// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The hand light that lights the GROUND, off Community Shaders (ENB and Vanilla) - Illuminated's GroundLights.cpp, copied
// (the two mods never run together; code is copied across, never shared at run time). HIS WORD 2026-10-10: "relight casting
// light is not working ... re add the enb and cs and vanilla versions ... relight only uses illuminated vanilla, cs and enb
// configs where absolutely neccesary to make lights work, but relight is the main driver."
// The game makes a casting light so that it lights the caster but not the land (Dynamic Wards measured it 2026-10-05,
// Illuminated 2026-10-09: -0.09 on the ground before, +35 after its twin). So the casting light - ours or RE::Light's - is kept
// (Brightness, the fading module, sneaking and the switches all keep writing to it) but hidden, and a twin made the way
// Dynamic Wards makes its hand light (land and water lighting on) copies it every frame. The twin hangs beside the hand's
// magic node, not under it (the fading module scales every light under the magic node; the twin copies a light it scaled).
// A light the mod keeps dark now (sneaking, a switched-off option, a hand light held out - WantedDark) keeps its twin dark too.
// Under Community Shaders the casting light lights the ground itself: nothing is made.
//
// CREDIT: a light is made and registered the way ReLight by Truman does it (github.com/TrumanGIT/ReLight, GPL-3.0-or-later,
// used with his permission): a master NiPointLight cloned for every use and handed to the shadow scene node.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kName = "RelightSpellAddonGroundLight";

		struct Twin
		{
			RE::NiPointer<RE::NiPointLight> source;  // the casting light it copies (hidden while the twin stands)
			RE::NiPointer<RE::NiNode>       parent;  // the hand node the twin hangs on
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
			bool                            seen{ false };
		};

		std::unordered_map<std::uint64_t, Twin> gTwins;  // main thread only; key = actor form id << 1 | hand

		void Drop(Twin& a_twin, RE::ShadowSceneNode* a_scene)
		{
			if (a_scene && a_twin.bs) {
				a_scene->RemoveLight(a_twin.bs);
			}
			if (a_twin.light && a_twin.light->parent) {
				a_twin.light->parent->DetachChild(a_twin.light.get());
			}
			if (a_twin.source && !WantedDark(a_twin.source.get())) {
				a_twin.source->SetAppCulled(false);  // the casting light shows again
			}
			a_twin = {};
		}

		bool Make(Twin& a_twin, RE::NiPointLight* a_source, RE::NiNode* a_hand, RE::ShadowSceneNode* a_scene)
		{
			auto* light = LightKit::CloneLight();
			if (!light) {
				return false;
			}
			light->name = kName;
			light->GetLightRuntimeData() = a_source->GetLightRuntimeData();
			light->SetLightAttenuation(light->GetLightRuntimeData().radius.x);
			light->fadeAmount = kMovingLightMark;  // RE::Light's flicker prevention lets it reach every surface (Plugin.h)
			a_hand->AttachChild(light, true);
			RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};  // Dynamic Wards' hand light (DomeLights.cpp Make)
			params.dynamic = true;
			params.shadowLight = false;
			params.portalStrict = true;
			params.affectLand = true;
			params.affectWater = true;
			params.neverFades = true;
			params.fov = 90.0f;
			params.falloff = 1.0f;
			params.nearDistance = 5.0f;
			params.depthBias = 1.0f;
			auto* bs = a_scene->AddLight(light, params);
			if (!bs) {
				a_hand->DetachChild(light);
				return false;
			}
			a_twin = { RE::NiPointer<RE::NiPointLight>(a_source), RE::NiPointer<RE::NiNode>(a_hand), RE::NiPointer<RE::NiPointLight>(light),
				RE::NiPointer<RE::BSLight>(bs), true };
			return true;
		}

		// this frame's numbers of the casting light onto the twin, the twin at the magic node's place
		void Follow(Twin& a_twin)
		{
			auto*       light = a_twin.light.get();
			auto*       source = a_twin.source.get();
			const auto* magic = source->parent;
			auto&       data = light->GetLightRuntimeData();
			const float radius = data.radius.x;
			data = source->GetLightRuntimeData();
			if (data.radius.x != radius) {
				light->SetLightAttenuation(data.radius.x);
			}
			if (WantedDark(source)) {
				data.fade = 0.0f;
			}
			light->local = magic ? magic->local : RE::NiTransform{};
			light->local.translate += source->local.translate;
			RE::NiUpdateData update{};
			light->Update(update);
			if (!source->GetAppCulled()) {
				source->SetAppCulled(true);
			}
		}

		void Visit(RE::Actor* a_actor, RE::ShadowSceneNode* a_scene)
		{
			if (!a_actor || !a_actor->Is3DLoaded()) {
				return;
			}
			const auto& casters = a_actor->GetActorRuntimeData().magicCasters;
			for (std::uint64_t hand = 0; hand < 2; ++hand) {
				const auto key = (static_cast<std::uint64_t>(a_actor->GetFormID()) << 1) | hand;
				auto*      caster = casters[hand];
				auto*      bs = caster ? caster->light.get() : nullptr;
				auto*      source = bs ? netimmerse_cast<RE::NiPointLight*>(bs->light.get()) : nullptr;
				auto*      magic = source ? source->parent : nullptr;
				auto*      parent = magic ? magic->parent : nullptr;
				const auto it = gTwins.find(key);
				if (!parent) {
					continue;  // no casting light in this hand: a twin it had is dropped with the unseen ones
				}
				auto& twin = it != gTwins.end() ? it->second : gTwins[key];
				if (twin.light && (twin.source.get() != source || twin.parent.get() != parent)) {
					Drop(twin, a_scene);
				}
				if (!twin.light && !Make(twin, source, parent, a_scene)) {
					continue;
				}
				twin.seen = true;
				Follow(twin);
			}
		}
	}

	void TickGroundLights()
	{
		if (IslShader()) {
			return;  // Community Shaders: the casting light lights the ground itself
		}
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!scene || gGroundLightsOn.load(std::memory_order_relaxed) < 0.5f) {  // switched off in the advanced settings file
			for (auto& entry : gTwins) {
				Drop(entry.second, scene);  // with a scene, each twin leaves its lists too (CodeRabbit, RELight #8)
			}
			gTwins.clear();
			return;
		}
		for (auto& entry : gTwins) {
			entry.second.seen = false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		Visit(player, scene);
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			for (auto& handle : lists->highActorHandles) {
				if (auto actor = handle.get(); actor && actor.get() != player) {
					Visit(actor.get(), scene);
				}
			}
		}
		std::erase_if(gTwins, [&](auto& a_entry) {
			if (!a_entry.second.seen) {
				Drop(a_entry.second, scene);
				return true;
			}
			return false;
		});
	}

	std::size_t GroundLightCount() { return gTwins.size(); }
}
