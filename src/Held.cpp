// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// A weapon's own light while it is drawn. RE::Light lights a weapon's mesh only where the weapon is a reference (on the
// ground, on a rack) and reaches a weapon in the hand only through its enchantment shader. The build writes the light each
// staff and weapon config of ours gives that mesh as a `held` line; this hangs it on the drawn weapon of whoever holds it
// (the player, in both views, and the actors near them) and takes it off when it is sheathed, unequipped or switched off.
// One object, one light: a weapon whose enchantment shader is one of ours may wear RE::Light's enchantment light in the
// hand, so the held light steps aside the frame that light is on its node (ReLightOn). "Hand lights for" (the light budget
// for big fights) leaves out the same actors here as it does for hand lights. Nothing on any record is edited.
//
// CREDIT: how a light is made and registered here (MakeHeldLight) follows ReLight by Truman (github.com/TrumanGIT/ReLight),
// GPL-3.0-or-later, with his permission and kept under the same license. From ReLight: one master NiPointLight made once
// and cloned for every use (a freshly made light attached straight away crashes), the light's size carried in the
// radius' z, the create parameters a non-shadow light needs (field of view 90, portal-strict, never fades), and handing
// the light to the shadow scene node, which is what renders it.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::size_t kMaxLive = 16;
		constexpr const char* kLightName = "RSAHeld";  // was "RSAStream", from the travelling lights this code came from
		bool                  gCapLogged = false;

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

		// a held light that could not be made or registered: warned about once, and not tried again until that hand stops
		// wanting it (sheathed, unequipped, another weapon)
		struct Failed
		{
			RE::FormID    actor{ 0 };
			bool          left{ false }, firstPerson{ false };
			const Stream* held{ nullptr };

			[[nodiscard]] bool Is(const Want& a_w) const
			{
				return a_w.actor->GetFormID() == actor && a_w.left == left && a_w.firstPerson == firstPerson && a_w.held == held;
			}
		};
		std::vector<Failed> gFailed;

		// off Community Shaders the reach is set through the game's attenuation call, which writes the same two words as the
		// light's ambient color; the ambient is written after it, as when the light was made (the audit, 2026-10-14: the
		// Reach slider moved only the stated radius of a held light on Vanilla and ENB)
		void SetPlainReach(RE::NiPointLight* a_light, float a_reach, const RE::NiColor& a_color)
		{
			a_light->SetLightAttenuation(a_reach);
			a_light->GetLightRuntimeData().ambient = { a_color.red * 0.1f, a_color.green * 0.1f, a_color.blue * 0.1f };
		}

		// one light made and registered the ReLight way, on the main thread; nullptr when it could not be
		RE::BSLight* MakeHeldLight(const Stream& a_s, float a_fade, float a_reach, RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene,
			RE::NiPointLight*& a_made)
		{
			auto* light = LightKit::CloneLight();
			if (!light) {
				return nullptr;
			}
			light->name = kLightName;
			light->fadeAmount = kMovingLightMark;
			auto& data = light->GetLightRuntimeData();
			data.diffuse = a_s.color;
			data.fade = a_fade;
			data.radius = { a_reach, a_reach, a_s.size };  // x and y are the reach; z carries the light's size
			if (IslShader()) {
				light->SetLightAttenuation(a_reach);  // without it the light has no attenuation and lights nothing
				// after SetLightAttenuation, which writes the same two words; the cutoff is re-derived so Brightness moves the
				// peak with the reach held (at 100%/100% it equals the file's). A light of our own, never made by the game's
				// GenDynamic, so Community Shaders set none of its flags: the flag word is ours whole - the attenuation's float
				// left there could read as Disabled (2.2's code review, the hand-light fix)
				Isl::Write(light, { Isl::kFlag, 0u });
				Isl::SetCutoff(light, CutoffFor(a_fade, a_reach, a_s.size));
			} else {
				// off Community Shaders: an ambient of a tenth of its color, RE::Light's rule (Truman); the caller hands a plain
				// fade and reach (fade x 1.14, reach x 178 / 133)
				SetPlainReach(light, a_reach, a_s.color);
			}
			light->local.translate = a_s.at;
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

		// ...and has it? Measured on a Bound Sword: some games it does (its "RL" light, fadeAmount 5, on the weapon),
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
			if ((isPlayer && a_hidden) || LeftOutByBudget(a_actor)) {
				return;  // sneaking, or an actor "Hand lights for" leaves out (the audit, 2026-10-14: held lights ignored it)
			}
			for (const bool left : { false, true }) {
				auto* form = a_actor->GetEquippedObject(left);
				auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				// a weapon held in both hands is one weapon: it is lit once, from the right hand
				if (!weapon || (left && weapon == a_actor->GetEquippedObject(false) && BothHands(weapon))) {
					continue;
				}
				const bool enchanted = EnchantmentLit(a_actor, left, weapon);
				for (const bool fp : { false, true }) {
					if (fp && !isPlayer) {
						break;
					}
					const Stream* held = HeldOf(weapon, fp);
					if (!held || !OptionLit(held->option)) {
						continue;
					}
					if (auto* node = NodeFor(a_actor, left, fp, weapon); node && !(enchanted && ReLightOn(node))) {
						a_out.push_back({ a_actor, left, fp, held, node });
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
		std::erase_if(gFailed, [&](const Failed& f) { return std::ranges::none_of(want, [&f](const Want& w) { return f.Is(w); }); });
		// the same Brightness x daylight factor the brightness pass gives every other light of ours (the re-score's P5: held
		// lights missed Dim in daylight); the color is the light's own
		// 🔁 (his "re add the enb and cs and vanilla versions ... auto detect"): off Community Shaders a held light,
		// made for inverse square lighting, is drawn plain - the house light the hand lights take (LightKit::PlainOf: fade x 1.14,
		// the reach drawn at 178 / 133)
		const bool  isl = IslShader();
		const float scale = Brightness() * DaylightFactor() * (isl ? 1.0f : 1.14f);
		const float reachScale = Reach() * (isl ? 1.0f : 178.0f / 133.0f);
		for (const auto& w : want) {
			const bool have = std::ranges::any_of(gLive, [&w](const Live& v) {
				return w.actor->GetFormID() == v.actor && w.left == v.left && w.firstPerson == v.firstPerson;
			});
			if (have || std::ranges::any_of(gFailed, [&w](const Failed& f) { return f.Is(w); })) {
				continue;
			}
			if (gLive.size() >= kMaxLive) {
				if (!gCapLogged) {
					gCapLogged = true;
					SKSE::log::info("[HELD] {} held weapon lights are lit at once, the most there may be: the rest wait for one to go out",
						kMaxLive);
				}
				continue;
			}
			const float       fade = w.held->fade * scale;
			const float       reach = w.held->radius * reachScale;
			RE::NiPointLight* light = nullptr;
			auto*             bs = MakeHeldLight(*w.held, fade, reach, w.node, scene, light);
			if (!bs) {
				SKSE::log::warn("[HELD] {}: the light could not be made or registered; not tried again until that hand's weapon changes",
					w.held->key);
				gFailed.push_back({ w.actor->GetFormID(), w.left, w.firstPerson, w.held });
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
		// the sliders and the daylight reach a light already hung
		for (auto& v : gLive) {
			const float fade = v.held->fade * scale;
			const float reach = v.held->radius * reachScale;
			if (fade != v.written || reach != v.wroteReach) {
				auto& d = v.light->GetLightRuntimeData();
				d.fade = fade;
				d.radius.x = reach;
				d.radius.y = reach;
				if (isl) {  // without it these words are ambient color (Plugin.h)
					Isl::SetCutoff(v.light.get(), CutoffFor(fade, reach, v.held->size));
				} else if (reach != v.wroteReach) {
					SetPlainReach(v.light.get(), reach, v.held->color);
				}
				v.written = fade;
				v.wroteReach = reach;
			}
		}
	}

	std::size_t LiveHeldLights() { return gLive.size(); }
}
