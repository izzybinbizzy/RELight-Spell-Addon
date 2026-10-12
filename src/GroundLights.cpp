// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The hand light that lights the GROUND, off Community Shaders (ENB and Vanilla) - Illuminated's GroundLights.cpp, copied
// (the two mods never run together; code is copied across, never shared at run time). HIS WORD: "relight casting
// light is not working ... re add the enb and cs and vanilla versions ... relight only uses illuminated vanilla, cs and enb
// configs where absolutely neccesary to make lights work, but relight is the main driver."
// The game makes a casting light so that it lights the caster but not the land (Dynamic Wards measured it,
// Illuminated: -0.09 on the ground before, +35 after its twin). So the casting light - ours or RE::Light's - is kept
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
			RE::NiPointer<RE::NiAVObject>   anchor;              // where it sits: magic's third-person counterpart (also in first person), or magic itself when there is none
			bool                            handsDown{ false };  // dropped because the hands went down: the game's light stays hidden
			RE::NiPointer<RE::NiAVObject>   magic;               // the nearest node above the casting light whose name holds "MagicNode", or the light's own parent when none does
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
			if (a_twin.source && !a_twin.handsDown && !WantedDark(a_twin.source.get())) {
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
				RE::NiPointer<RE::BSLight>(bs), true, {}, false, {} };
			return true;
		}

		// The skeleton's magic node above a casting light, in the light's own body. Most casting lights hang right under it, but a
		// casting art can carry its own light node - measured 2026-10-12 by the spell sweep (`Misc\Build work\relight spell sweep
		// 2026-10-12\`): Wall of Storms, Septimus Vanish, the storm atronach's bolts hang theirs at MagicNode > MagicRight >
		// BillboardHelperHACK > AttachLight, a name the third-person body does not have in first person, so the twin stayed
		// under the first-person arms and lit nothing. The magic node is in both bodies.
		RE::NiAVObject* MagicNodeAbove(RE::NiAVObject* a_light)
		{
			for (auto* node = a_light ? a_light->parent : nullptr; node; node = node->parent) {
				if (std::string_view(node->name.c_str()).find("MagicNode") != std::string_view::npos) {
					return node;
				}
			}
			return a_light ? a_light->parent : nullptr;  // no magic node above it: the node it hangs on, as before
		}

		// where a light sits in the frame of a node above it (the local transforms between them, composed)
		RE::NiPoint3 OffsetUnder(const RE::NiAVObject* a_node, const RE::NiAVObject* a_light)
		{
			RE::NiPoint3 at{};
			for (const auto* obj = a_light; obj && obj != a_node; obj = obj->parent) {
				// a billboard node is turned to face the camera every frame: its turn is the view's, not the hand's (the code review
				// of 2026-10-12), so only its offset counts - else the lit spot would swing with the camera
				const bool billboard = std::string_view(obj->name.c_str()).find("Billboard") != std::string_view::npos;
				at = billboard ? at + obj->local.translate : obj->local * at;
			}
			return at;
		}

		// this frame's numbers of the casting light onto the twin, the twin at the magic node's place
		void Follow(Twin& a_twin)
		{
			auto*       light = a_twin.light.get();
			auto*       source = a_twin.source.get();
			const auto* anchor = a_twin.anchor ? a_twin.anchor.get() : source->parent;
			auto&       data = light->GetLightRuntimeData();
			const float radius = data.radius.x;
			data = source->GetLightRuntimeData();
			if (data.radius.x != radius) {
				light->SetLightAttenuation(data.radius.x);
			}
			if (WantedDark(source)) {
				data.fade = 0.0f;
			}
			// the casting light's place under its own magic node, put under the anchor (the third-person magic node of that name)
			light->local = anchor ? anchor->local : RE::NiTransform{};
			light->local.translate = light->local * OffsetUnder(a_twin.magic.get(), source);
			RE::NiUpdateData update{};
			light->Update(update);
			if (!source->GetAppCulled()) {
				source->SetAppCulled(true);
			}
		}

		// In first person the casting light sits under the FIRST-person skeleton's magic node, and on ENB a light hung there lights
		// nothing in the world - Dynamic Wards measured it (DomeLights.cpp TickHands: the ground 45.6 / 44.2 / 45.4 with the light
		// on / off / on under the first-person nodes, 117.9 / 57.5 / 117.9 under the third-person ones). So the twin hangs at the
		// THIRD-person body's magic node of the same name (hidden in first person, it still moves with the actor); a node that is
		// already in the third-person body is kept.
		RE::NiAVObject* ThirdPersonTwin(RE::Actor* a_actor, RE::NiAVObject* a_magic)
		{
			// only a skeleton's magic node is looked up by name: it is unique per hand, where a generic node name (an art's
			// "AttachLight") would find the first of its kind - maybe the other hand's
			if (!a_magic || std::string_view(a_magic->name.c_str()).find("MagicNode") == std::string_view::npos) {
				return a_magic;
			}
			auto* body = a_actor->Get3D(false);
			if (!body) {
				return a_magic;
			}
			const auto* root = a_magic;
			while (root->parent && root != body) {
				root = root->parent;
			}
			if (root == body) {
				return a_magic;  // already in the third-person body (third person, and every NPC): no search
			}
			auto* same = body->GetObjectByName(a_magic->name);
			return same && same->parent ? same : a_magic;
		}

		void Visit(RE::Actor* a_actor, RE::ShadowSceneNode* a_scene)
		{
			if (!a_actor || !a_actor->Is3DLoaded()) {
				return;
			}
			const auto& casters = a_actor->GetActorRuntimeData().magicCasters;
			// Measured 2026-10-12 (his "There is a light thats not going away", then "Light is on the ground"; a probe build's log,
			// `Misc\Build work\hang hunt 2026-10-12\`): in THIRD person the game keeps the casting light on the hand, in the scene,
			// after the spell is put away - until it is unequipped - and only keeps it hidden; the twin copied it on, lighting the
			// ground with no spell readied (in first person the game takes the light away at once). So a twin stands only while
			// the hands are readied, and one dropped because they went down leaves the game's light hidden as the game keeps it.
			const auto* state = a_actor->AsActorState();
			const bool  readied = state && state->IsWeaponDrawn();
			for (std::uint64_t hand = 0; hand < 2; ++hand) {
				const auto key = (static_cast<std::uint64_t>(a_actor->GetFormID()) << 1) | hand;
				auto*      caster = casters[hand];
				auto*      bs = caster ? caster->light.get() : nullptr;
				auto*      source = bs ? netimmerse_cast<RE::NiPointLight*>(bs->light.get()) : nullptr;
				const auto it = gTwins.find(key);
				if (!source || !readied) {
					if (!readied && it != gTwins.end()) {
						it->second.handsDown = true;
					}
					continue;  // no casting light in this hand, or the hands are down: a twin it had is dropped with the unseen ones
				}
				auto* magic = MagicNodeAbove(source);
				auto* anchor = ThirdPersonTwin(a_actor, magic);
				auto* parent = anchor ? anchor->parent : nullptr;
				if (!parent) {
					continue;
				}
				auto& twin = it != gTwins.end() ? it->second : gTwins[key];
				if (twin.light && (twin.source.get() != source || twin.parent.get() != parent)) {
					Drop(twin, a_scene);
				}
				if (!twin.light && !Make(twin, source, parent, a_scene)) {
					continue;
				}
				if (twin.anchor.get() != anchor) {
					twin.anchor.reset(anchor);
				}
				if (twin.magic.get() != magic) {
					twin.magic.reset(magic);
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
