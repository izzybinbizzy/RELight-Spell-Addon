// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The switches: every tick-box option the installer put down gets one, and a switched-off option's lights are
// put out while the game runs.
//
// How a light is matched to an option:
//   - RE::Light names every light it makes from a config "RL" + the node it hung it on (LightManager.cpp),
//     so the two letters leave every other light in the game alone;
//   - the light hangs under the object's own 3D, so walking up its parents to the reference gives the object;
//   - the reference's base form is what the data files name, by form or by mesh key (Data.cpp);
//   - an ENCHANTMENT light is the exception: RE::Light hangs it on the weapon node of the ACTOR holding the weapon
//     (ShaderReferenceEffect::Init, LightAttachmentHooks.cpp) and marks it `fadeAmount = 5`, so the walk up finds the
//     actor, whose base no data file names - his report, 2026-09-26: "vibrant weapons and vaer are saying 0 lit". Each
//     frame every live effect shader of ours is walked the way RE::Light attached to it, and the marked lights under
//     its root belong to the option whose data names that shader's editor ID.
//
// It never edits a config, a record or a file. A light it puts out is held in a list and given back the
// moment the switch is turned on again.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		struct Held
		{
			RE::NiLight* light;  // held in Keep.cpp; ForgetOptionLights drops it here before it is freed
			std::size_t  option;
			bool         enchant{ false };
		};
		std::vector<Held>                                      gHeldOut;
		std::unordered_map<const RE::NiLight*, std::size_t>    gEnchant;  // this frame's enchantment lights -> option

		bool IsReLightLight(RE::NiLight* a_light)
		{
			if (!a_light) {
				return false;
			}
			const char* n = a_light->name.c_str();
			return n && n[0] == 'R' && n[1] == 'L';
		}

		// RE::Light's own mark on an enchantment light: "RL" and a fadeAmount of 5 (its sheathe handler reads the same two)
		bool IsEnchantLight(const RE::NiLight* a_light)
		{
			const char* n = a_light ? a_light->name.c_str() : nullptr;
			return n && n[0] == 'R' && n[1] == 'L' && a_light->fadeAmount == 5.0f;
		}

		// where RE::Light attached for this effect (GetReferenceAttachRoot, then the third-person node of the same name)
		RE::NiAVObject* EnchantRootOf(RE::ShaderReferenceEffect* a_effect)
		{
			if (const auto* weap = skyrim_cast<RE::WeaponEnchantmentController*>(a_effect->controller); weap && !weap->shader) {
				return nullptr;
			}
			auto* root = a_effect->GetAttachRoot();
			const auto ref = a_effect->target.get();
			if (!root || !ref) {
				return nullptr;
			}
			if (auto* third = ref->Get3D(false)) {
				if (auto* same = third->GetObjectByName(root->name)) {
					root = same;
				}
			}
			return root;
		}

		void FindEnchantLights()
		{
			gEnchant.clear();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			lists->ForEachShaderEffect([](RE::ShaderReferenceEffect* a_effect) {
				const auto opt = a_effect ? OptionOfShader(a_effect->effectData) : kNone;
				auto*      root = opt != kNone ? EnchantRootOf(a_effect) : nullptr;
				if (root) {
					RE::BSVisit::TraverseScenegraphLights(root, [opt](RE::NiPointLight* a_light) {
						if (IsEnchantLight(a_light)) {
							gEnchant.insert_or_assign(a_light, opt);
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		std::size_t OptionOfLight(RE::NiLight* a_light)
		{
			if (!IsReLightLight(a_light)) {
				return kNone;
			}
			if (IsEnchantLight(a_light)) {
				return EnchantOptionOf(a_light);
			}
			auto* ref = ReferenceOf(a_light);
			return ref ? OptionOf(ref->GetBaseObject()) : kNone;
		}

		bool Off(std::size_t a_option) { return a_option != kNone && !OptionLit(a_option); }

		// RE::Light culls an enchantment light on sheathe: a switch turned back on must not light a sheathed weapon
		bool MayGiveBack(const Held& a_held)
		{
			if (!a_held.enchant) {
				return true;
			}
			auto* ref = ReferenceOf(a_held.light);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			return actor && actor->AsActorState()->IsWeaponDrawn();
		}
	}

	std::size_t EnchantOptionOf(const RE::NiLight* a_light)
	{
		const auto it = gEnchant.find(a_light);
		return it != gEnchant.end() ? it->second : kNone;
	}

	RE::TESObjectREFR* ReferenceOf(RE::NiAVObject* a_obj)
	{
		for (auto* o = a_obj; o; o = o->parent) {
			if (auto* ref = o->GetUserData()) {
				return ref;
			}
		}
		return nullptr;
	}

	bool HeldOutForOption(const RE::NiLight* a_light)
	{
		return std::ranges::any_of(gHeldOut, [a_light](const Held& h) { return h.light == a_light; });
	}

	void ForgetOptionLights(const GoneLights& a_gone)
	{
		std::erase_if(gHeldOut, [&](const Held& h) { return a_gone.contains(h.light); });
	}

	void UpdateOptionLights()
	{
		auto& opts = Options();
		for (auto& o : opts) {
			o.lit = 0;
			o.heldOut = 0;
		}
		FindEnchantLights();
		// give back whatever belongs to a switch that is on again (what has unloaded is already forgotten: Keep.cpp)
		std::erase_if(gHeldOut, [](Held& h) {
			if (!Off(h.option)) {
				if (MayGiveBack(h)) {
					h.light->SetAppCulled(false);
				}
				return true;
			}
			return false;
		});
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto*      niLight = bsLight->light.get();
			const auto opt = OptionOfLight(niLight);
			if (opt == kNone) {
				continue;
			}
			if (Off(opt)) {
				// RE::Light unculls its enchantment light on each new equip, so one already held can come round again
				if (!niLight->GetAppCulled()) {
					niLight->SetAppCulled(true);
					if (!HeldOutForOption(niLight)) {
						KeepLight(niLight);
						gHeldOut.push_back({ niLight, opt, IsEnchantLight(niLight) });
					}
				}
			} else if (!niLight->GetAppCulled()) {
				++opts[opt].lit;
			}
		}
		for (auto& h : gHeldOut) {
			if (h.option < opts.size()) {
				++opts[h.option].heldOut;
			}
		}
	}

	void CullOptionLightsUnder(RE::NiAVObject* a_root)
	{
		if (!a_root) {
			return;
		}
		// done at load so a switched-off light never shows for the frame before the next player update
		auto* ref = ReferenceOf(a_root);
		const auto opt = ref ? OptionOf(ref->GetBaseObject()) : kNone;
		if (!Off(opt)) {
			return;
		}
		RE::BSVisit::TraverseScenegraphLights(a_root, [opt](RE::NiPointLight* a_light) {
			if (a_light && !a_light->GetAppCulled() && IsReLightLight(a_light)) {
				a_light->SetAppCulled(true);
				KeepLight(a_light);
				gHeldOut.push_back({ a_light, opt });
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
}
