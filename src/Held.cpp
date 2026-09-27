// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// A staff's own light while it is drawn. RE::Light reaches a weapon in the hand only through its enchantment shader, and
// some staves (the Sanguine Rose, the Wabbajack) carry none, so in the hand they had only the casting-hand light. The build
// writes each such staff's own light as a `held` line; this hangs it on the weapon node of whoever has that staff drawn
// (the player, in both views, and the actors near them) and takes it off when it is sheathed, unequipped or switched off.
// The light is made the travelling-light way (Streams.cpp, ReLight's method). Nothing on any record is edited.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::size_t kMaxLive = 16;

		struct Live
		{
			RE::FormID                      actor{ 0 };
			bool                            left{ false }, firstPerson{ false };
			const Stream*                   held{ nullptr };
			RE::NiPointer<RE::NiNode>       node;  // what it hangs on - held, so taking the light off never reaches a freed node
			RE::NiPointer<RE::BSLight>      bs;
			RE::NiPointer<RE::NiPointLight> light;
			float                           written{ -1.0f }, wroteReach{ -1.0f };
		};

		std::vector<Live> gLive;
		std::size_t       gTold = 0;

		struct Want
		{
			RE::Actor*    actor;
			bool          left, firstPerson;
			const Stream* held;
			RE::NiNode*   node;
		};

		void TakeOff(Live& a_v, RE::ShadowSceneNode* a_scene)
		{
			if (a_scene && a_v.bs) {
				a_scene->RemoveLight(a_v.bs);
			}
			if (a_v.node && a_v.light && a_v.light->parent == a_v.node.get()) {
				a_v.node->DetachChild(a_v.light.get());
			}
		}

		// the staves one actor has drawn, and the node each hangs on, in each view the actor has
		void WantsOf(RE::Actor* a_actor, bool a_hidden, std::vector<Want>& a_out)
		{
			if (!a_actor || !a_actor->Is3DLoaded() || !a_actor->AsActorState()->IsWeaponDrawn()) {
				return;
			}
			const bool isPlayer = a_actor->IsPlayerRef();
			if (isPlayer && a_hidden) {
				return;
			}
			for (const bool left : { false, true }) {
				auto*         weapon = a_actor->GetEquippedObject(left);
				const Stream* held = weapon && weapon->Is(RE::FormType::Weapon) ? HeldOf(weapon) : nullptr;
				if (!held || !OptionLit(held->option)) {
					continue;
				}
				const RE::BSFixedString name(left ? "SHIELD" : "WEAPON");
				for (const bool fp : { false, true }) {
					if (fp && !isPlayer) {
						break;
					}
					auto* root = a_actor->Get3D(fp);
					auto* node = root ? root->GetObjectByName(name) : nullptr;
					if (node && node->AsNode()) {
						a_out.push_back({ a_actor, left, fp, held, node->AsNode() });
					}
				}
			}
		}
	}

	void UpdateHeldLights()
	{
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!scene || !player) {
			return;
		}
		const bool        hidden = SneakOn() && player->IsSneaking();
		std::vector<Want> want;
		WantsOf(player, hidden, want);
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			for (auto& handle : lists->highActorHandles) {
				if (auto actor = handle.get(); actor && actor.get() != player) {
					WantsOf(actor.get(), false, want);
				}
			}
		}
		// what is no longer wanted, or hangs on a node the actor no longer has (a view switch rebuilds it), comes off
		std::erase_if(gLive, [&](Live& v) {
			const bool keep = std::ranges::any_of(want, [&v](const Want& w) {
				return w.actor->GetFormID() == v.actor && w.left == v.left && w.firstPerson == v.firstPerson && w.held == v.held &&
				       w.node == v.node.get();
			});
			if (!keep) {
				TakeOff(v, scene);
			}
			return !keep;
		});
		const float scale = Brightness();
		const float reachScale = Reach();
		for (const auto& w : want) {
			const bool have = std::ranges::any_of(gLive, [&w](const Live& v) {
				return w.actor->GetFormID() == v.actor && w.left == v.left && w.firstPerson == v.firstPerson;
			});
			if (have || gLive.size() >= kMaxLive) {
				continue;
			}
			const float       fade = w.held->fade * scale;
			const float       reach = w.held->radius * reachScale;
			RE::NiPointLight* light = nullptr;
			auto* bs = MakeOurLight(*w.held, w.held->color, w.held->positions.front(), fade, reach, w.node, scene, light);
			if (!bs) {
				SKSE::log::warn("[HELD] {}: the light could not be made or registered", w.held->key);
				continue;
			}
			gLive.push_back({ w.actor->GetFormID(), w.left, w.firstPerson, w.held, RE::NiPointer<RE::NiNode>(w.node),
				RE::NiPointer<RE::BSLight>(bs), RE::NiPointer<RE::NiPointLight>(light), fade, reach });
			if (gTold < 16) {
				++gTold;
				SKSE::log::info("[HELD] {} | {:08X} {} hand, {} person | fade {:.2f} | radius {:.0f}", w.held->key, w.actor->GetFormID(),
					w.left ? "left" : "right", w.firstPerson ? "first" : "third", fade, reach);
			}
		}
		// the sliders reach a light already hung
		for (auto& v : gLive) {
			const float fade = v.held->fade * scale;
			const float reach = v.held->radius * reachScale;
			if (fade != v.written || reach != v.wroteReach) {
				auto& d = v.light->GetLightRuntimeData();
				d.fade = fade;
				d.radius.x = reach;
				d.radius.y = reach;
				Isl::SetCutoff(v.light.get(), CutoffFor(fade, reach, v.held->size));
				v.written = fade;
				v.wroteReach = reach;
			}
		}
	}

	std::size_t LiveHeldLights() { return gLive.size(); }
}
