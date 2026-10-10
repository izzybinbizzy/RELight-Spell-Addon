// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// A weapon's own light while it is drawn. RE::Light lights a weapon's mesh only where the weapon is a reference (on the
// ground, on a rack) and reaches a weapon in the hand only through its enchantment shader. The build writes the light each
// staff and weapon config of ours gives that mesh as a `held` line; this hangs it on the drawn weapon of whoever holds it
// (the player, in both views, and the actors near them) and takes it off when it is sheathed, unequipped or switched off.
// One object, one light: a weapon whose enchantment shader is one of ours already wears RE::Light's enchantment light in
// the hand, so it gets none here. The light is made the travelling-light way (Streams.cpp, ReLight's method). Nothing on
// any record is edited.

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
			int                             element{ 0 };  // its enchantment's element: Fire / Frost / Shock color picks
			float                           written{ -1.0f }, wroteReach{ -1.0f };
			RE::NiColor                     wroteColor{ -1.0f, -1.0f, -1.0f };
		};

		std::vector<Live> gLive;
		std::size_t       gTold = 0;

		struct Want
		{
			RE::Actor*    actor;
			bool          left, firstPerson;
			const Stream* held;
			RE::NiNode*   node;
			int           element;
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

		[[nodiscard]] bool BothHands(const RE::TESObjectWEAP* a_weapon)
		{
			using T = RE::WEAPON_TYPE;
			const auto type = a_weapon->GetWeaponType();
			return type == T::kTwoHandSword || type == T::kTwoHandAxe || type == T::kBow || type == T::kCrossbow;
		}

		// RE::Light may hang its enchantment light on this weapon in the hand: an effect of its enchantment (the instance's own,
		// else the weapon's) carries an enchantment shader a config of ours names
		[[nodiscard]] bool EnchantmentLit(RE::Actor* a_actor, bool a_left, const RE::TESObjectWEAP* a_weapon)
		{
			const auto* entry = a_actor->GetEquippedEntryData(a_left);
			const auto* ench = entry ? entry->GetEnchantment() : nullptr;
			if (!ench) {
				ench = a_weapon->formEnchanting;
			}
			if (!ench) {
				return false;
			}
			return std::ranges::any_of(ench->effects, [](const RE::Effect* a_e) {
				return a_e && a_e->baseEffect && OptionOfShader(a_e->baseEffect->data.enchantShader) != kNone;
			});
		}

		// the element of the weapon's enchantment: what its effects are resisted by, 0 when there is none or they disagree
		[[nodiscard]] int EnchantElement(RE::Actor* a_actor, bool a_left, const RE::TESObjectWEAP* a_weapon)
		{
			const auto* entry = a_actor->GetEquippedEntryData(a_left);
			const auto* ench = entry ? entry->GetEnchantment() : nullptr;
			if (!ench) {
				ench = a_weapon->formEnchanting;
			}
			int element = 0;
			if (!ench) {
				return 0;
			}
			for (const auto* e : ench->effects) {
				const int one = e ? ElementOf(e->baseEffect) : 0;
				if (one && element && one != element) {
					return 0;
				}
				element = one ? one : element;
			}
			return element;
		}

		// ...and has it? Measured 2026-09-30 on a Bound Sword: some games it does (its "RL" light, fadeAmount 5, on the weapon),
		// others none at all (after a load, in a long session) - so the held line lights the weapon whenever that light is not
		// there, in this view, and steps aside the frame it is
		[[nodiscard]] bool ReLightOn(RE::NiAVObject* a_node)
		{
			bool found = false;
			RE::BSVisit::TraverseScenegraphLights(a_node, [&found](RE::NiPointLight* a_light) {
				if (IsEnchantLight(a_light)) {  // RE::Light's enchantment light (Plugin.h)
					found = true;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return found;
		}

		// the drawn weapon's own 3D in this view, parented to the hand node - the config's position is relative to the mesh.
		// A bow is held in the left hand, so its 3D may sit under SHIELD though it is equipped in the right. Falls back to the
		// hand node itself when the biped has no clone of it.
		[[nodiscard]] RE::NiNode* NodeFor(RE::Actor* a_actor, bool a_left, bool a_firstPerson, const RE::TESObjectWEAP* a_weapon)
		{
			auto* root = a_actor->Get3D(a_firstPerson);
			if (!root) {
				return nullptr;
			}
			const std::string_view hand = a_left ? "SHIELD" : "WEAPON";
			if (const auto& biped = a_actor->GetBiped(a_firstPerson)) {
				RE::NiNode* other = nullptr;
				for (auto& obj : biped->objects) {
					auto*       clone = obj.item == a_weapon && obj.partClone ? obj.partClone->AsNode() : nullptr;
					const char* parent = clone && clone->parent ? clone->parent->name.c_str() : nullptr;
					if (!parent) {
						continue;
					}
					if (hand == parent) {
						return clone;
					}
					if (!a_left && a_weapon->IsBow() && std::string_view(parent) == "SHIELD") {
						other = clone;
					}
				}
				if (other) {
					return other;
				}
			}
			auto* node = root->GetObjectByName(RE::BSFixedString(hand.data()));
			return node ? node->AsNode() : nullptr;
		}

		// the weapons one actor has drawn, and the node each light hangs on, in each view the actor has
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
				auto* form = a_actor->GetEquippedObject(left);
				auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				// a weapon held in both hands is one weapon: it is lit once, from the right hand
				if (!weapon || (left && weapon == a_actor->GetEquippedObject(false) && BothHands(weapon))) {
					continue;
				}
				const bool enchanted = EnchantmentLit(a_actor, left, weapon);
				const int  element = EnchantElement(a_actor, left, weapon);
				for (const bool fp : { false, true }) {
					if (fp && !isPlayer) {
						break;
					}
					const Stream* held = HeldOf(weapon, fp);
					if (!held || !OptionLit(held->option)) {
						continue;
					}
					if (auto* node = NodeFor(a_actor, left, fp, weapon); node && !(enchanted && ReLightOn(node))) {
						a_out.push_back({ a_actor, left, fp, held, node, element });
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
		const bool               hidden = SneakOn() && player->IsSneaking();
		static std::vector<Want> want;  // main thread; kept between frames so its storage is not made again each one
		want.clear();
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
		// the same Brightness x daylight factor and color rules the brightness pass gives every other light of ours (the
		// re-score's P5, 2026-10-08: held lights missed Dim in daylight and the element colors)
		// 🔁 2026-10-10 (his "re add the enb and cs and vanilla versions ... auto detect"): off Community Shaders a held light,
		// made for inverse square lighting, is drawn plain - the house light the hand lights take (LightKit::PlainOf: fade x 1.14,
		// the reach drawn at 178 / 133)
		const bool  isl = IslShader();
		const float scale = Brightness() * DaylightFactor() * (isl ? 1.0f : 1.14f);
		const float reachScale = Reach() * (isl ? 1.0f : 178.0f / 133.0f);
		for (const auto& w : want) {
			const bool have = std::ranges::any_of(gLive, [&w](const Live& v) {
				return w.actor->GetFormID() == v.actor && w.left == v.left && w.firstPerson == v.firstPerson;
			});
			if (have || gLive.size() >= kMaxLive) {
				continue;
			}
			const float        fade = w.held->fade * scale;
			const float        reach = w.held->radius * reachScale;
			const RE::NiPoint3 at = w.held->positions.empty() ? RE::NiPoint3{} : w.held->positions.front();  // the mesh's own origin
			RE::NiPointLight*  light = nullptr;
			const RE::NiColor  color = DrawnColor(w.element, w.held->color);
			auto*              bs = MakeOurLight(*w.held, color, at, fade, reach, w.node, scene, light);
			if (!bs) {
				SKSE::log::warn("[HELD] {}: the light could not be made or registered", w.held->key);
				continue;
			}
			gLive.push_back({ w.actor->GetFormID(), w.left, w.firstPerson, w.held, RE::NiPointer<RE::NiNode>(w.node),
				RE::NiPointer<RE::BSLight>(bs), RE::NiPointer<RE::NiPointLight>(light), w.element, fade, reach, color });
			if (gTold < 16) {
				++gTold;
				SKSE::log::info("[HELD] {} | {:08X} {} hand, {} person | fade {:.2f} | radius {:.0f}", w.held->key, w.actor->GetFormID(),
					w.left ? "left" : "right", w.firstPerson ? "first" : "third", fade, reach);
			}
		}
		// the sliders, the daylight and the color picks reach a light already hung
		for (auto& v : gLive) {
			const float       fade = v.held->fade * scale;
			const float       reach = v.held->radius * reachScale;
			const RE::NiColor color = DrawnColor(v.element, v.held->color);
			auto&             d = v.light->GetLightRuntimeData();
			if (color.red != v.wroteColor.red || color.green != v.wroteColor.green || color.blue != v.wroteColor.blue) {
				d.diffuse = color;
				v.wroteColor = color;
			}
			if (fade != v.written || reach != v.wroteReach) {
				d.fade = fade;
				d.radius.x = reach;
				d.radius.y = reach;
				if (IslShader()) {  // without it these words are ambient color (Plugin.h)
					Isl::SetCutoff(v.light.get(), CutoffFor(fade, reach, v.held->size));
				}
				v.written = fade;
				v.wroteReach = reach;
			}
		}
	}

	std::size_t LiveHeldLights() { return gLive.size(); }
}
