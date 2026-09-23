// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The switches: every tick-box option the installer put down gets one, and a switched-off option's lights are
// put out while the game runs.
//
// How a light is matched to an option:
//   - RE::Light names every light it makes from a config "RL" + the node it hung it on (LightManager.cpp),
//     so the two letters leave every other light in the game alone;
//   - the light hangs under the object's own 3D, so walking up its parents to the reference gives the object;
//   - the reference's base form is what the data files name, by form or by mesh key (Data.cpp).
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
			RE::NiPointer<RE::NiLight> light;
			std::size_t                option;
		};
		std::vector<Held> gHeldOut;

		bool IsReLightLight(RE::NiLight* a_light)
		{
			if (!a_light) {
				return false;
			}
			const char* n = a_light->name.c_str();
			return n && n[0] == 'R' && n[1] == 'L';
		}

		std::size_t OptionOfLight(RE::NiLight* a_light)
		{
			if (!IsReLightLight(a_light)) {
				return kNone;
			}
			auto* ref = ReferenceOf(a_light);
			return ref ? OptionOf(ref->GetBaseObject()) : kNone;
		}

		bool Off(std::size_t a_option)
		{
			auto& opts = Options();
			return a_option < opts.size() && opts[a_option].switchable && !opts[a_option].on;
		}
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

	bool HeldOutForOption(RE::NiLight* a_light)
	{
		return std::ranges::any_of(gHeldOut, [a_light](const Held& h) { return h.light.get() == a_light; });
	}

	void UpdateOptionLights()
	{
		auto& opts = Options();
		for (auto& o : opts) {
			o.lit = 0;
			o.heldOut = 0;
		}
		// give back whatever belongs to a switch that is on again, and forget what has unloaded
		std::erase_if(gHeldOut, [](Held& h) {
			if (!h.light || h.light->GetRefCount() <= 1) {
				return true;
			}
			if (!Off(h.option)) {
				h.light->SetAppCulled(false);
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
				if (!niLight->GetAppCulled()) {
					niLight->SetAppCulled(true);
					gHeldOut.push_back({ RE::NiPointer<RE::NiLight>(niLight), opt });
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
				gHeldOut.push_back({ RE::NiPointer<RE::NiLight>(a_light), opt });
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
}
