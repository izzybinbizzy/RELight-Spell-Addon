// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version. See LICENSE.
//
// THE FILES, AND WHAT EACH ONE IS FOR
//   main.cpp        this file - the hooks, and spell lights going out while you sneak
//   Data.cpp        the data files the installer put down: what this mod lights, its switches, its streams
//   Settings.cpp    the settings file
//   Options.cpp     the switches - an option's lights put out while the game runs
//   Brightness.cpp  our own brightness slider, which scales this mod's lights and nothing else
//   Streams.cpp     sprays, breath shouts and beams: RE::Light lights them, the menu reaches their lights
//   Held.cpp        a weapon's own light while it is drawn (RE::Light reaches a weapon in the hand only by enchantment)
//   Wards.cpp       one ward, one dome, and the ward colour pick - all left to Dynamic Wards when it is loaded
//   HandLights.cpp  the light on the caster's hands, made in memory - no plugin, no script
//   Keep.cpp        the one place a light the game made is held for the lists above, and let go when it leaves the game
//   VaerSwirls.cpp  VAER Reborn's swirl on Thaumaturgy's own enchantment effects, once at data loaded
//   Menu.cpp       the settings page, in SKSE Menu Framework's Mod Control Panel
//   Plugin.h        what they share      PCH.h  what they all include

#include "Plugin.h"

namespace
{
	RE::BSSpinLock gLock;
	// every spell light the game made through the hooked call sites, so sneaking can find it again; and the lights put
	// out while sneaking. Plain pointers: Keep.cpp holds each light, and ForgetSpellLights drops it here before it is freed
	std::unordered_set<RE::NiLight*> gMagicLights;
	std::vector<RE::NiLight*>        gCulled;
	bool                             gWasSneaking = false;

	bool PlayerSneaking()
	{
		if (!Plugin::SneakOn()) {
			return false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player && player->IsSneaking();
	}

	[[nodiscard]] bool IsSpellObject(const RE::TESObjectREFR* a_ref)
	{
		using FT = RE::FormType;
		return a_ref && a_ref->Is(FT::ProjectileMissile, FT::ProjectileArrow, FT::ProjectileGrenade, FT::ProjectileBeam,
							FT::ProjectileFlame, FT::ProjectileCone, FT::ProjectileBarrier, FT::Explosion, FT::PlacedHazard);
	}

	void CullSpellLights()
	{
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			if (niLight->GetAppCulled()) {
				continue;
			}
			if (gMagicLights.contains(niLight) || IsSpellObject(Plugin::ReferenceOf(niLight))) {
				niLight->SetAppCulled(true);
				Plugin::KeepLight(niLight);
				gCulled.push_back(niLight);
			}
		}
	}

	void UncullAll()
	{
		for (auto* l : gCulled) {
			// a light held out for a switched-off option (or a hand light a switch put out) stays out when the player stands up
			if (!Plugin::HeldOutForOption(l) && !Plugin::HandLightHeldOut(l)) {
				l->SetAppCulled(false);
			}
		}
		gCulled.clear();
	}

	// on the main thread only (an SKSE task from the hook below): a new spell light goes into our lists. The last
	// reference to a light is only ever dropped on the main thread (here, or in Keep.cpp's sweep)
	void NoteMagicLight(const RE::NiPointer<RE::NiLight>& a_light, const std::string& a_handKey, bool a_ours)
	{
		if (!a_light || a_light->GetRefCount() <= 1) {
			return;  // the game let go of it before the main thread came round: nothing to note (freed right here)
		}
		Plugin::KeepLight(a_light.get());
		RE::BSSpinLockGuard lock(gLock);
		gMagicLights.insert(a_light.get());
		if (!a_handKey.empty()) {
			Plugin::NoteHandLight(a_light.get(), a_handKey);
		}
		// a hand light of ours, a spray light RE::Light made from our config, and the light of a beam or breath
		// projectile our data names (RE::Light lights it; Streams.cpp) are ours for the sliders
		if (a_ours) {
			Plugin::RememberLight(a_light.get(), a_handKey.empty() ? nullptr : Plugin::HandFxOf(a_handKey));
		}
		// a light made while the player was already sneaking was never made (the hook returns nullptr); one made just
		// before she crouched is put out by the next player update's sneaking pass, which reads gMagicLights
	}

