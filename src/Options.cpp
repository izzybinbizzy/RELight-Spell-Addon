// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// THE PROBE - one installer option, switched on and off while the game runs.
//
// How a light is matched to an option, and why it is done this way:
//   - RE::Light names every light it makes from a config "RL" + the node it hung it on (LightManager.cpp),
//     so the two letters are a cheap way to leave every other light in the game alone;
//   - the light hangs under the object's own 3D, so walking up its parents to the reference gives the
//     object it belongs to;
//   - that reference's base record carries the mesh, and the mesh is what our configs claim. The bare end
//     of the path, with no folder and no .nif, is exactly the key the configs are written with.
// So: light -> reference -> mesh name -> is that one of this option's twelve.
//
// It never edits a config, a record or a file. A light it puts out is held in a list and given back the
// moment the box is ticked again.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// The Runes option, read off `RELight - Spell Addon 1.0.zip` (Full tier) rather than typed from
		// memory: twelve claims, and `fireballexp01` is among them, which is why a Fireball answers the
		// second half of the question.
		constexpr std::array<std::string_view, 12> kRuneMeshes{
			"ashexp01",
			"explosionfrost01",
			"explosionparalysis01",
			"fireballexp01",
			"healinghazard",
			"runeashprojectile",
			"runefireprojectile01",
			"runefrenzyprojectile",
			"runelightningprojectile01",
			"runeparalysisprojectile01",
			"runepoisonprojectile",
			"turnundeadhazard",
		};

		bool                                    gSneakOn = false;
		bool                                    gRunesOn = true;
		std::vector<RE::NiPointer<RE::NiLight>> gHeldOut;
		std::size_t                             gLitLastPass = 0;

		// the bare end of a mesh path, lowercased: "Magic\RuneFireProjectile01.nif" -> "runefireprojectile01"
		std::string MeshKey(std::string_view a_path)
		{
			const auto slash = a_path.find_last_of("\\/");
			if (slash != std::string_view::npos) {
				a_path.remove_prefix(slash + 1);
			}
			if (a_path.size() > 4 && (a_path.ends_with(".nif") || a_path.ends_with(".NIF"))) {
				a_path.remove_suffix(4);
			}
			std::string out{ a_path };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool IsReLightLight(RE::NiLight* a_light)
		{
			if (!a_light) {
				return false;
			}
			const char* n = a_light->name.c_str();
			return n && n[0] == 'R' && n[1] == 'L';
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

		// true when this light belongs to a mesh the Runes option claims
		bool BelongsToRunes(RE::NiLight* a_light)
		{
			if (!IsReLightLight(a_light)) {
				return false;
			}
			auto* ref = ReferenceOf(a_light);
			if (!ref) {
				return false;
			}
			auto* base = ref->GetBaseObject();
			if (!base) {
				return false;
			}
			auto* model = base->As<RE::TESModel>();
			if (!model) {
				return false;
			}
			const char* path = model->GetModel();
			if (!path || !*path) {
				return false;
			}
			const auto key = MeshKey(path);
			return std::ranges::find(kRuneMeshes, key) != kRuneMeshes.end();
		}

		void Prune()
		{
			std::erase_if(gHeldOut, [](const RE::NiPointer<RE::NiLight>& l) { return !l || l->GetRefCount() <= 1; });
		}
	}

	bool SneakOn()
	{
		return gSneakOn;
	}

	void SetSneakOn(bool a_on)
	{
		if (gSneakOn == a_on) {
			return;
		}
		gSneakOn = a_on;
		SKSE::log::info("lights off while sneaking turned {}", a_on ? "on" : "off");
	}

	bool RunesOn()
	{
		return gRunesOn;
	}

	std::size_t RunesLit()
	{
		return gLitLastPass;
	}

	std::size_t RunesHeldOut()
	{
		return gHeldOut.size();
	}

	bool HeldOutForOption(RE::NiLight* a_light)
	{
		return std::ranges::any_of(gHeldOut, [a_light](const RE::NiPointer<RE::NiLight>& l) { return l.get() == a_light; });
	}

	void SetRunesOn(bool a_on)
	{
		if (gRunesOn == a_on) {
			return;
		}
		gRunesOn = a_on;
		SKSE::log::info("probe: Runes turned {}", a_on ? "on" : "off");
	}

	void UpdateOptionLights()
	{
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		std::size_t lit = 0;
		if (gRunesOn) {
			// give back every light this option was holding out, and nothing else
			for (auto& l : gHeldOut) {
				if (l) {
					l->SetAppCulled(false);
				}
			}
			gHeldOut.clear();
			for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
				if (bsLight && bsLight->light && !bsLight->light->GetAppCulled() && BelongsToRunes(bsLight->light.get())) {
					++lit;
				}
			}
		} else {
			for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
				if (!bsLight || !bsLight->light) {
					continue;
				}
				auto* niLight = bsLight->light.get();
				if (niLight->GetAppCulled() || !BelongsToRunes(niLight)) {
					continue;
				}
				niLight->SetAppCulled(true);
				gHeldOut.emplace_back(niLight);
			}
			Prune();
		}
		gLitLastPass = lit;
	}

	void CullOptionLightsUnder(RE::NiAVObject* a_root)
	{
		if (!a_root || gRunesOn) {
			return;
		}
		// The reference is read once from the root: a light under this 3D belongs to this object, and its
		// own parent walk would reach the same place. Doing it here is what stops a switched-off light
		// showing for the frame between the object loading and the next player update.
		auto* ref = ReferenceOf(a_root);
		if (!ref) {
			return;
		}
		auto* base = ref->GetBaseObject();
		auto* model = base ? base->As<RE::TESModel>() : nullptr;
		const char* path = model ? model->GetModel() : nullptr;
		if (!path || !*path) {
			return;
		}
		if (std::ranges::find(kRuneMeshes, MeshKey(path)) == kRuneMeshes.end()) {
			return;
		}
		RE::BSVisit::TraverseScenegraphLights(a_root, [](RE::NiPointLight* a_light) {
			if (a_light && !a_light->GetAppCulled() && IsReLightLight(a_light)) {
				a_light->SetAppCulled(true);
				gHeldOut.emplace_back(a_light);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
}