	struct MagicLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* a_light, RE::TESObjectREFR* a_ref, RE::NiNode* a_node,
			bool a_forceDynamic, bool a_useLightRadius, bool a_affectRequesterOnly)
		{
			if (PlayerSneaking()) {
				return nullptr;
			}
			// our hand lights are made with the game's own function and never reach RE::Light, which would switch off
			// a casting light it has no config for (an in-memory record can have none)
			const Plugin::Hand* hand = Plugin::HandOfLight(a_light);
			RE::NiPointLight*   made = nullptr;
			if (hand) {
				made = netimmerse_cast<RE::NiPointLight*>(
					a_light->GenDynamic(a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly));
				Plugin::DressHandLight(made, *hand);  // only writes the fresh light's own fields
			} else {
				made = func(a_light, a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly);
			}
			// ⚠ 2026-09-29, the Lightning Bolt freeze (found with Truman, by switching hooks off one at a time): this hook
			// also runs on the game's loader and job threads. It used to keep the light in our lists right here and, every
			// 64th light, let go of lights nothing else held - so a light could be FREED on a loader thread while the game's
			// threads walked the scene. RE::Light's own hook keeps nothing (it marks the light and returns); ours now hands
			// the light to the main thread (an SKSE task, Truman's route for main-thread work), where every list of ours is
			// filled and emptied. The task holds the light until then, and lets go of it on the main thread too.
			if (made) {
				const bool ours = hand || Plugin::IsSprayLight(a_light) || Plugin::IsStreamObject(a_ref);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([light = RE::NiPointer<RE::NiLight>(made), key = hand ? hand->key : std::string{}, ours]() {
						NoteMagicLight(light, key, ours);
					});
				}
			}
			return made;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void CullTree(RE::NiAVObject* a_root)
	{
		if (!a_root) {
			return;
		}
		RE::BSVisit::TraverseScenegraphLights(a_root, [](RE::NiPointLight* a_light) {
			if (a_light && !a_light->GetAppCulled()) {
				a_light->SetAppCulled(true);
				Plugin::KeepLight(a_light);
				gCulled.push_back(a_light);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	template <class T>
	struct Load3D
	{
		static RE::NiAVObject* thunk(T* a_this, bool a_backgroundLoading)
		{
			auto* root = func(a_this, a_backgroundLoading);
			if (root) {
				RE::BSSpinLockGuard lock(gLock);
				if (PlayerSneaking()) {
					CullTree(root);
				} else {
					// RE::Light has already hung its light here; put a switched-off option's out before its first frame
					Plugin::CullOptionLightsUnder(root);
				}
			}
			return root;
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			func = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6A, thunk);
		}
	};

	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			// first: every list forgets the lights that left the game since the last frame, and they are freed
			Plugin::SweepKeptLights();
			const bool          sneaking = Plugin::SneakOn() && a_this && a_this->IsSneaking();
			RE::BSSpinLockGuard lock(gLock);
			if (sneaking) {
				CullSpellLights();
			} else if (gWasSneaking) {
				UncullAll();
			}
			gWasSneaking = sneaking;
			// after the sneaking pass, in the same frame, so a light given back above and then held out
			// here never reaches the screen in between
			Plugin::UpdateOptionLights();
			Plugin::UpdateHeldLights();
			// last: RE::Light has already written this frame's fades (its update runs inside `func` above)
			Plugin::UpdateBrightness(a_delta);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Install()
	{
		auto&                                                         trampoline = SKSE::GetTrampoline();
		std::vector<std::pair<REL::RelocationID, REL::VariantOffset>> sites{
			{ RELOCATION_ID(33603, 34381), REL::VariantOffset(0xAC, 0xE2, 0xE2) },
			{ RELOCATION_ID(33391, 34151), REL::VariantOffset(0x86, 0xCD, 0x86) },
			{ RELOCATION_ID(42965, 44222), REL::VariantOffset(0x58, 0x36D, 0x58) }
		};
		if (REL::Module::IsAE()) {
			sites.push_back({ RELOCATION_ID(33403, 34185), REL::VariantOffset(0x407, 0x407, 0x407) });
		}
		std::uintptr_t first = 0;
		for (auto& [id, off] : sites) {
			REL::Relocation<std::uintptr_t> target{ id, off };
			auto                            old = trampoline.write_call<5>(target.address(), MagicLight::thunk);
			if (!first) {
				first = old;
				MagicLight::func = old;
			} else if (old != first) {
				SKSE::log::warn("spell light call sites lead to different functions; using the first");
			}
		}
		// if the call sites still lead to the game's own function, RE::Light hooked nothing here
		const auto game = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(17208, 17610) }.address();
		SKSE::log::info("spell lights: {} call sites hooked, after every plugin loaded; they led to {}", sites.size(),
			first == game ? "the game's own function (RE::Light has no hook there)" : "another plugin's hook (RE::Light's)");
	}

	void InstallLate()
	{
		Load3D<RE::MissileProjectile>::Install();
		Load3D<RE::ArrowProjectile>::Install();
		Load3D<RE::GrenadeProjectile>::Install();
		Load3D<RE::BeamProjectile>::Install();
		Load3D<RE::FlameProjectile>::Install();
		Load3D<RE::ConeProjectile>::Install();
		Load3D<RE::BarrierProjectile>::Install();
		Load3D<RE::Explosion>::Install();
		Load3D<RE::Hazard>::Install();
		REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		SKSE::log::info("projectile, explosion and hazard loads and the player update hooked after every plugin loaded");
	}
}

void Plugin::ForgetSpellLights(const GoneLights& a_gone)
{
	RE::BSSpinLockGuard lock(gLock);
	std::erase_if(gMagicLights, [&](const RE::NiLight* a_l) { return a_gone.contains(a_l); });
	std::erase_if(gCulled, [&](const RE::NiLight* a_l) { return a_gone.contains(a_l); });
	ForgetOptionLights(a_gone);  // its list lives under this lock too (the player update and the load hooks)
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		if (!a_msg) {
			return;
		}
		if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
			// after every plugin has loaded: the hook written last runs first, and ours must run before RE::Light's
			Install();
			// Dynamic Wards 2.0 changes a ward's casting art from its menu; its hand light follows
			SKSE::GetMessagingInterface()->RegisterListener("DynamicWards", [](SKSE::MessagingInterface::Message* a_m) {
				if (a_m && a_m->type == 'DWAC') {
					Plugin::RefindHandLights();
				}
			});
		} else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Plugin::LoadData();
			Plugin::LoadSettings();
			Plugin::ClaimSprayLights();         // before the hand lights remember each effect's own light
			Plugin::ApplyWards("data loaded");  // before the hand lights: a silenced ward effect must not get one
			Plugin::MakeHandLights();
			Plugin::VaerSwirls();  // after LoadData: it needs to know whether the VAER Reborn option is installed
			InstallLate();
			Plugin::RegisterMenu();
		} else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame || a_msg->type == SKSE::MessagingInterface::kNewGame) {
			Plugin::ApplyWards("a save loaded");  // a ward mod that resets art at load may have put it back
			Plugin::RefindHandLights();           // Dynamic Wards 2.0's ranked hand art, set at data load in whichever order
		}
	});
	return true;
}
